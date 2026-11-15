import subprocess
import sys

import numpy as np
import pytest
import rasterio
from rasterio.transform import from_origin

from relay.app import create_app
from pipeline.build import _load_dem
from pipeline.route import plan_routes
from pipeline.roughness import rough_from_dem


def test_build_and_post_preserve_nodata_barrier(tmp_path):
    dem_path = tmp_path / 'dem.tif'
    source = np.zeros((9, 9), dtype=np.float32)
    source[:, 4] = -9999
    with rasterio.open(dem_path, 'w', driver='GTiff', height=9, width=9,
                       count=1, dtype='float32', nodata=-9999,
                       transform=from_origin(100, 100, 4, 4)) as ds:
        ds.write(source, 1)
    out = tmp_path / 'out'
    result = subprocess.run([sys.executable, '-m', 'pipeline.build', '--dem', str(dem_path),
                             '--rows', '9', '--cols', '9', '--cell', '4', '--out', str(out),
                             '--start', '1', '0', '--end', '7', '0'],
                            capture_output=True, text=True)
    assert result.returncode == 0, result.stderr
    assert np.isnan(np.load(out / 'dem.npy')[:, 4]).all()
    client = create_app(str(out)).test_client()
    base = {'start': [1, 0], 'end': [7, 0]}
    control = client.post('/api/route', json=base)
    assert control.status_code == 200, control.json
    for profile in ('fast', 'safe'):
        assert all(p[1] < 4 for leg in (control.json[profile]['outbound'], control.json[profile]['return']) for p in leg['points'])
    invalid = client.post('/api/route', json={**base, 'science_waypoints': [[4, 4]]})
    assert invalid.status_code == 400 and 'intransitable' in invalid.json['error']
    impossible = client.post('/api/route', json={**base, 'science_waypoints': [[4, 8]]})
    assert impossible.status_code == 400 and 'tramo 1' in impossible.json['error']


def test_downsampled_partial_nodata_and_detour(tmp_path):
    path = tmp_path / 'source.tif'
    source = np.zeros((18, 18), dtype=np.float32)
    source[8, 8] = -9999
    with rasterio.open(path, 'w', driver='GTiff', height=18, width=18,
                       count=1, dtype='float32', nodata=-9999,
                       transform=from_origin(100, 100, 2, 2)) as ds:
        ds.write(source, 1)
    dem, y, x, _ = _load_dem(path, 4, 9, 9)
    assert np.isnan(dem[4, 4])
    assert np.isfinite(dem).sum() == 80
    slope = np.zeros((9, 9))
    result = plan_routes(dem, slope, slope, y, (4, 0), (4, 8), 10000, 0, [], 0, cell_x_m=x)
    for profile in ('fast', 'safe'):
        for leg in (result[profile]['outbound'], result[profile]['return']):
            assert [4, 4] not in leg['points']


def test_roughness_ranking_excludes_invalid_neighborhood():
    dem = np.arange(25, dtype=np.float32).reshape(5, 5) ** 2
    valid = np.ones((5, 5), dtype=bool)
    valid[1:4, 1:4] = False
    rough = rough_from_dem(dem, valid=valid)
    from scipy.ndimage import uniform_filter
    from scipy.stats import rankdata
    residuals = np.abs(dem - uniform_filter(dem, 3))[valid]
    assert np.allclose(rough[valid], (rankdata(residuals, method='min') - 1) / (valid.sum() - 1))


def test_fully_invalid_after_block_aggregation_is_rejected(tmp_path):
    path = tmp_path / 'partial.tif'
    source = np.zeros((4, 4), dtype=np.float32)
    source[::2, ::2] = -9999
    with rasterio.open(path, 'w', driver='GTiff', height=4, width=4,
                       count=1, dtype='float32', nodata=-9999,
                       transform=from_origin(100, 100, 2, 2)) as ds:
        ds.write(source, 1)
    with pytest.raises(SystemExit, match='ventana no tiene datos'):
        _load_dem(path, 4, 2, 2)
