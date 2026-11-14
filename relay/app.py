import json
import sys
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
            return send_from_directory(WEB, "mock_routes.json")
        return jsonify(json.loads(f.read_text(encoding="utf-8")))

    @app.post("/api/route")
    def compute():
        import numpy as np

        from pipeline.route import least_cost_route

        body = request.get_json(force=True, silent=True) or {}
        start = body.get("start")
        end = body.get("end")
        if not (isinstance(start, list) and isinstance(end, list)):
            return jsonify(error="start y end deben ser [row, col]"), 400

        cf = root / "cost_fast.npy"
        cs = root / "cost_safe.npy"
        if not (cf.exists() and cs.exists()):
            return jsonify(error="faltan cost_fast.npy / cost_safe.npy"), 503

        meta = {}
        mf = root / "routes.json"
        if mf.exists():
            meta = json.loads(mf.read_text(encoding="utf-8"))
        cell_m = meta.get("cell_m", 4.0)
        rough = np.load(root / "rough.npy") if (root / "rough.npy").exists() else None

        out = {"cell_m": cell_m, "grid": meta.get("grid", {}),
               "budget_s": meta.get("budget_s", 14400),
               "science_s": meta.get("science_s", 600)}

        for name, path in (("safe", cs), ("fast", cf)):
            cost = np.load(path)
            pts, sec = least_cost_route(cost, tuple(start), tuple(end), cell_m)
            out[name] = {
                "points": [[int(r), int(c)] for r, c in pts],
                "time_s": round(sec, 1),
                "distance_m": round(len(pts) * cell_m, 1),
                "rough_mean": round(float(np.mean([rough[r, c] for r, c in pts])), 3) if rough is not None else None,
            }
        return jsonify(out)

    return app


if __name__ == "__main__":
    import sys

    create_app(sys.argv[1] if len(sys.argv) > 1 else "data/out").run(
        host="0.0.0.0", port=8080, debug=False
    )
