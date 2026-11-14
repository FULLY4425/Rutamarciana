import numpy as np

from pipeline.cost import cost_grid
from pipeline.route import least_cost_route
from pipeline.synth import hills
from pipeline.terrain import slope_deg


def test_straight_flat_route_time():
    cost = np.full((10, 10), 1.7867)
    pts, seconds = least_cost_route(cost, (0, 0), (0, 9), cell_m=4.0)
    assert pts[0] == (0, 0) and pts[-1] == (0, 9)
    assert abs(seconds - 9 * 4 * 1.7867) < 0.5


def test_safe_route_avoids_steep_step():
    dem, cell_m = hills()
    s = slope_deg(dem, cell_m)
    rough = np.zeros_like(s)
    fast = least_cost_route(cost_grid(s, rough, k=0.2), (2, 2), (37, 37), cell_m)
    safe = least_cost_route(cost_grid(s, rough, k=1.0), (2, 2), (37, 37), cell_m)
    assert safe[1] >= fast[1]


def test_route_never_crosses_steep_cell():
    dem, cell_m = hills()
    s = slope_deg(dem, cell_m)
    rough = np.zeros_like(s)
    pts, _ = least_cost_route(cost_grid(s, rough, k=1.0), (2, 2), (37, 37), cell_m)
    crossed = [s[r, c] for r, c in pts[1:-1]]
    assert max(crossed) <= 25.0 + 1e-3
