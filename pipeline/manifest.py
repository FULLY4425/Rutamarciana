import json
from pathlib import Path


def write_manifest(path: Path, *, site, waypoints, layers, routes, budget_s, science_s,
                   cell_m=4.0, grid=None, science_waypoints=None, reserve_fraction=0.15,
                   cell_x_m=None, spatial=None):
    science_waypoints = [] if science_waypoints is None else science_waypoints
    doc = {
        "schema_version": 3,
        "site": site,
        "cell_m": cell_m,
        "cell_x_m": cell_m if cell_x_m is None else cell_x_m,
        "spatial": spatial or {},
        "grid": grid or {},
        "budget_s": budget_s,
        "science_s": science_s,
        "science_waypoints": science_waypoints,
        "science_stops": len(science_waypoints),
        "budget_model": "legacy_provisional",
        "reserve_fraction": reserve_fraction,
        "waypoints": waypoints,
        "layers": layers,
        "routes": routes,
        "note": "EVA budget is simulated from the planned route; no suit telemetry.",
    }
    Path(path).parent.mkdir(parents=True, exist_ok=True)
    Path(path).write_text(json.dumps(doc, indent=2), encoding="utf-8")
    return doc
