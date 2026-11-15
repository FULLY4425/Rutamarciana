import json
from pathlib import Path
from pipeline.scenario import scenario, metadata


def write_manifest(path: Path, *, site, waypoints, layers, routes, budget_s, science_s,
                   cell_m=4.0, grid=None, science_waypoints=None, reserve_fraction=0.15,
                   cell_x_m=None, spatial=None, dem_validity=None, scenario_assumptions=None,
                   scenario_overrides=None):
    science_waypoints = [] if science_waypoints is None else science_waypoints
    values = scenario({'suit_budget_s': budget_s, 'science_stop_s': science_s,
                       **(scenario_assumptions or {})})
    if scenario_overrides is not None and scenario(scenario_overrides) != values:
        raise ValueError('scenario_overrides no coincide con los valores efectivos')
    doc = {
        "schema_version": 3,
        "dem_validity": dem_validity,
        "site": site,
        "cell_m": cell_m,
        "cell_x_m": cell_m if cell_x_m is None else cell_x_m,
        "spatial": spatial or {},
        "grid": grid or {},
        "budget_s": values['suit_budget_s'],
        "science_s": values['science_stop_s'],
        "science_waypoints": science_waypoints,
        "science_stops": len(science_waypoints),
        "budget_model": "LUNAR-ANALOG-2026",
        "scenario": metadata(values, 'Jezero/Mars' if site == 'jez_dtm_1m' else ('synthetic' if site == 'synth' else 'unknown'), scenario_overrides),
        "waypoints": waypoints,
        "layers": layers,
        "routes": routes,
        "note": "EVA budget is simulated from the planned route; no suit telemetry.",
    }
    Path(path).parent.mkdir(parents=True, exist_ok=True)
    Path(path).write_text(json.dumps(doc, indent=2), encoding="utf-8")
    return doc
