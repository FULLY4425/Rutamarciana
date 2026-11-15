import numpy as np
import pytest

from pipeline.route import plan_routes


def test_manifest_site_does_not_claim_synthetic_or_unknown_is_jezero(tmp_path):
    from pipeline.manifest import write_manifest
    for site, expected in [('synth', 'synthetic'), ('other_dem', 'unknown'), ('jez_dtm_1m', 'Jezero/Mars')]:
        doc = write_manifest(tmp_path / 'routes.json', site=site, waypoints=[], layers=[], routes={},
                             budget_s=14400, science_s=1200)
        assert doc['scenario']['site'] == expected


def test_scenario_counts_stops_and_exposes_budget():
    dem = np.zeros((5, 5))
    result = plan_routes(dem, dem, dem, 4, (0, 0), (4, 4), 14400, 1200,
                         [(1, 1), (2, 2)], .15)
    for route in result.values():
        budget = route['budget']
        assert budget['science_stops'] == 2
        assert budget['science_total_s'] == 2400
        assert budget['overhead_s'] == 1800
        assert budget['reserve_s'] == 1800
        assert budget['status'] == 'cabe'
        assert budget['return_contingency']['method'] == 'radial_distance_and_flat_time_proxy'


@pytest.mark.parametrize('count', [0, 1, 3])
def test_stop_count_and_duration(count):
    from pipeline.scenario import scenario, budget
    result = budget(scenario({'science_stop_s': 601}), count, 10, 20, 0, [0, 0])
    assert result['science_stops'] == count
    assert result['science_total_s'] == count * 601
    assert result['operational_s'] == 1800 + 30 + count * 601


@pytest.mark.parametrize(('operational', 'expected'), [
    (60.0, 'cabe'), (60.000001, 'justo'), (100.0, 'justo'),
    (100.000001, 'no_cabe'),
])
def test_unrounded_boundaries(operational, expected):
    from pipeline.scenario import scenario, budget
    values = scenario({'suit_budget_s': 100, 'reserve_fixed_s': 40,
                       'egress_s': 0, 'ingress_s': 0})
    result = budget(values, 0, operational, 0, 0, [0, 0])
    assert result['status'] == expected
    assert result['slack_s'] == pytest.approx(100 - operational - 40)


def test_radial_distance_is_not_path_and_does_not_certify_return():
    dem = np.zeros((5, 5))
    slope = dem.copy()
    slope[2, 1:4] = 26
    result = plan_routes(dem, slope, dem, 500, (3, 0), (1, 4), 14400, 1200, [], 0)
    for route in result.values():
        check = route['budget']['return_contingency']
        assert check['radial_distance_m'] == pytest.approx(np.hypot(2000, 1000))
        assert check['status'] == 'exceeds_radial_limit'
        assert check['time_status'] == 'not_verified'
        assert route['outbound']['distance_m'] > check['radial_distance_m']
        assert 'not a traversable route' in check['note']
    assert plan_routes(dem, dem, dem, 4, (0, 0), (1, 1), 14400, 1200, [], 0)['fast']['budget']['return_contingency']['status'] == 'not_verified'


def test_scenario_validation():
    from pipeline.scenario import scenario, metadata
    assert 'not a Mars suit capacity' in metadata(scenario())['note']
    for override in ({'bad': 1}, {'v_nom_flat_kmh': 0}, {'egress_s': -1},
                     {'science_stop_s': float('nan')}, {'reserve_fixed_s': True}):
        with pytest.raises(ValueError):
            scenario(override)


def test_overrides_change_speeds_and_budget():
    dem = np.zeros((3, 3))
    route = plan_routes(dem, dem, dem, 100, (1, 0), (1, 2), 14400, 1200, [(1, 1)], 0,
                        scenario_assumptions={'v_nom_flat_kmh': 1, 'v_return_flat_kmh': 2,
                                              'science_stop_s': 600, 'egress_s': 1800,
                                              'ingress_s': 1800, 'reserve_fixed_s': 3600})['fast']
    assert route['outbound']['time_s'] == pytest.approx(720)
    assert route['return']['time_s'] == pytest.approx(360)
    assert route['science_total_s'] == 600
    assert route['budget']['overhead_s'] == 3600
    assert route['budget']['reserve_s'] == 3600


def test_downhill_radial_time_proxy_is_not_return_time_failure():
    dem = np.tile(np.arange(43) * 2.5, (2, 1))
    slope = np.full_like(dem, np.degrees(np.arctan(.05)))
    route = plan_routes(dem, slope, np.zeros_like(dem), 50, (0, 0), (0, 42),
                        14400, 1200, [], 0,
                        scenario_assumptions={'max_return_distance_m': 3000})['fast']
    check = route['budget']['return_contingency']
    assert route['return']['time_s'] < 3600 < check['flat_radial_time_s']
    assert check['distance_status'] == 'not_verified'
    assert check['time_proxy_status'] == 'exceeds_proxy_limit'
    assert check['time_status'] == 'not_verified'
    assert check['status'] == 'not_verified'
