import numpy as np
import pytest

from pipeline.route import plan_routes
from pipeline.synth import ramp
from pipeline.terrain import slope_deg


def test_round_trip_accounts_for_direction_and_stops():
    dem, cell = ramp(slope=0.15, size=7, cell_m=4)
    routes = plan_routes(dem, slope_deg(dem, cell), np.zeros_like(dem), cell,
                         (3, 1), (3, 5), 14400, 600, 5, 0.15)
    route = routes['fast']
    assert route['outbound']['points'][0] == [3, 1]
    assert route['return']['points'][-1] == [3, 1]
    assert route['outbound']['time_s'] > route['return']['time_s']
    assert route['time_s'] == pytest.approx(route['outbound']['time_s'] + route['return']['time_s'], abs=0.11)
    assert route['budget']['used_s'] == pytest.approx(route['time_s'] + 5 * 600, abs=0.11)
    assert route['budget']['reserve_s'] == 2160
    assert route['distance_m'] == pytest.approx(32)


def test_fastest_and_conservative_optimize_different_costs():
    dem = np.zeros((9, 13))
    slope = np.zeros_like(dem)
    rough = np.zeros_like(dem)
    rough[4, 2:11] = 0.3
    routes = plan_routes(dem, slope, rough, 4, (4, 1), (4, 11), 14400, 0, 0, .15)
    assert routes['fast']['time_s'] < routes['safe']['time_s']
    assert routes['fast']['rough_mean'] > routes['safe']['rough_mean']


def test_blocked_barrier_is_never_traversed():
    dem = np.zeros((5, 5))
    slope = np.zeros_like(dem)
    slope[:, 2] = 26
    with pytest.raises(ValueError, match='ruta'):
        plan_routes(dem, slope, np.zeros_like(dem), 4, (2, 0), (2, 4), 14400, 0, 0, .15)


def test_flat_dem_has_no_artificial_roughness():
    from pipeline.roughness import rough_from_dem

    assert np.all(rough_from_dem(np.zeros((7, 7))) == 0)


def test_edge_gradient_blocks_steep_transition_even_when_cell_gradient_is_low():
    dem = np.zeros((3, 3))
    dem[:, 2] = 4
    with pytest.raises(ValueError, match='ruta'):
        plan_routes(dem, np.zeros_like(dem), np.zeros_like(dem), 4,
                    (1, 0), (1, 2), 60, 0, 0, 0)


def test_same_endpoint_and_budget_boundary():
    dem = np.zeros((3, 3))
    route = plan_routes(dem, dem, dem, 4, (1, 1), (1, 1), 100, 20, 4, .2)['fast']
    assert route['time_s'] == 0
    assert route['distance_m'] == 0
    assert route['budget']['fits_reserve'] is True


def test_cli_manifest_matches_recomputed_api(tmp_path):
    import json
    import subprocess
    import sys
    from relay.app import create_app

    subprocess.run([sys.executable, '-m', 'pipeline.build', '--synth', '--rows', '30',
                    '--cols', '30', '--stops', '2', '--reserve-fraction', '.25',
                    '--out', str(tmp_path)], check=True, capture_output=True, text=True)
    doc = json.loads((tmp_path / 'routes.json').read_text())
    client = create_app(str(tmp_path)).test_client()
    assert client.get('/api/routes').get_json() == doc
    response = client.post('/api/route', json={'start': [2, 2], 'end': [27, 27]})
    assert response.status_code == 200
    assert response.get_json()['fast'] == doc['routes']['fast']
    assert doc['science_stops'] == 2
    assert doc['reserve_fraction'] == .25
