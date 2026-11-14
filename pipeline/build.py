import argparse
import json
from pathlib import Path

import numpy as np

from pipeline.cost import cost_grid
from pipeline.manifest import write_manifest
from pipeline.route import plan_routes
from pipeline.synth import hills
from pipeline.terrain import slope_deg
from pipeline.roughness import rough_from_dem
from pipeline.tiles import save_png, save_tile_bmp, to_u8

MARS_KM_PER_DEG = 59.27


def _load_dem(path, target_cell_m, rows, cols, at=None):
    """Lee una ventana del GeoTIFF y la promedia a bloques de `target_cell_m` (metros)."""
    import rasterio
    from rasterio.windows import Window

    with rasterio.open(path) as ds:
        tr = ds.transform
        geo = bool(ds.crs and ds.crs.is_geographic)
        if geo:
            cy = (ds.bounds.top + ds.bounds.bottom) / 2.0
            cell_x = abs(tr.a) * MARS_KM_PER_DEG * 1000.0 * np.cos(np.radians(cy))
            cell_y = abs(tr.e) * MARS_KM_PER_DEG * 1000.0
        else:
            cell_x, cell_y = abs(tr.a), abs(tr.e)

        cell = float(np.mean([cell_x, cell_y]))
        f = max(1, int(round(target_cell_m / cell)))
        win_h, win_w = rows * f, cols * f

        if at:
            r0, c0 = int(at[0]), int(at[1])
        else:
            r0 = max(0, (ds.height - win_h) // 2)
            c0 = max(0, (ds.width - win_w) // 2)
        r0 = max(0, min(r0, max(0, ds.height - win_h)))
        c0 = max(0, min(c0, max(0, ds.width - win_w)))

        z = ds.read(1, window=Window(c0, r0, win_w, win_h)).astype(np.float32)
        nod = ds.nodata

    # El centinela de "sin dato" es ~ -3.4e38 (no un umbral: en Marte las
    # elevaciones son negativas, p. ej. -2400 m en Jezero).
    if nod is not None:
        z[z == np.float32(nod)] = np.nan
    z[~np.isfinite(z)] = np.nan
    if np.isfinite(z).any():
        z = np.nan_to_num(z, nan=float(np.nanmedian(z)))
    else:
        raise SystemExit("la ventana no tiene datos: prueba otro --at")

    z = z.reshape(z.shape[0] // f, f, z.shape[1] // f, f).mean(axis=(1, 3))
    return z.astype(np.float32), cell * f


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
    ap.add_argument("--at", type=int, nargs=2, help="fila,col de la esquina superior izquierda (px del DEM)")
    ap.add_argument("--budget-h", type=float, default=4.0)
    ap.add_argument("--science-min", type=float, default=10.0)
    ap.add_argument("--stops", type=int, default=5)
    ap.add_argument("--reserve-fraction", type=float, default=0.15)
    a = ap.parse_args()

    out = Path(a.out)
    if a.synth:
        dem, cell_m = hills(size=a.rows, cell_m=a.cell)
    else:
        if not a.dem:
            raise SystemExit("falta --dem o --synth")
        dem, cell_m = _load_dem(a.dem, a.cell, a.rows, a.cols, a.at)

    slope = slope_deg(dem, cell_m)
    rough = rough_from_dem(dem)
    r, c = dem.shape
    start = tuple(a.start) if a.start else (2, 2)
    end = tuple(a.end) if a.end else (r - 3, c - 3)

    routes = plan_routes(dem, slope, rough, cell_m, start, end,
                         a.budget_h * 3600, a.science_min * 60, a.stops, a.reserve_fraction)

    save_png(to_u8(slope), out / "preview.png")
    np.save(out / "dem.npy", dem)
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
        budget_s=a.budget_h * 3600,
        science_s=a.science_min * 60,
        science_stops=a.stops,
        reserve_fraction=a.reserve_fraction,
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
