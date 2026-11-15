import json
import numpy as np

from relay.app import create_app


def test_route_api_requires_dem_and_returns_roundtrip(tmp_path):
    np.save(tmp_path / 'dem.npy', np.tile(np.arange(7) * .6, (7, 1)))
    np.save(tmp_path / 'slope.npy', np.full((7, 7), np.degrees(np.arctan(.15))))
    np.save(tmp_path / 'rough.npy', np.zeros((7, 7)))
    (tmp_path / 'routes.json').write_text(json.dumps({'schema_version': 3, 'dem_validity': 'nodata-preserved-v1', 'cell_m': 4, 'cell_x_m': 4, 'science_waypoints': [], 'reserve_fraction': .2}))
    client = create_app(str(tmp_path)).test_client()
    response = client.post('/api/route', json={'start': [3, 1], 'end': [3, 5]})
    assert response.status_code == 200
    data = response.get_json()
    assert data['science_stops'] == 0
    assert data['reserve_fraction'] == .2
    assert data['fast']['return']['points'][-1] == [3, 1]
    assert data['fast']['budget']['used_s'] == data['fast']['time_s']
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


def test_v3_before_nodata_preservation_requires_rebuild(tmp_path):
    for name in ('dem', 'slope', 'rough'):
        np.save(tmp_path / f'{name}.npy', np.zeros((3, 3)))
    (tmp_path / 'routes.json').write_text(json.dumps({'schema_version': 3, 'cell_m': 4, 'cell_x_m': 4}))
    client = create_app(str(tmp_path)).test_client()
    assert client.get('/api/routes').status_code == 503
    assert client.post('/api/route', json={'start': [0, 0], 'end': [1, 1]}).status_code == 503


def test_waypoint_api_validation_and_legacy_migration(tmp_path):
    for name in ('dem', 'slope', 'rough'):
        np.save(tmp_path / f'{name}.npy', np.zeros((5, 5)))
    manifest = tmp_path / 'routes.json'
    manifest.write_text(json.dumps({'schema_version': 3, 'dem_validity': 'nodata-preserved-v1', 'cell_m': 4, 'cell_x_m': 3,
                                    'science_s': 12, 'science_stops': 99}))
    client = create_app(str(tmp_path)).test_client()
    base = {'start': [0, 0], 'end': [4, 4]}
    for points in ([], [[2, 2]], [[0, 4], [4, 0]]):
        result = client.post('/api/route', json={**base, 'science_waypoints': points})
        assert result.status_code == 200
        data = result.get_json()
        assert data['science_stops'] == len(points)
        for name in ('fast', 'safe'):
            route = data[name]
            assert route['science_total_s'] == len(points) * 12
            assert len(route['outbound']['legs']) == len(points) + 1
            assert route['total_s'] == route['budget']['used_s']
    for invalid in ([[1, 1], [1, 1]], [[0, 0]], [[4, 4]], [[-1, 2]], [[5, 0]],
                    [[1.0, 2]], [[True, 1]], [[float('nan'), 1]], [[float('inf'), 0]],
                    [[1]], [None], None, 5):
        assert client.post('/api/route', json={**base, 'science_waypoints': invalid}).status_code == 400
    assert client.post('/api/route', json={**base, 'science_stops': 5}).status_code == 400
    assert client.post('/api/route', json={**base, 'schema_version': 2}).status_code == 400
    manifest.write_text(json.dumps({'schema_version': 2, 'cell_m': 4, 'cell_x_m': 4,
                                    'science_stops': 5}))
    assert client.post('/api/route', json=base).status_code == 503
    assert client.get('/api/routes').status_code == 503


def test_waypoint_blocked_and_impossible_segment(tmp_path):
    slope = np.zeros((5, 5)); slope[:, 2] = 26
    for name, data in (('dem', np.zeros((5, 5))), ('slope', slope), ('rough', np.zeros((5, 5)))):
        np.save(tmp_path / f'{name}.npy', data)
    (tmp_path / 'routes.json').write_text(json.dumps({'schema_version': 3, 'dem_validity': 'nodata-preserved-v1', 'cell_m': 4, 'cell_x_m': 4}))
    client = create_app(str(tmp_path)).test_client()
    base = {'start': [0, 0], 'end': [4, 0]}
    response = client.post('/api/route', json={**base, 'science_waypoints': [[2, 2]]})
    assert response.status_code == 400 and 'intransitable' in response.json['error']
    response = client.post('/api/route', json={**base, 'science_waypoints': [[2, 4]]})
    assert response.status_code == 400 and 'tramo 1' in response.json['error']


def test_second_segment_failure_is_identified(tmp_path):
    slope = np.zeros((5, 5)); slope[:, 2] = 26
    for name, value in (('dem', np.zeros((5, 5))), ('slope', slope), ('rough', np.zeros((5, 5)))):
        np.save(tmp_path / f'{name}.npy', value)
    (tmp_path / 'routes.json').write_text(json.dumps({'schema_version': 3, 'dem_validity': 'nodata-preserved-v1', 'cell_m': 4, 'cell_x_m': 4}))
    response = create_app(str(tmp_path)).test_client().post('/api/route', json={
        'start': [0, 0], 'science_waypoints': [[4, 0]], 'end': [4, 4]})
    assert response.status_code == 400
    assert 'tramo 2' in response.json['error']
