"""Contrato del presupuesto EVA: conteo de paradas, overhead, reserva exacta,
fronteras de decisión sin redondeo, retorno contingente por camino vs radial y
overrides de escenario.

Todas las aserciones son sobre el modelo declarado (LUNAR-ANALOG-2026), no sobre
capacidad real de traje ni transitabilidad certificada.
"""
import hashlib
import json

import numpy as np
import pytest

from pipeline.route import plan_routes
from pipeline.scenario import DEFAULTS, budget, metadata, scenario


def _route(stops, stop_s=601, cell=100, budget_s=14400, **assumptions):
    dem = np.zeros((5, 5))
    return plan_routes(dem, dem, dem, cell, (2, 0), (2, 4), budget_s, stop_s, stops, 0,
                       scenario_assumptions=assumptions)['fast']


# --- N = 0, 1 y varios stops: conteo, duración y términos -------------------

@pytest.mark.parametrize('stops', [[], [(2, 2)], [(2, 1), (2, 2), (2, 3)]])
def test_planner_counts_stops_and_charges_duration(stops):
    route = _route(stops)
    b = route['budget']
    assert b['science_stops'] == len(stops)
    assert b['science_total_s'] == 601 * len(stops)
    assert b['overhead_s'] == 1800
    assert b['other_tasks_s'] == 0
    assert b['operational_s'] == pytest.approx(
        b['overhead_s'] + b['outbound_travel_s'] + b['science_total_s']
        + b['other_tasks_s'] + b['return_travel_s'], abs=1e-6)
    assert route['science_total_s'] == 601 * len(stops)
    assert route['total_s'] == pytest.approx(b['operational_s'], abs=0.05)
    assert route['time_s'] == pytest.approx(
        route['outbound']['time_s'] + route['return']['time_s'], abs=0.11)
    assert len(b['return_contingency']['waypoint_returns']) == max(len(stops), 1)


def test_each_stop_adds_exactly_its_duration_when_travel_is_unchanged():
    # Waypoints colineales sobre terreno plano: el viaje no cambia, solo la ciencia.
    assert _route([(2, 2)])['budget']['operational_s'] - _route([])['budget']['operational_s'] \
        == pytest.approx(601, abs=1e-9)
    assert _route([(2, 1), (2, 2), (2, 3)])['budget']['operational_s'] \
        - _route([])['budget']['operational_s'] == pytest.approx(1803, abs=1e-9)


def test_stop_duration_override_scales_science_term_only():
    base = _route([(2, 2)], stop_s=1200)['budget']
    short = _route([(2, 2)], stop_s=0)['budget']
    assert short['science_total_s'] == 0
    assert short['science_stops'] == 1
    assert base['operational_s'] - short['operational_s'] == pytest.approx(1200, abs=1e-9)


# --- Overhead ---------------------------------------------------------------

def test_overhead_is_egress_plus_ingress_not_other_tasks():
    base = plan_routes(np.zeros((4, 4)), np.zeros((4, 4)), np.zeros((4, 4)), 100,
                       (1, 0), (1, 3), 14400, 600, [], 0)['fast']['budget']
    assert base['overhead_s'] == 1800
    over = plan_routes(np.zeros((4, 4)), np.zeros((4, 4)), np.zeros((4, 4)), 100,
                       (1, 0), (1, 3), 14400, 600, [], 0,
                       scenario_assumptions={'egress_s': 1200, 'ingress_s': 300,
                                             'other_tasks_s': 240})['fast']['budget']
    assert over['overhead_s'] == 1500
    assert over['other_tasks_s'] == 240
    assert over['operational_s'] - base['operational_s'] == pytest.approx(1500 - 1800 + 240, abs=1e-6)


# --- Reserva exacta y fronteras cabe / justo / no_cabe ----------------------

@pytest.mark.parametrize(('operational', 'reserve', 'budget_s', 'expected', 'slack'), [
    (60.0, 40.0, 100.0, 'cabe', 0.0),          # reserva exacta
    (59.96, 40.0, 100.0, 'cabe', 0.04),
    (60.04, 40.0, 100.0, 'justo', -0.04),      # 0.04 s por encima de la reserva exacta
    (100.0, 0.0, 100.0, 'cabe', 0.0),          # presupuesto exacto sin reserva
    (100.0, 1.0, 100.0, 'justo', -1.0),        # entra en presupuesto, no en reserva
    (100.000001, 0.0, 100.0, 'no_cabe', -1e-6),
])
def test_decision_boundaries_use_unrounded_seconds(operational, reserve, budget_s, expected, slack):
    values = scenario({'suit_budget_s': budget_s, 'reserve_fixed_s': reserve,
                       'egress_s': 0, 'ingress_s': 0})
    b = budget(values, 0, operational, 0.0, 0.0, [0, 0])
    assert b['status'] == expected
    assert b['slack_s'] == pytest.approx(slack, abs=1e-9)
    assert b['reserve_s'] == reserve
    assert b['fits_reserve'] is (expected == 'cabe')
    assert b['margin_s'] == pytest.approx(budget_s - operational, abs=1e-9)


def test_boundary_is_not_decided_on_rounded_seconds():
    values = scenario({'suit_budget_s': 100.0, 'reserve_fixed_s': 40.0,
                       'egress_s': 0, 'ingress_s': 0})
    assert round(60.04, 1) + 40.0 == 100.0  # redondear a 0.1 s diría 'cabe'
    assert budget(values, 0, 60.04, 0.0, 0.0, [0, 0])['status'] == 'justo'
    values = scenario({'suit_budget_s': 100.0, 'reserve_fixed_s': 0.0,
                       'egress_s': 0, 'ingress_s': 0})
    assert round(100.000001, 1) == 100.0    # redondear diría 'cabe'
    assert budget(values, 0, 100.000001, 0.0, 0.0, [0, 0])['status'] == 'no_cabe'


def test_route_decision_flips_although_reported_total_is_identical():
    dem = np.tile(np.arange(5) * 1.7, (5, 1))
    from pipeline.terrain import slope_deg
    slope = slope_deg(dem, 100)

    def run(budget_s):
        return plan_routes(dem, slope, np.zeros_like(dem), 100, (0, 0), (4, 4),
                           budget_s, 600, [(2, 2)], 0)['fast']

    ref = run(14400)['budget']
    operational, reserve = float(ref['operational_s']), ref['reserve_s']
    fits, over = run(operational + reserve), run(operational + reserve - 0.04)
    assert fits['budget']['status'] == 'cabe'
    assert fits['budget']['slack_s'] >= 0 and abs(float(fits['budget']['slack_s'])) < 1e-6
    assert over['budget']['status'] == 'justo'
    assert float(over['budget']['slack_s']) < 0
    assert fits['budget']['reserve_s'] == over['budget']['reserve_s'] == reserve
    assert fits['total_s'] == over['total_s']  # mismo total reportado, decisión distinta


# --- Retorno contingente: camino dirigido vs radial -------------------------

def _walled_grid(rows=5, cols=9):
    dem = np.zeros((rows, cols))
    slope = np.zeros((rows, cols))
    slope[:, 4] = 26.0   # muro intransitable
    slope[0, 4] = 0.0    # único paso, fila 0
    return dem, slope, np.zeros((rows, cols))


def test_contingency_measures_terrain_path_not_straight_line():
    dem, slope, rough = _walled_grid()
    route = plan_routes(dem, slope, rough, 10, (2, 0), (2, 8), 14400, 600, [(2, 6)], 0)['fast']
    check = route['budget']['return_contingency']
    wp = check['waypoint_returns'][0]
    assert wp['waypoint'] == [2, 6]
    assert wp['distance_m'] > 60.0 + 15.0        # el muro obliga a rodear
    assert wp['points'][0] == [2, 6] and wp['points'][-1] == [2, 0]
    assert any(point[0] == 0 for point in wp['points'])   # cruza por el paso
    assert wp['time_s'] == pytest.approx(wp['distance_m'] * 1.8, rel=1e-3)
    # El proxy radial sigue identificado por separado y apunta a otro punto (el final).
    assert 'radial' in check['method'] and check['method'] != check['path_method']
    assert check['path_method'] == 'fastest_directed_terrain_path'
    assert check['radial_distance_m'] == pytest.approx(80.0)
    assert wp['distance_m'] != pytest.approx(check['radial_distance_m'])
    assert check['radial_distance_m'] <= wp['distance_m'] + 20.0


def test_contingency_without_stops_uses_farthest_route_point():
    dem, slope, rough = _walled_grid()
    route = plan_routes(dem, slope, rough, 10, (4, 0), (0, 2), 14400, 0, [], 0)['fast']
    check = route['budget']['return_contingency']
    points = route['outbound']['points'] + route['return']['points']
    radial = [float(np.hypot((r - 4) * 10.0, (c - 0) * 10.0)) for r, c in points]
    worst = points[int(np.argmax(radial))]
    assert check['worst_point'] == worst
    assert check['radial_distance_m'] == pytest.approx(max(radial))
    assert len(check['waypoint_returns']) == 1
    wp = check['waypoint_returns'][0]
    assert wp['waypoint'] == worst          # sin paradas: punto más alejado, no el final
    assert wp['points'][-1] == [4, 0]
    assert wp['distance_m'] >= check['radial_distance_m']
    assert route['budget']['science_total_s'] == 0
    assert route['budget']['science_stops'] == 0


def test_each_waypoint_gets_its_own_ordered_contingency_check():
    dem = np.zeros((7, 7))
    stops = [(6, 0), (0, 6), (6, 6)]
    route = plan_routes(dem, dem, dem, 10, (0, 0), (3, 3), 14400, 300, stops, 0,
                        scenario_assumptions={'max_return_distance_m': 70,
                                              'max_return_s': 120})['fast']
    check = route['budget']['return_contingency']
    assert [w['waypoint'] for w in check['waypoint_returns']] == [list(p) for p in stops]
    for wp in check['waypoint_returns']:
        straight = float(np.hypot(wp['waypoint'][0] * 10.0, wp['waypoint'][1] * 10.0))
        assert wp['distance_m'] >= straight - 1e-9
        assert wp['distance_status'] == ('exceeds_limit' if wp['distance_m'] > 70 else 'within_limit')
        assert wp['time_status'] == ('exceeds_limit' if wp['time_s'] > 120 else 'within_limit')
        assert wp['points'][-1] == [0, 0]
    statuses = {w['distance_status'] for w in check['waypoint_returns']}
    assert statuses == {'within_limit', 'exceeds_limit'}   # límites por parada, no globales
    b = route['budget']
    assert b['operational_s'] == pytest.approx(
        b['outbound_travel_s'] + b['return_travel_s'] + b['science_total_s']
        + b['overhead_s'] + b['other_tasks_s'], abs=1e-6)   # contingencia no se suma


# --- Overrides de escenario -------------------------------------------------

def test_overrides_reach_budget_terms_and_metadata():
    base = _route([(1, 1)])['budget']
    slower = _route([(1, 1)], v_return_flat_kmh=1.0)['budget']
    assert slower['return_travel_s'] == pytest.approx(base['return_travel_s'] * 2.0, rel=1e-9)
    assert slower['outbound_travel_s'] == pytest.approx(base['outbound_travel_s'], rel=1e-12)
    over = plan_routes(np.zeros((5, 5)), np.zeros((5, 5)), np.zeros((5, 5)), 100,
                       (2, 0), (2, 4), 7200, 900, [(2, 2)], 0,
                       scenario_assumptions={'egress_s': 600, 'ingress_s': 60,
                                             'reserve_fixed_s': 0})['fast']['budget']
    assert over['overhead_s'] == 660
    assert over['reserve_s'] == 0
    assert over['science_total_s'] == 900
    assert over['margin_s'] == pytest.approx(7200 - over['operational_s'], abs=1e-6)
    meta = metadata(scenario({'reserve_fixed_s': 0}), site='synthetic')
    assert meta['values']['reserve_fixed_s'] == 0
    assert meta['defaults']['reserve_fixed_s'] == 1800
    assert meta['overrides'] == {'build': {'reserve_fixed_s': 0}, 'request': {}}
    assert set(meta['units']) == set(DEFAULTS)


def test_build_overrides_preserved_and_request_overrides_separate(tmp_path):
    from pipeline.manifest import write_manifest
    from relay.app import create_app

    for name in ('dem', 'slope', 'rough'):
        np.save(tmp_path / f'{name}.npy', np.zeros((5, 5)))
    path = tmp_path / 'routes.json'
    write_manifest(path, site='synth', waypoints=[], layers=[], routes={},
                   budget_s=14400, science_s=1200, cell_m=4,
                   dem_validity='nodata-preserved-v1',
                   scenario_assumptions={'reserve_fixed_s': 3600},
                   scenario_overrides={'reserve_fixed_s': 3600})
    before = hashlib.sha256(path.read_bytes()).hexdigest()
    client = create_app(str(tmp_path)).test_client()

    base = client.get('/api/routes').get_json()['scenario']
    assert base['overrides'] == {'build': {'reserve_fixed_s': 3600}, 'request': {}}
    assert base['values']['reserve_fixed_s'] == 3600
    assert base['values']['science_stop_s'] == DEFAULTS['science_stop_s']

    inherited = client.post('/api/route', json={'start': [0, 0], 'end': [4, 4]}).get_json()
    assert inherited['scenario']['overrides'] == {'build': {'reserve_fixed_s': 3600}, 'request': {}}
    assert inherited['scenario']['values']['reserve_fixed_s'] == 3600
    assert inherited['fast']['budget']['reserve_s'] == 3600

    requested = client.post('/api/route', json={
        'start': [0, 0], 'end': [4, 4], 'science_waypoints': [[1, 1], [2, 2]],
        'scenario_assumptions': {'science_stop_s': 900}}).get_json()
    assert requested['scenario']['overrides'] == {'build': {'reserve_fixed_s': 3600},
                                                  'request': {'science_stop_s': 900}}
    assert requested['scenario']['values']['science_stop_s'] == 900
    assert requested['scenario']['values']['reserve_fixed_s'] == 3600   # heredado del build
    assert requested['fast']['budget']['science_total_s'] == 1800
    assert requested['fast']['budget']['reserve_s'] == 3600
    assert hashlib.sha256(path.read_bytes()).hexdigest() == before      # POST no edita el archivo

    for bad in ({'unknown_key': 1}, {'science_stop_s': -1}, {'reserve_fixed_s': True},
                {'v_nom_flat_kmh': 0}, {'suit_budget_s': float('inf')}):
        response = client.post('/api/route', json={'start': [0, 0], 'end': [4, 4],
                                                   'scenario_assumptions': bad})
        assert response.status_code == 400, bad


# --- Mensaje: no es capacidad de traje marciano -----------------------------

def test_output_disclaims_mars_suit_capacity(tmp_path):
    from pipeline.manifest import write_manifest
    from relay.app import create_app

    for name in ('dem', 'slope', 'rough'):
        np.save(tmp_path / f'{name}.npy', np.zeros((5, 5)))
    doc = write_manifest(tmp_path / 'routes.json', site='synth', waypoints=[], layers=[],
                         routes={}, budget_s=14400, science_s=1200, cell_m=4,
                         dem_validity='nodata-preserved-v1')
    note = doc['scenario']['note']
    assert 'not a Mars suit capacity' in note
    assert 'not NASA GO/NO-GO' in note
    assert 'simulated' in doc['note']
    data = create_app(str(tmp_path)).test_client().post(
        '/api/route', json={'start': [0, 0], 'end': [4, 4],
                            'science_waypoints': [[1, 1]]}).get_json()
    assert 'not a Mars suit capacity' in data['scenario']['note']
    path_note = data['fast']['budget']['return_contingency']['path_note']
    assert 'not field validation' in path_note
    assert 'not added to planned return' in path_note.lower()


# --- Contrato v3 ------------------------------------------------------------

def test_contract_v3_shape_from_cli_build(tmp_path):
    import subprocess
    import sys

    from relay.app import create_app

    subprocess.run([sys.executable, '-m', 'pipeline.build', '--synth', '--rows', '16',
                    '--cols', '16', '--science-waypoint', '5', '5', '--out', str(tmp_path)],
                   check=True, capture_output=True, text=True)
    doc = json.loads((tmp_path / 'routes.json').read_text())
    assert doc['schema_version'] == 3
    assert doc['dem_validity'] == 'nodata-preserved-v1'
    assert doc['cell_m'] > 0 and doc['cell_x_m'] > 0
    assert doc['budget_model'] == 'LUNAR-ANALOG-2026'
    assert doc['science_stops'] == 1
    assert set(doc['routes']) == {'fast', 'safe'}
    sc = doc['scenario']
    assert sc['profile'] == sc['scenario_id'] == 'LUNAR-ANALOG-2026'
    assert sc['assumption_basis'] == 'lunar_analogue'
    assert sc['site'] == 'synthetic'
    assert set(sc['values']) == set(sc['defaults']) == set(sc['units']) == set(DEFAULTS)
    assert set(sc['overrides']) == {'build', 'request'}
    for name in ('fast', 'safe'):
        b = doc['routes'][name]['budget']
        assert b['science_stops'] == 1
        assert b['reserve_s'] == DEFAULTS['reserve_fixed_s']
        assert b['return_contingency']['path_method'] == 'fastest_directed_terrain_path'
    assert create_app(str(tmp_path)).test_client().get('/api/routes').get_json() == doc
