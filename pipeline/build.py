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
        if tr.b != 0 or tr.d != 0 or tr.a <= 0 or tr.e >= 0:
            raise SystemExit("DEM requiere rejilla norte-arriba sin rotación")
        if geo:
            cy = (ds.bounds.top + ds.bounds.bottom) / 2.0
            cell_x = abs(tr.a) * MARS_KM_PER_DEG * 1000.0 * np.cos(np.radians(cy))
            cell_y = abs(tr.e) * MARS_KM_PER_DEG * 1000.0
        else:
            cell_x, cell_y = abs(tr.a), abs(tr.e)

        cell = float(np.mean([cell_x, cell_y]))
        f = max(1, int(round(target_cell_m / cell)))
        win_h, win_w = rows * f, cols * f
        if win_h > ds.height or win_w > ds.width:
            raise SystemExit("la ventana solicitada excede el DEM")

        if at:
            r0, c0 = int(at[0]), int(at[1])
        else:
            r0 = max(0, (ds.height - win_h) // 2)
            c0 = max(0, (ds.width - win_w) // 2)
        r0 = max(0, min(r0, max(0, ds.height - win_h)))
        c0 = max(0, min(c0, max(0, ds.width - win_w)))

        window = Window(c0, r0, win_w, win_h)
        bounds = list(ds.window_bounds(window))
        latitude = None
        if geo:
            latitude = (bounds[1] + bounds[3]) / 2
            cell_x = abs(tr.a) * MARS_KM_PER_DEG * 1000.0 * np.cos(np.radians(latitude))
            cell_y = abs(tr.e) * MARS_KM_PER_DEG * 1000.0
        elif ds.crs and ds.crs.to_authority() == ('ESRI', '103885'):
            # shortcut: escala constante en el centro; para áreas grandes usar escala por fila.
            latitude = np.degrees((bounds[1] + bounds[3]) / (2 * 3396190.0))
            cell_x = abs(tr.a) * np.cos(np.radians(latitude))
            cell_y = abs(tr.e)
        spatial = {"crs": str(ds.crs) if ds.crs else None, "bounds": bounds,
                   "source_window": {"row": r0, "col": c0, "height": win_h, "width": win_w},
                   "latitude_center_deg": float(latitude) if latitude is not None else None}
        z = ds.read(1, window=window, masked=True).astype(np.float32).filled(np.nan)

    # El centinela de "sin dato" es ~ -3.4e38 (no un umbral: en Marte las
    # elevaciones son negativas, p. ej. -2400 m en Jezero).
    z[~np.isfinite(z)] = np.nan
    if not np.isfinite(z).any():
        raise SystemExit("la ventana no tiene datos: prueba otro --at")

    z = z.reshape(z.shape[0] // f, f, z.shape[1] // f, f).mean(axis=(1, 3))
    if not np.isfinite(z).any():
        raise SystemExit("la ventana no tiene datos utilizables tras agregar bloques")
    return z.astype(np.float32), cell_y * f, cell_x * f, spatial


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
    ap.add_argument("--science-waypoint", type=int, nargs=2, action="append", default=[],
                    metavar=("ROW", "COL"), help="parada científica elegida; repetir en orden")
    ap.add_argument("--reserve-fraction", type=float, default=0.15)
    a = ap.parse_args()

    out = Path(a.out)
    if a.synth:
        dem, cell_m = hills(size=a.rows, cell_m=a.cell)
        cell_x_m, spatial = cell_m, {}
    else:
        if not a.dem:
            raise SystemExit("falta --dem o --synth")
        dem, cell_m, cell_x_m, spatial = _load_dem(a.dem, a.cell, a.rows, a.cols, a.at)

    # Las métricas dependen de vecinos: excluir también celdas afectadas por NoData.
    from scipy.ndimage import maximum_filter
    invalid = ~np.isfinite(dem)
    filled = np.where(invalid, np.nanmedian(dem), dem) if invalid.any() else dem
    affected = maximum_filter(invalid, size=3)
    slope = slope_deg(filled, cell_m, cell_x_m)
    rough = rough_from_dem(filled, valid=~affected)
    slope[affected] = np.nan
    r, c = dem.shape
    start = tuple(a.start) if a.start else (2, 2)
    end = tuple(a.end) if a.end else (r - 3, c - 3)

    routes = plan_routes(dem, slope, rough, cell_m, start, end,
                         a.budget_h * 3600, a.science_min * 60, a.science_waypoint, a.reserve_fraction,
                         cell_x_m=cell_x_m)

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
        science_waypoints=a.science_waypoint,
        reserve_fraction=a.reserve_fraction,
        cell_m=cell_m,
        cell_x_m=cell_x_m,
        spatial=spatial,
        grid={"rows": int(r), "cols": int(c)},
        dem_validity="nodata-preserved-v1",
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
