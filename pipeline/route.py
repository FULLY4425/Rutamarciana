import numpy as np
from scipy.sparse import coo_matrix
from scipy.sparse.csgraph import dijkstra
from skimage.graph import route_through_array

from pipeline.cost import DOSE_MSV_PER_HOUR, MAX_SLOPE_DEG, seconds_per_meter, tobler_kmh, SUIT_FACTOR
from pipeline.scenario import scenario, budget

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
                science_waypoints, reserve_fraction, cell_x_m=None, scenario_assumptions=None):
    """Busca rutas dirigidas; reserve_fraction legado se ignora, no determina la reserva."""
    dem, slope, rough = (np.asarray(a, dtype=float) for a in (dem, slope, rough))
    cell_x_m = cell_m if cell_x_m is None else cell_x_m
    if (dem.ndim != 2 or dem.shape != slope.shape or dem.shape != rough.shape
            or not np.isfinite(cell_m) or cell_m <= 0
            or not np.isfinite(cell_x_m) or cell_x_m <= 0):
        raise ValueError("raster o tamaño de celda inválido")
    if (not np.isfinite(budget_s) or budget_s < 0 or not np.isfinite(science_s)
            or science_s < 0 or not isinstance(science_waypoints, (list, tuple))
            or not np.isfinite(reserve_fraction) or not 0 <= reserve_fraction <= 1):
        raise ValueError("presupuesto, waypoints o reserva inválidos")
    values = scenario({'suit_budget_s': budget_s, 'science_stop_s': science_s,
                       **(scenario_assumptions or {})})
    valid_cells = np.isfinite(dem) & np.isfinite(slope) & np.isfinite(rough) & (slope <= MAX_SLOPE_DEG)
    dem, slope, rough = (np.where(valid_cells, a, 0.0) for a in (dem, slope, rough))
    rows, cols = dem.shape
    stops = [start, *science_waypoints, end]
    for p in stops:
        if (not isinstance(p, (list, tuple)) or len(p) != 2
                or any(isinstance(v, (bool, np.bool_)) or not isinstance(v, (int, np.integer)) for v in p)
                or not (0 <= p[0] < rows and 0 <= p[1] < cols)):
            raise ValueError("coordenadas de ruta inválidas")
        if not valid_cells[tuple(p)]:
            raise ValueError("waypoint de ruta intransitable")
    if len({tuple(p) for p in stops}) != len(stops):
        raise ValueError("coordenadas duplicadas")

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
            length = np.hypot(dr * cell_m, dc * cell_x_m)
            grade = np.degrees(np.arctan((dem[rt, ct] - dem[rs, cs]) / length))
            valid = (valid_cells[rs, cs]
                     & valid_cells[rt, ct]
                     & (np.abs(grade) <= MAX_SLOPE_DEG))
            if dr and dc:
                valid &= valid_cells[rt, cs] & valid_cells[rs, ct]
            grade = np.where(valid, grade, 0.0)
            avg_rough = (rough[rs, cs] + rough[rt, ct]) / 2
            avg_slope = (slope[rs, cs] + slope[rt, ct]) / 2
            travel = seconds_per_meter(grade, avg_rough, k=0.2) * length
            source.append(ids[rs, cs][valid])
            target.append(ids[rt, ct][valid])
            times.append(travel[valid])
            risks.append((travel * (1 + avg_rough + (avg_slope / MAX_SLOPE_DEG) ** 2))[valid])
    source, target, times, risks = map(np.concatenate, (source, target, times, risks))
    endpoints = [int(ids[tuple(point)]) for point in stops]
    segments = list(zip(endpoints, endpoints[1:])) + [(endpoints[-1], endpoints[0])]
    # Reverse directed travel graph: one search gives fastest walkback from any cell.
    return_graph = coo_matrix((times, (target, source)), shape=(rows * cols,) * 2).tocsr()
    return_dist, return_pred = dijkstra(return_graph, directed=True, indices=endpoints[0],
                                        return_predecessors=True)
    result = {}
    for name, weights in (("fast", times), ("safe", risks)):
        graph = coo_matrix((weights, (source, target)), shape=(rows * cols,) * 2).tocsr()
        legs = []
        for segment_index, (origin, destination) in enumerate(segments):
            dist, pred = dijkstra(graph, directed=True, indices=origin, return_predecessors=True)
            if not np.isfinite(dist[destination]):
                raise ValueError(f"no existe ruta transitable en tramo {segment_index + 1} ({name})")
            nodes = [destination]
            while nodes[-1] != origin:
                nodes.append(int(pred[nodes[-1]]))
            nodes.reverse()
            pts = np.array(np.unravel_index(nodes, dem.shape)).T
            delta = np.diff(pts, axis=0)
            lengths = np.hypot(delta[:, 0] * cell_m, delta[:, 1] * cell_x_m)
            rr, cc = pts[:, 0], pts[:, 1]
            grades = np.degrees(np.arctan(np.diff(dem[rr, cc]) / lengths))
            edge_rough = (rough[rr[:-1], cc[:-1]] + rough[rr[1:], cc[1:]]) / 2
            speed = values['v_return_flat_kmh'] if segment_index == len(segments) - 1 else values['v_nom_flat_kmh']
            sec = float(np.sum(seconds_per_meter(grades, edge_rough, k=0.2) * lengths)) * (tobler_kmh(0) * SUIT_FACTOR / speed)
            avg_slope = (slope[rr[:-1], cc[:-1]] + slope[rr[1:], cc[1:]]) / 2
            distance = float(lengths.sum())
            edge_slope = np.maximum(avg_slope, np.abs(grades))
            legs.append({"points": pts.tolist(), "time_s": sec,
                         "distance_m": distance,
                         "slope_max_deg": max(float(slope[rr, cc].max()),
                                              float(np.abs(grades).max()) if grades.size else 0.0),
                         "slope_exposure_m": float(lengths[edge_slope >= 15].sum()),
                         "rough_mean": float(np.dot(edge_rough, lengths) / distance) if distance else 0.0})
        outbound_legs, back = legs[:-1], legs[-1]
        distance_out = sum(leg["distance_m"] for leg in outbound_legs)
        outbound = {"points": outbound_legs[0]["points"] + [p for leg in outbound_legs[1:] for p in leg["points"][1:]],
                    "time_s": sum(leg["time_s"] for leg in outbound_legs),
                    "distance_m": distance_out,
                    "slope_max_deg": max(leg["slope_max_deg"] for leg in outbound_legs),
                    "slope_exposure_m": sum(leg["slope_exposure_m"] for leg in outbound_legs),
                    "rough_mean": sum(leg["rough_mean"] * leg["distance_m"] for leg in outbound_legs) / distance_out if distance_out else 0.0,
                    "legs": outbound_legs}
        duration = outbound["time_s"] + back["time_s"]
        length = outbound["distance_m"] + back["distance_m"]
        science_total = len(science_waypoints) * values['science_stop_s']
        all_points = np.asarray(outbound['points'] + back['points'])
        radial = np.hypot((all_points[:, 0] - start[0]) * cell_m,
                          (all_points[:, 1] - start[1]) * cell_x_m)
        worst = int(np.argmax(radial))
        eva_budget = budget(values, len(science_waypoints), outbound['time_s'], back['time_s'],
                            float(radial[worst]), all_points[worst].tolist())
        contingency_points = list(science_waypoints) or [all_points[worst].tolist()]
        waypoint_returns = []
        for point in contingency_points:
            node = int(ids[tuple(point)])
            if not np.isfinite(return_dist[node]):
                raise ValueError('retorno contingente intransitable')
            nodes = [node]
            while nodes[-1] != endpoints[0]:
                nodes.append(int(return_pred[nodes[-1]]))
            pts = np.array(np.unravel_index(nodes, dem.shape)).T
            delta = np.diff(pts, axis=0)
            distance = float(np.hypot(delta[:, 0] * cell_m, delta[:, 1] * cell_x_m).sum())
            seconds = float(return_dist[node]) * (tobler_kmh(0) * SUIT_FACTOR / values['v_return_flat_kmh'])
            waypoint_returns.append({'waypoint': list(point), 'points': pts.tolist(),
                                     'distance_m': distance, 'time_s': seconds,
                                     'distance_status': 'exceeds_limit' if distance > values['max_return_distance_m'] else 'within_limit',
                                     'time_status': 'exceeds_limit' if seconds > values['max_return_s'] else 'within_limit'})
        eva_budget['return_contingency'].update({
            'path_method': 'fastest_directed_terrain_path', 'waypoint_returns': waypoint_returns,
            'path_note': 'Fastest modeled return from each science waypoint, or the farthest radial route point if there are no science waypoints. Limits apply to this path only; exceeding one does not prove no alternative path can satisfy both. Not added to planned return or fixed reserve; terrain model is not field validation.'})
        result[name] = {"objective": "fastest" if name == "fast" else "conservative",
                        "points": outbound["points"], "outbound": outbound, "return": back,
                        "time_s": round(duration, 1), "distance_m": round(length, 1),
                        "science_total_s": science_total, "total_s": round(eva_budget['operational_s'], 1),
                        "slope_max_deg": round(max(outbound["slope_max_deg"], back["slope_max_deg"]), 2),
                        "slope_exposure_m": round(outbound["slope_exposure_m"] + back["slope_exposure_m"], 1),
                        "rough_mean": round((outbound["rough_mean"] * outbound["distance_m"] + back["rough_mean"] * back["distance_m"]) / length, 3) if length else 0.0,
                        "dose_msv": round(duration / 3600 * DOSE_MSV_PER_HOUR, 4),
                        "budget": eva_budget}
        for leg in [*legs, outbound]:
            for key, digits in (("time_s", 1), ("distance_m", 1), ("slope_max_deg", 2),
                                ("slope_exposure_m", 1), ("rough_mean", 3)):
                leg[key] = round(leg[key], digits)
    return result
