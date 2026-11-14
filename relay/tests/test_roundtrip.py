import json
import numpy as np

from relay.app import create_app


def test_route_api_requires_dem_and_returns_roundtrip(tmp_path):
    np.save(tmp_path / 'dem.npy', np.tile(np.arange(7) * .6, (7, 1)))
    np.save(tmp_path / 'slope.npy', np.full((7, 7), np.degrees(np.arctan(.15))))
    np.save(tmp_path / 'rough.npy', np.zeros((7, 7)))
    (tmp_path / 'routes.json').write_text(json.dumps({'schema_version': 2, 'cell_m': 4, 'cell_x_m': 4, 'science_stops': 3, 'reserve_fraction': .2}))
    client = create_app(str(tmp_path)).test_client()
    response = client.post('/api/route', json={'start': [3, 1], 'end': [3, 5]})
    assert response.status_code == 200
    data = response.get_json()
    assert data['science_stops'] == 3
    assert data['reserve_fraction'] == .2
    assert data['fast']['return']['points'][-1] == [3, 1]
    assert data['fast']['budget']['used_s'] == data['fast']['time_s'] + 1800
    assert client.post('/api/route', json={'start': [-1, 0], 'end': [1, 1]}).status_code == 400
    assert client.post('/api/route', json=[1, 2]).status_code == 400


def test_old_manifest_is_not_served_as_roundtrip(tmp_path):
    (tmp_path / 'routes.json').write_text(json.dumps({'routes': {'fast': {'time_s': 5}}}))
    response = create_app(str(tmp_path)).test_client().get('/api/routes')
    assert response.status_code == 503


def test_post_requires_manifest_for_cell_size(tmp_path):
    for name in ('dem', 'slope', 'rough'):
        np.save(tmp_path / f'{name}.npy', np.zeros((3, 3)))
    response = create_app(str(tmp_path)).test_client().post(
        '/api/route', json={'start': [0, 0], 'end': [1, 1]})
    assert response.status_code == 503
