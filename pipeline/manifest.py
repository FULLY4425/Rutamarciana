import json
from pathlib import Path


def write_manifest(path: Path, *, site, waypoints, layers, routes, budget_s, science_s,
                   cell_m=4.0, grid=None, science_stops=5):
    doc = {
        "site": site,
        "cell_m": cell_m,
        "grid": grid or {},
        "budget_s": budget_s,
        "science_s": science_s,
        "science_stops": science_stops,
        "waypoints": waypoints,
        "layers": layers,
        "routes": routes,
        "note": "EVA budget is simulated from the planned route; no suit telemetry.",
    }
    Path(path).parent.mkdir(parents=True, exist_ok=True)
    Path(path).write_text(json.dumps(doc, indent=2), encoding="utf-8")
    return doc
