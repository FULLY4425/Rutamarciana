import json
import os
import sys
import tempfile
from pathlib import Path

from flask import Flask, abort, jsonify, request, send_file, send_from_directory

ROOT = Path(__file__).resolve().parent.parent
if str(ROOT) not in sys.path:
    sys.path.insert(0, str(ROOT))

WEB = ROOT / "web"


def create_app(data_dir: str):
    app = Flask(__name__, static_folder=None)
    root = Path(data_dir).resolve()

    @app.get("/health")
    def health():
        return jsonify(ok=True, files=sorted(p.name for p in root.glob("*")))

    @app.get("/")
    def index():
        return send_from_directory(WEB, "index.html")

    @app.get("/app.js")
    def app_js():
        return send_from_directory(WEB, "app.js")

    @app.get("/style.css")
    def style_css():
        return send_from_directory(WEB, "style.css")

    @app.get("/mock_routes.json")
    def mock():
        return send_from_directory(WEB, "mock_routes.json")

    @app.get("/preview.png")
    def preview():
        f = root / "preview.png"
        if not f.exists():
            abort(404)
        return send_file(f, mimetype="image/png")

    @app.get("/api/routes")
    def routes():
        f = root / "routes.json"
        if not f.exists():
            return jsonify(error="falta routes.json; ejecuta el pipeline"), 503
        data = json.loads(f.read_text(encoding="utf-8"))
        if (data.get("schema_version") != 3 or "cell_m" not in data or "cell_x_m" not in data
                or data.get("dem_validity") != "nodata-preserved-v1"):
            return jsonify(error="routes.json antiguo; regenera el pipeline"), 503
        return jsonify(data)

    @app.get("/api/manifest")
    def manifest():
        f = root / "routes.json"
        if not f.exists():
            abort(404)
        try:
            data = json.loads(f.read_text(encoding="utf-8"))
        except ValueError:
            return jsonify(error="routes.json inválido; regenera el pipeline"), 503
        raw = data.get("routes") if isinstance(data.get("routes"), dict) else {}
        out = {"site": data.get("site", "unknown"),
               "waypoints": [{"id": w.get("id"), "name": w.get("name"), "row": w.get("row"), "col": w.get("col")}
                             for w in data.get("waypoints", [])
                             if isinstance(w, dict) and isinstance(w.get("name"), str) and w.get("name")],
               "routes": {k: {"time_s": v.get("time_s"), "distance_m": v.get("distance_m"), "points": v.get("points")}
                          for k, v in raw.items() if k in ("safe", "fast") and isinstance(v, dict)},
               "budget_s": data.get("budget_s"), "science_s": data.get("science_s"),
               "science_stops": data.get("science_stops")}
        return jsonify(out)

    @app.post("/api/route")
    def compute():
        import numpy as np

        from pipeline.route import plan_routes

        body = request.get_json(force=True, silent=True) or {}
        if not isinstance(body, dict):
            return jsonify(error="se requiere un objeto JSON"), 400
        if "schema_version" in body and (type(body["schema_version"]) is not int or body["schema_version"] != 3):
            return jsonify(error="schema_version incompatible; usa versión 3"), 400
        start = body.get("start")
        end = body.get("end")
        if not (isinstance(start, list) and isinstance(end, list)):
            return jsonify(error="start y end deben ser [row, col]"), 400
        if "science_stops" in body:
            return jsonify(error="science_stops legado no define ubicaciones; envía science_waypoints"), 400
        waypoints = body.get("science_waypoints", [])
        if not isinstance(waypoints, list):
            return jsonify(error="science_waypoints debe ser una lista ordenada"), 400

        files = [root / f"{name}.npy" for name in ("dem", "slope", "rough")]
        if not all(p.exists() for p in files):
            return jsonify(error="faltan dem.npy / slope.npy / rough.npy; regenera el pipeline"), 503

        mf = root / "routes.json"
        if not mf.exists():
            return jsonify(error="falta routes.json; regenera el pipeline"), 503
        meta = json.loads(mf.read_text(encoding="utf-8"))
        if (meta.get("schema_version") != 3 or "cell_m" not in meta or "cell_x_m" not in meta
                or meta.get("dem_validity") != "nodata-preserved-v1"):
            return jsonify(error="routes.json legado o incompleto; regenera a schema_version 3 y selecciona waypoints"), 503
        cell_m = meta["cell_m"]
        out = {"schema_version": 3, "start": start, "end": end,
               "science_waypoints": waypoints, "science_stops": len(waypoints),
               "budget_model": "legacy_provisional",
               "cell_m": cell_m, "cell_x_m": meta["cell_x_m"], "grid": meta.get("grid", {}),
               "budget_s": meta.get("budget_s", 14400),
               "science_s": meta.get("science_s", 600),
               "reserve_fraction": meta.get("reserve_fraction", 0.15)}
        try:
            out.update(plan_routes(*(np.load(p, allow_pickle=False) for p in files),
                                   cell_m, start, end, out["budget_s"], out["science_s"],
                                   waypoints, out["reserve_fraction"],
                                   cell_x_m=out["cell_x_m"]))
        except (ValueError, TypeError, IndexError) as exc:
            return jsonify(error=str(exc)), 400
        return jsonify(out)

    @app.post("/api/bitacora")
    def bitacora():
        body = request.get_json(silent=True)
        if not isinstance(body, dict):
            return jsonify(error="se requiere un objeto JSON"), 400
        ident = body.get("id")
        if not ((type(ident) is int and ident >= 0) or
                (isinstance(ident, str) and ident.isascii() and ident.isdecimal())):
            return jsonify(error="id inválido"), 400
        if not isinstance(body.get("fecha"), str) or not isinstance(body.get("entradas"), list):
            return jsonify(error="fecha o entradas inválidas"), 400
        directory = root / "bitacora"
        directory.mkdir(parents=True, exist_ok=True)
        name = None
        try:
            with tempfile.NamedTemporaryFile(mode="w", encoding="utf-8", dir=directory,
                                             prefix=".bitacora-", delete=False) as f:
                name = f.name
                json.dump(body, f, ensure_ascii=False)
                f.flush()
                os.fsync(f.fileno())
            os.replace(name, directory / (str(ident) + ".json"))
        finally:
            if name and os.path.exists(name):
                os.unlink(name)
        return jsonify(ok=True)

    return app


if __name__ == "__main__":
    import sys

    create_app(sys.argv[1] if len(sys.argv) > 1 else "data/out").run(
        host="0.0.0.0", port=8080, debug=False
    )
