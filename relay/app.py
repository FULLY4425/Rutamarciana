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

    def valid_scenario(data):
        from pipeline.scenario import DEFAULTS, PROFILE, metadata, scenario
        profile = data.get('scenario')
        if (data.get('budget_model') != PROFILE or not isinstance(profile, dict)
                or profile.get('scenario_id') != PROFILE or profile.get('profile') != PROFILE
                or profile.get('assumption_basis') != 'lunar_analogue'
                or profile.get('site') not in ('Jezero/Mars', 'synthetic', 'unknown')
                or not isinstance(profile.get('values'), dict)
                or set(profile['values']) != set(DEFAULTS)):
            return False
        try:
            overrides = profile['overrides']
            if set(overrides) != {'build', 'request'} or overrides['request'] != {}:
                return False
            scenario(profile['values'])
            values = scenario(overrides['build'])
            expected = metadata(values, profile['site'], overrides['build'])
            if any(profile.get(key) != value for key, value in expected.items()):
                return False
            if 'site' in data:
                site = 'Jezero/Mars' if data['site'] == 'jez_dtm_1m' else ('synthetic' if data['site'] == 'synth' else 'unknown')
                if profile['site'] != site:
                    return False
            if any(key in data and data[key] != values[field] for key, field in
                   (('budget_s', 'suit_budget_s'), ('science_s', 'science_stop_s'))):
                return False
            if 'science_waypoints' in data and (not isinstance(data['science_waypoints'], list)
                    or ('science_stops' in data and data['science_stops'] != len(data['science_waypoints']))):
                return False
        except (ValueError, TypeError, KeyError, AttributeError):
            return False
        return True

    def read_manifest():
        def invalid_constant(value):
            raise ValueError(f'constante JSON inválida: {value}')
        try:
            data = json.loads((root / 'routes.json').read_text(encoding='utf-8'), parse_constant=invalid_constant)
            return data if isinstance(data, dict) else None
        except (OSError, UnicodeError, ValueError):
            return None

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

    @app.get("/reference-image")
    def reference_image():
        return send_from_directory(ROOT / "assets", "jezero_ortho_4m.png")

    @app.get("/navigation.js")
    def navigation_js():
        return send_from_directory(WEB, "navigation.js")

    @app.get("/guidance.js")
    def guidance_js():
        return send_from_directory(WEB, "guidance.js")

    @app.get("/terrain.js")
    def terrain_js():
        return send_from_directory(WEB, "terrain.js")

    @app.get("/api/terrain")
    def terrain():
        import numpy as np
        meta = read_manifest()
        path = root / "dem.npy"
        if meta is None or not path.exists():
            return jsonify(error="Genera el terreno con make demo o pipeline.build --dem"), 503
        dem = np.load(path, allow_pickle=False)
        # Include both edges and bound payload / rendering cost.
        rows = np.unique(np.linspace(0, dem.shape[0] - 1, min(75, dem.shape[0])).astype(int))
        cols = np.unique(np.linspace(0, dem.shape[1] - 1, min(75, dem.shape[1])).astype(int))
        z = dem[np.ix_(rows, cols)]
        return jsonify(rows=rows.tolist(), cols=cols.tolist(),
                       elevations=[[float(v) if np.isfinite(v) else None for v in row] for row in z],
                       grid=meta["grid"], cell_m=meta["cell_m"], cell_x_m=meta["cell_x_m"],
                       site=meta.get("site", "unknown"), spatial=meta.get("spatial", {}))

    @app.get("/api/manifest")
    def manifest():
        f = root / "routes.json"
        if not f.exists():
            abort(404)
        data = read_manifest()
        if data is None:
            return jsonify(error="routes.json inválido; regenera el pipeline"), 503
        if request.args.get("full") == "1":
            return send_file(f, mimetype="application/json")
        routes = data.get("routes")
        raw = routes if isinstance(routes, dict) else {}
        waypoints = data.get("waypoints")
        return jsonify({
            "site": data.get("site", "unknown"),
            "waypoints": [{"id": w.get("id"), "name": w.get("name"), "row": w.get("row"), "col": w.get("col")}
                          for w in (waypoints if isinstance(waypoints, list) else [])
                          if isinstance(w, dict) and isinstance(w.get("name"), str) and w.get("name")],
            "routes": {k: {"time_s": v.get("time_s"), "distance_m": v.get("distance_m"), "points": v.get("points")}
                       for k, v in raw.items() if k in ("safe", "fast") and isinstance(v, dict)},
            "budget_s": data.get("budget_s"), "science_s": data.get("science_s"),
            "science_stops": data.get("science_stops"),
        })

    @app.get("/api/routes")
    def routes():
        f = root / "routes.json"
        if not f.exists():
            return jsonify(error="falta routes.json; ejecuta el pipeline"), 503
        data = read_manifest()
        if (data is None or data.get("schema_version") != 3 or "cell_m" not in data or "cell_x_m" not in data
                or data.get("dem_validity") != "nodata-preserved-v1"
                or not valid_scenario(data)):
            return jsonify(error="routes.json antiguo; regenera el pipeline"), 503
        return jsonify(data)

    @app.post("/api/route")
    def compute():
        import numpy as np

        from pipeline.route import plan_routes
        from pipeline.scenario import scenario, metadata

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
        meta = read_manifest()
        if (meta is None or meta.get("schema_version") != 3 or "cell_m" not in meta or "cell_x_m" not in meta
                or meta.get("dem_validity") != "nodata-preserved-v1"
                or not valid_scenario(meta)):
            return jsonify(error="routes.json legado o incompleto; regenera a schema_version 3 y selecciona waypoints"), 503
        cell_m = meta["cell_m"]
        try:
            base = scenario(meta.get('scenario', {}).get('values'))
            overrides = body.get('scenario_assumptions', {})
            if not isinstance(overrides, dict):
                raise ValueError('scenario_assumptions debe ser un objeto')
            values = scenario({**base, **overrides})
        except (ValueError, TypeError, AttributeError) as exc:
            return jsonify(error=str(exc)), 400
        out = {"schema_version": 3, "start": start, "end": end,
               "science_waypoints": waypoints, "science_stops": len(waypoints),
               "budget_model": "LUNAR-ANALOG-2026", "scenario": metadata(values, meta['scenario']['site'], meta['scenario']['overrides']['build'], overrides),
               "cell_m": cell_m, "cell_x_m": meta["cell_x_m"], "grid": meta.get("grid", {}),
               "budget_s": values['suit_budget_s'],
               "science_s": values['science_stop_s']}
        try:
            out.update(plan_routes(*(np.load(p, allow_pickle=False) for p in files),
                                   cell_m, start, end, out["budget_s"], out["science_s"],
                                   waypoints, 0,
                                   cell_x_m=out["cell_x_m"], scenario_assumptions=values))
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
