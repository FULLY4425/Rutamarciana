import argparse
import json
from pathlib import Path

import numpy as np

from pipeline.cost import cost_grid, DOSE_MSV_PER_HOUR
from pipeline.manifest import write_manifest
from pipeline.route import least_cost_route
from pipeline.synth import hills
from pipeline.terrain import slope_deg
from pipeline.roughness import rough_from_dem
from pipeline.tiles import save_png, save_tile_bmp, to_u8

MARS_KM_PER_DEG = 59.27


def _load_dem(path, target_cell_m, rows, cols):
    """Lee un GeoTIFF, recorta el centro y promedia a bloques de `target_cell_m`."""
    import rasterio

    with rasterio.open(path) as ds:
        z = ds.read(1).astype(np.float32)
        nod = ds.nodata
        if nod is not None:
            z[z == nod] = np.nan
        z[z < -1000] = np.nan
        tr = ds.transform
        geo = ds.crs.is_geographic if ds.crs else False
        if geo:
            cy = (ds.bounds.top + ds.bounds.bottom) / 2.0
            cell_x = abs(tr.a) * MARS_KM_PER_DEG * 1000.0 * np.cos(np.radians(cy))
            cell_y = abs(tr.e) * MARS_KM_PER_DEG * 1000.0
        else:
            cell_x, cell_y = abs(tr.a), abs(tr.e)

    cell = float(np.mean([cell_x, cell_y]))
    f = max(1, int(round(target_cell_m / cell)))
    r0 = max(0, (z.shape[0] - rows * f) // 2)
    c0 = max(0, (z.shape[1] - cols * f) // 2)
    z = z[r0:r0 + rows * f, c0:c0 + cols * f]
    z = np.nan_to_num(z, nan=float(np.nanmedian(z)))
    z = z.reshape(z.shape[0] // f, f, z.shape[1] // f, f).mean(axis=(1, 3))
    return z.astype(np.float32), cell * f


def _route_block(pts, seconds, cell_m, rough, dose_rate):
    return {
        "points": [[int(r), int(c)] for r, c in pts],
        "time_s": round(seconds, 1),
        "distance_m": round(len(pts) * cell_m, 1),
        "rough_mean": round(float(np.mean([rough[r, c] for r, c in pts])), 3),
        "dose_msv": round(seconds / 3600.0 * dose_rate, 4),
    }


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--synth", action="store_true")
    ap.add_argument("--dem")
    ap.add_argument("--out", default="data/out")
    ap.add_argument("--cell", type=float, default=4.0)
    ap.add_argument("--rows", type=int, default=500)
    ap.add_argument("--cols", type=int, default=500)
    ap.add_argument("--start", type=int, nargs=2)
    ap.add_argument("--end", type=int, nargs=2)
    ap.add_argument("--budget-h", type=float, default=4.0)
    ap.add_argument("--science-min", type=float, default=10.0)
    a = ap.parse_args()

    out = Path(a.out)
    if a.synth:
        dem, cell_m = hills(size=a.rows, cell_m=a.cell)
    else:
        if not a.dem:
            raise SystemExit("falta --dem o --synth")
        dem, cell_m = _load_dem(a.dem, a.cell, a.rows, a.cols)

    slope = slope_deg(dem, cell_m)
    rough = rough_from_dem(dem)
    r, c = dem.shape
    start = tuple(a.start) if a.start else (2, 2)
    end = tuple(a.end) if a.end else (r - 3, c - 3)

    routes = {}
    for name, k in (("safe", 1.0), ("fast", 0.2)):
        pts, sec = least_cost_route(cost_grid(slope, rough, k=k), start, end, cell_m)
        routes[name] = _route_block(pts, sec, cell_m, rough, DOSE_MSV_PER_HOUR)

    save_png(to_u8(slope), out / "preview.png")
    np.save(out / "slope.npy", slope)
    np.save(out / "rough.npy", rough)
    np.save(out / "cost_safe.npy", cost_grid(slope, rough, k=1.0))
    np.save(out / "cost_fast.npy", cost_grid(slope, rough, k=0.2))
    for name in routes:
        save_tile_bmp(to_u8(slope), "slope", out / "tiles" / f"{name}.bmp")

    doc = write_manifest(
        out / "routes.json",
        site="synth" if a.synth else Path(a.dem).stem,
        waypoints=[{"id": 1, "name": "START", "row": start[0], "col": start[1]},
                   {"id": 2, "name": "END", "row": end[0], "col": end[1]}],
        layers=[{"id": "slope", "label": "Slope", "tile": "slope"}],
        routes=routes,
        budget_s=int(a.budget_h * 3600),
        science_s=int(a.science_min * 60),
        cell_m=cell_m,
        grid={"rows": int(r), "cols": int(c)},
    )

    print(json.dumps({
        "ok": True,
        "cell_m": round(cell_m, 2),
        "grid": [int(r), int(c)],
        "safe_min": round(doc["routes"]["safe"]["time_s"] / 60, 1),
        "fast_min": round(doc["routes"]["fast"]["time_s"] / 60, 1),
    }, indent=2))


if __name__ == "__main__":
    main()
