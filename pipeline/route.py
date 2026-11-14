import numpy as np
from scipy.sparse import coo_matrix
from scipy.sparse.csgraph import dijkstra
from skimage.graph import route_through_array

from pipeline.cost import DOSE_MSV_PER_HOUR, MAX_SLOPE_DEG, seconds_per_meter

BLOCKED_PENALTY = 1e6   # s/m: intransitable pero no rompe el algoritmo


def least_cost_route(cost_s_per_m, start, end, cell_m):
    """
    Devuelve (lista de (row,col), segundos totales).
    `cost_s_per_m` en segundos por metro. Inf = intransitable.
    """
    arr = np.array(cost_s_per_m, dtype=np.float64, copy=True)
    blocked = ~np.isfinite(arr)
    arr[blocked] = BLOCKED_PENALTY
    path, cost = route_through_array(
        arr, tuple(start), tuple(end), fully_connected=True, geometric=True
    )
    if any(blocked[r, c] for r, c in path):
        raise ValueError("no existe ruta transitable")
    return [(int(r), int(c)) for r, c in path], float(cost) * float(cell_m)


def plan_routes(dem, slope, rough, cell_m, start, end, budget_s, science_s,
                science_stops, reserve_fraction):
    """Busca ambos tramos sobre el mismo modelo temporal dirigido y dos objetivos."""
    dem, slope, rough = (np.asarray(a, dtype=float) for a in (dem, slope, rough))
    if (dem.ndim != 2 or dem.shape != slope.shape or dem.shape != rough.shape
            or not all(np.isfinite(a).all() for a in (dem, slope, rough))
            or not np.isfinite(cell_m) or cell_m <= 0):
        raise ValueError("raster o tamaño de celda inválido")
    if (not np.isfinite(budget_s) or budget_s < 0 or not np.isfinite(science_s)
            or science_s < 0 or not isinstance(science_stops, int) or science_stops < 0
            or not np.isfinite(reserve_fraction) or not 0 <= reserve_fraction <= 1):
        raise ValueError("presupuesto, paradas o reserva inválidos")
    rows, cols = dem.shape
    for p in (start, end):
        if (len(p) != 2 or any(not isinstance(v, (int, np.integer)) for v in p)
                or not (0 <= p[0] < rows and 0 <= p[1] < cols)):
            raise ValueError("coordenadas de ruta inválidas")
        if slope[tuple(p)] > MAX_SLOPE_DEG:
            raise ValueError("extremo de ruta intransitable")

    ids = np.arange(rows * cols).reshape(rows, cols)
    source, target, times, risks = [], [], [], []
    for dr in (-1, 0, 1):
        for dc in (-1, 0, 1):
            if not (dr or dc):
                continue
            rs = slice(max(0, -dr), min(rows, rows - dr))
            cs = slice(max(0, -dc), min(cols, cols - dc))
            rt = slice(max(0, dr), min(rows, rows + dr))
            ct = slice(max(0, dc), min(cols, cols + dc))
            length = cell_m * np.hypot(dr, dc)
            grade = np.degrees(np.arctan((dem[rt, ct] - dem[rs, cs]) / length))
            valid = ((slope[rs, cs] <= MAX_SLOPE_DEG)
                     & (slope[rt, ct] <= MAX_SLOPE_DEG)
                     & (np.abs(grade) <= MAX_SLOPE_DEG))
            if dr and dc:
                valid &= (slope[rt, cs] <= MAX_SLOPE_DEG) & (slope[rs, ct] <= MAX_SLOPE_DEG)
            avg_rough = (rough[rs, cs] + rough[rt, ct]) / 2
            avg_slope = (slope[rs, cs] + slope[rt, ct]) / 2
            travel = seconds_per_meter(grade, avg_rough, k=0.2) * length
            source.append(ids[rs, cs][valid])
            target.append(ids[rt, ct][valid])
            times.append(travel[valid])
            risks.append((travel * (1 + avg_rough + (avg_slope / MAX_SLOPE_DEG) ** 2))[valid])
    source, target, times, risks = map(np.concatenate, (source, target, times, risks))
    endpoints = [int(ids[tuple(start)]), int(ids[tuple(end)])]
    result = {}
    for name, weights in (("fast", times), ("safe", risks)):
        graph = coo_matrix((weights, (source, target)), shape=(rows * cols,) * 2).tocsr()
        dist, pred = dijkstra(graph, directed=True, indices=endpoints, return_predecessors=True)
        legs = []
        for i, destination in ((0, endpoints[1]), (1, endpoints[0])):
            if not np.isfinite(dist[i, destination]):
                raise ValueError("no existe ruta transitable")
            nodes = [destination]
            while nodes[-1] != endpoints[i]:
                nodes.append(int(pred[i, nodes[-1]]))
            nodes.reverse()
            pts = np.array(np.unravel_index(nodes, dem.shape)).T
            delta = np.diff(pts, axis=0)
            lengths = np.hypot(delta[:, 0], delta[:, 1]) * cell_m
            rr, cc = pts[:, 0], pts[:, 1]
            grades = np.degrees(np.arctan(np.diff(dem[rr, cc]) / lengths))
            edge_rough = (rough[rr[:-1], cc[:-1]] + rough[rr[1:], cc[1:]]) / 2
            sec = float(np.sum(seconds_per_meter(grades, edge_rough, k=0.2) * lengths))
            avg_slope = (slope[rr[:-1], cc[:-1]] + slope[rr[1:], cc[1:]]) / 2
            distance = float(lengths.sum())
            legs.append({"points": pts.tolist(), "time_s": round(sec, 1),
                         "distance_m": round(distance, 1),
                         "slope_max_deg": round(float(slope[rr, cc].max()), 2),
                         "slope_exposure_m": round(float(lengths[avg_slope >= 15].sum()), 1),
                         "rough_mean": round(float(np.dot(edge_rough, lengths) / distance), 3) if distance else 0.0})
        outbound, back = legs
        duration = round(outbound["time_s"] + back["time_s"], 1)
        length = round(outbound["distance_m"] + back["distance_m"], 1)
        used = duration + science_stops * science_s
        reserve = budget_s * reserve_fraction
        result[name] = {"objective": "fastest" if name == "fast" else "conservative",
                        "points": outbound["points"], "outbound": outbound, "return": back,
                        "time_s": duration, "distance_m": length,
                        "slope_max_deg": max(outbound["slope_max_deg"], back["slope_max_deg"]),
                        "slope_exposure_m": round(outbound["slope_exposure_m"] + back["slope_exposure_m"], 1),
                        "rough_mean": round((outbound["rough_mean"] * outbound["distance_m"] + back["rough_mean"] * back["distance_m"]) / length, 3) if length else 0.0,
                        "dose_msv": round(duration / 3600 * DOSE_MSV_PER_HOUR, 4),
                        "budget": {"used_s": round(used, 1), "margin_s": round(budget_s - used, 1),
                                   "reserve_s": round(reserve, 1), "fits_reserve": used + reserve <= budget_s}}
    return result
