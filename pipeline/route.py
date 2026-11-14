import numpy as np
from skimage.graph import route_through_array

BLOCKED_PENALTY = 1e6   # s/m: intransitable pero no rompe el algoritmo


def least_cost_route(cost_s_per_m, start, end, cell_m):
    """
    Devuelve (lista de (row,col), segundos totales).
    `cost_s_per_m` en segundos por metro. Inf = intransitable.
    """
    arr = np.array(cost_s_per_m, dtype=np.float64, copy=True)
    arr[~np.isfinite(arr)] = BLOCKED_PENALTY
    path, cost = route_through_array(
        arr, tuple(start), tuple(end), fully_connected=True, geometric=True
    )
    return [(int(r), int(c)) for r, c in path], float(cost) * float(cell_m)
