"""Smoke-test con el DEM real de Jezero (500x500 a 4 m).

Construye el pipeline completo sobre data/raw/jez_dtm_1m.tif, valida el
manifiesto v3, sirve GET /api/routes y hace POST /api/route con paradas
ordenadas. Se omite si el DEM real no está presente (repo parcial / CI).

No modifica web/: se verifica con `git status --porcelain -- web/`.
"""
import hashlib
import json
import shutil
import subprocess
import sys
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parents[1]
DEM = ROOT / 'data' / 'raw' / 'jez_dtm_1m.tif'

pytestmark = pytest.mark.skipif(
    not DEM.exists() or shutil.which('git') is None,
    reason='requiere el DEM real de Jezero y git para verificar web/')


@pytest.fixture(scope='module')
def jezero_build(tmp_path_factory):
    out = tmp_path_factory.mktemp('jezero500')
    subprocess.run([sys.executable, '-m', 'pipeline.build', '--dem', str(DEM),
                    '--cell', '4', '--rows', '500', '--cols', '500', '--out', str(out),
                    '--science-waypoint', '100', '200',
                    '--science-waypoint', '200', '300'],
                   check=True, capture_output=True, text=True, cwd=ROOT)
    return out


def _web_status():
    return subprocess.run(['git', 'status', '--porcelain', '--', 'web/'], cwd=ROOT,
                          check=True, capture_output=True, text=True).stdout


def test_jezero_build_manifest_is_v3_contract(jezero_build):
    doc = json.loads((jezero_build / 'routes.json').read_text())
    assert doc['schema_version'] == 3
    assert doc['dem_validity'] == 'nodata-preserved-v1'
    assert doc['site'] == 'jez_dtm_1m'
    assert doc['scenario']['site'] == 'Jezero/Mars'
    assert doc['grid'] == {'rows': 500, 'cols': 500}
    assert doc['cell_m'] == 4.0
    assert doc['science_stops'] == 2 and len(doc['science_waypoints']) == 2
    for name in ('fast', 'safe'):
        route = doc['routes'][name]
        assert route['budget']['science_stops'] == 2
        assert route['budget']['science_total_s'] == 2 * doc['scenario']['values']['science_stop_s']
        assert route['outbound']['distance_m'] > 0 and route['return']['distance_m'] > 0
        assert route['budget']['return_contingency']['path_method'] == 'fastest_directed_terrain_path'
        assert len(route['budget']['return_contingency']['waypoint_returns']) == 2
    assert _web_status() == ''


def test_jezero_get_and_post_ordered_waypoints(jezero_build):
    from relay.app import create_app

    manifest_path = jezero_build / 'routes.json'
    before = hashlib.sha256(manifest_path.read_bytes()).hexdigest()
    client = create_app(str(jezero_build)).test_client()

    served = client.get('/api/routes')
    assert served.status_code == 200
    doc = served.get_json()
    assert doc['schema_version'] == 3
    assert doc['scenario']['values']['suit_budget_s'] == doc['budget_s']

    response = client.post('/api/route', json={
        'schema_version': 3, 'start': [2, 2], 'end': [497, 497],
        'science_waypoints': [[100, 200], [200, 300]]})
    assert response.status_code == 200
    data = response.get_json()
    assert data['schema_version'] == 3
    assert data['science_stops'] == 2
    assert data['cell_m'] == doc['cell_m'] and data['cell_x_m'] == doc['cell_x_m']
    for name in ('fast', 'safe'):
        route = data[name]
        assert [leg['points'][-1] for leg in route['outbound']['legs']] == \
            [[100, 200], [200, 300], [497, 497]]
        assert route['return']['points'][0] == [497, 497]
        assert route['return']['points'][-1] == [2, 2]
        budget = route['budget']
        assert budget['science_total_s'] == 2 * data['scenario']['values']['science_stop_s']
        assert budget['overhead_s'] == (data['scenario']['values']['egress_s']
                                        + data['scenario']['values']['ingress_s'])
        assert budget['operational_s'] == pytest.approx(
            budget['outbound_travel_s'] + budget['return_travel_s']
            + budget['science_total_s'] + budget['overhead_s']
            + budget['other_tasks_s'], abs=1e-6)
        assert budget['status'] in ('cabe', 'justo', 'no_cabe')
        assert 'not a Mars suit capacity' in data['scenario']['note']
        assert len(budget['return_contingency']['waypoint_returns']) == 2
    assert hashlib.sha256(manifest_path.read_bytes()).hexdigest() == before
    assert _web_status() == ''
