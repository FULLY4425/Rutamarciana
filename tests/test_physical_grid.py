import json
import hashlib
from pathlib import Path

import numpy as np
import pytest
import rasterio
from PIL import Image
from rasterio.transform import from_origin

from pipeline.build import _load_dem
from pipeline.route import plan_routes
from pipeline.terrain import slope_deg
from relay.app import create_app


def test_anisotropic_routes_and_gradient():
    factor = np.cos(np.radians(18.49))
    x, y = 4 * factor, 4.0
    flat = np.zeros((7, 7))
    def route(end):
        return plan_routes(flat, flat, flat, y, (1, 1), end, 10000, 0, 0, 0, cell_x_m=x)['fast']
    assert route((1, 5))['outbound']['distance_m'] == pytest.approx(4 * x, abs=.05)
    assert route((5, 1))['outbound']['distance_m'] == pytest.approx(4 * y, abs=.05)
    assert route((5, 5))['outbound']['distance_m'] == pytest.approx(4 * np.hypot(x, y), abs=.05)
    dem = np.tile(np.arange(7) * .8, (7, 1))
    slope = slope_deg(dem, y, x)
    assert slope[3, 3] == pytest.approx(np.degrees(np.arctan(.8 / x)), abs=1e-5)
    result = plan_routes(dem, slope, flat, y, (3, 1), (3, 5), 10000, 0, 0, 0, cell_x_m=x)
    assert result['fast']['slope_max_deg'] == pytest.approx(slope[3, 3], abs=.01)


def test_jezero_projection_window_and_relay(tmp_path):
    path = tmp_path / 'dem.tif'
    with rasterio.open(path, 'w', driver='GTiff', height=20, width=20, count=1,
                       dtype='float32', crs='ESRI:103885',
                       transform=from_origin(4587066, 1096874, 1, 1)) as ds:
        ds.write(np.zeros((20, 20), dtype='float32'), 1)
    dem, y, x, spatial = _load_dem(path, 4, 5, 5)
    assert dem.shape == (5, 5)
    assert y == 4
    assert x == pytest.approx(4 * np.cos((1096874 - 10) / 3396190))
    assert spatial['bounds'] == [4587066, 1096854, 4587086, 1096874]
    assert spatial['crs'] == 'ESRI:103885'
    from pipeline.manifest import write_manifest
    slope = slope_deg(dem, y, x)
    result = plan_routes(dem, slope, dem, y, (0, 0), (4, 4), 14400, 0, 0, 0, cell_x_m=x)
    for name, value in [('dem', dem), ('slope', slope), ('rough', dem)]:
        np.save(tmp_path / f'{name}.npy', value)
    write_manifest(tmp_path / 'routes.json', site='test', waypoints=[], layers=[], routes=result,
                   budget_s=14400, science_s=0, science_stops=0, reserve_fraction=0,
                   cell_m=y, cell_x_m=x, spatial=spatial)
    client = create_app(str(tmp_path)).test_client()
    response = client.post('/api/route', json={'start': [0, 0], 'end': [4, 4]})
    assert response.status_code == 200
    assert response.get_json()['fast'] == result['fast']
    doc = json.loads((tmp_path / 'routes.json').read_text())
    del doc['cell_x_m']
    (tmp_path / 'routes.json').write_text(json.dumps(doc))
    assert client.post('/api/route', json={'start': [0, 0], 'end': [4, 4]}).status_code == 503
    assert client.get('/api/routes').status_code == 503


def test_visual_asset_grid_and_coverage():
    assets = Path(__file__).resolve().parents[1] / 'assets'
    meta = json.loads((assets / 'jezero_ortho_4m.json').read_text())
    image = assets / meta['image']
    assert hashlib.sha256(image.read_bytes()).hexdigest() == meta['image_sha256']
    pixels = np.asarray(Image.open(image))
    assert list(pixels.shape) == meta['dimensions'] == [500, 500]
    assert not np.any(pixels == 0)
    assert meta['crs'] == 'ESRI:103885'
    assert meta['bounds_m'] == [4587066, 1094874, 4589066, 1096874]
    assert meta['transform'] == [4, 0, 4587066, 0, -4, 1096874]
