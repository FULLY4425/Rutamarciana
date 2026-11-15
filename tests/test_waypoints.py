import numpy as np
import pytest

from pipeline.route import plan_routes


def test_ordered_waypoints_and_direct_return_for_both_profiles():
    dem = np.tile(np.arange(7) * .6, (7, 1))
    slope = np.full_like(dem, np.degrees(np.arctan(.15)))
    result = plan_routes(dem, slope, np.zeros_like(dem), 4, (3, 0), (3, 6),
                         10000, 100, [(1, 2), (5, 4)], .15)
    for route in result.values():
        legs = route['outbound']['legs']
        assert [leg['points'][-1] for leg in legs] == [[1, 2], [5, 4], [3, 6]]
        assert route['return']['points'][0] == [3, 6]
        assert route['return']['points'][-1] == [3, 0]
        assert route['science_total_s'] == 200
        assert route['budget']['used_s'] == pytest.approx(route['time_s'] + 200 + 1800, abs=.11)
        assert route['distance_m'] == pytest.approx(sum(leg['distance_m'] for leg in legs) + route['return']['distance_m'], abs=.21)


def test_return_is_independently_directed_for_both_profiles():
    dem = np.tile(np.arange(7) * .6, (7, 1))
    slope = np.full_like(dem, np.degrees(np.arctan(.15)))
    routes = plan_routes(dem, slope, np.zeros_like(dem), 4, (3, 0), (3, 6),
                         10000, 50, [], .15)
    for route in routes.values():
        assert route['outbound']['time_s'] > route['return']['time_s']
        assert route['return']['points'][0] == [3, 6]
        assert route['return']['points'][-1] == [3, 0]
        assert route['science_total_s'] == 0
