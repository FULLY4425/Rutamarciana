import json

import numpy as np
import pytest

from pipeline.manifest import write_manifest
from pipeline.scenario import DEFAULTS
from relay.app import create_app


@pytest.fixture
def api(tmp_path):
    for name in ('dem', 'slope', 'rough'):
        np.save(tmp_path / f'{name}.npy', np.zeros((5, 5)))
    write_manifest(tmp_path / 'routes.json', site='synth', waypoints=[], layers=[], routes={},
                   budget_s=14400, science_s=1200, cell_m=4,
                   dem_validity='nodata-preserved-v1',
                   scenario_assumptions={'reserve_fixed_s': 3600},
                   scenario_overrides={'reserve_fixed_s': 3600})
    return create_app(str(tmp_path)).test_client(), tmp_path / 'routes.json'


def test_effective_defaults_overrides_and_budget(api):
    client, _ = api
    manifest = client.get('/api/routes').json
    base = manifest['scenario']
    assert base['defaults'] == DEFAULTS
    assert base['overrides'] == {'build': {'reserve_fixed_s': 3600}, 'request': {}}
    assert set(base['units']) == set(DEFAULTS)
    assert base['site'] == 'synthetic'
    response = client.post('/api/route', json={'start': [0, 0], 'end': [4, 4],
                            'science_waypoints': [[1, 1], [2, 2]],
                            'scenario_assumptions': {'science_stop_s': 600}})
    assert response.status_code == 200
    data = response.json
    assert data['scenario']['overrides'] == {'build': {'reserve_fixed_s': 3600}, 'request': {'science_stop_s': 600}}
    assert data['scenario']['values']['science_stop_s'] == 600
    for name in ('fast', 'safe'):
        b = data[name]['budget']
        assert b['science_stops'] == 2 and b['science_total_s'] == 1200
        assert b['operational_s'] == pytest.approx(b['outbound_travel_s'] + b['return_travel_s'] + b['science_total_s'] + b['overhead_s'] + b['other_tasks_s'])
        assert b['slack_s'] == pytest.approx(14400 - b['operational_s'] - 3600)
        assert b['return_contingency']['status'] and b['limiting_terms']
    empty = client.post('/api/route', json={'start': [0, 0], 'end': [4, 4]}).json
    assert empty['fast']['budget']['science_total_s'] == 0
    assert empty['scenario']['values']['reserve_fixed_s'] == 3600


@pytest.mark.parametrize('content', ['{broken', '[]', 'null', '42', 'NaN', '{"x":NaN}'])
def test_bad_manifest_returns_503(api, content):
    client, path = api
    path.write_text(content)
    assert client.get('/api/manifest').status_code == 503
    assert client.get('/api/routes').status_code == 503
    assert client.post('/api/route', json={'start': [0, 0], 'end': [4, 4]}).status_code == 503


@pytest.mark.parametrize('mutation', [
    lambda d: d.update(site='jez_dtm_1m'),
    lambda d: d.update(budget_s=1),
    lambda d: d.update(science_s=1),
    lambda d: d.update(science_stops=1),
    lambda d: d['scenario'].update(note='NASA GO'),
    lambda d: d['scenario']['defaults'].update(science_stop_s=7),
    lambda d: d['scenario']['values'].update(reserve_fixed_s=7),
    lambda d: d['scenario']['values'].update(other_tasks_s=False),
])
def test_contradictory_manifest_rejected(api, mutation):
    client, path = api
    doc = json.loads(path.read_text())
    mutation(doc)
    path.write_text(json.dumps(doc))
    assert client.get('/api/routes').status_code == 503
    assert client.post('/api/route', json={'start': [0, 0], 'end': [4, 4]}).status_code == 503
