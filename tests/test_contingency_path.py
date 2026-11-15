import numpy as np
import pytest
from pipeline.route import plan_routes


def test_contingency_routes_from_each_science_waypoint():
    dem = np.zeros((5, 5))
    slope = dem.copy()
    slope[2, 1:4] = 26
    result = plan_routes(dem, slope, dem, 500, (3, 0), (1, 4), 14400, 1200,
                         [(1, 3)], 0)['fast']['budget']['return_contingency']
    check = result['waypoint_returns'][0]
    assert check['waypoint'] == [1, 3]
    assert check['points'][0] == [1, 3] and check['points'][-1] == [3, 0]
    assert check['distance_m'] > np.hypot(1500, 1000)
    assert check['time_s'] == pytest.approx(check['distance_m'] * 1.8)
    assert check['time_status'] == 'exceeds_limit'
    assert check['distance_status'] == 'exceeds_limit'


def test_contingency_uses_direction_and_return_speed_for_each_stop():
    dem = np.tile(np.arange(5) * 5.0, (3, 1))
    slope = np.full_like(dem, np.degrees(np.arctan(.05)))
    kwargs = {'scenario_assumptions': {'v_return_flat_kmh': 1.0}}
    route = plan_routes(dem, slope, np.zeros_like(dem), 100, (0, 0), (0, 4),
                        14400, 1200, [(0, 2), (0, 3)], 0, **kwargs)['fast']
    checks = route['budget']['return_contingency']['waypoint_returns']
    assert [c['waypoint'] for c in checks] == [[0, 2], [0, 3]]
    # The first outbound leg is uphill at 2 km/h; return is downhill at 1 km/h.
    expected = plan_routes(dem, slope, np.zeros_like(dem), 100, (0, 0), (0, 2),
                           14400, 1200, [], 0, **kwargs)['fast']['return']['time_s']
    assert checks[0]['time_s'] == pytest.approx(expected, abs=.051)
    assert checks[0]['time_status'] == 'within_limit'
