import hashlib
import json
from pathlib import Path

import numpy as np
import pytest

from relay.app import create_app
from pipeline import fetch_jezero
import iniciar

ROOT = Path(__file__).resolve().parent.parent
BUNDLE = ROOT / 'assets' / 'jezero'


def test_bundled_real_jezero_is_complete_and_hashes_match():
    provenance = json.loads((BUNDLE / 'provenance.json').read_text())
    for name, expected in provenance['derived_sha256'].items():
        assert hashlib.sha256((BUNDLE / name).read_bytes()).hexdigest() == expected
    dem = np.load(BUNDLE / 'dem.npy', allow_pickle=False)
    assert dem.shape == (500, 500)
    assert np.isfinite(dem).all()
    assert -2600 < dem.min() < dem.max() < -2300
    manifest = json.loads((BUNDLE / 'routes.json').read_text())
    assert manifest['site'] == 'jez_dtm_1m'
    assert manifest['scenario']['site'] == 'Jezero/Mars'
    reference = json.loads((ROOT / 'assets' / 'jezero_ortho_4m.json').read_text())
    assert manifest['spatial']['source_window'] == reference['dem_source_window']
    assert manifest['spatial']['bounds'] == reference['bounds_m']
    assert manifest['spatial']['crs'] == reference['crs']
    assert manifest['cell_m'] == 4
    assert manifest['cell_x_m'] == pytest.approx(3.793559095, rel=1e-7)


def test_default_launcher_selects_bundled_real_data_and_demo_is_explicit():
    assert iniciar.options([]).data == BUNDLE
    assert iniciar.options(['--demo']).data == ROOT / 'data' / 'demo'


def test_scientific_api_reports_real_terrain_and_lod_without_changing_routes():
    client = create_app(str(BUNDLE)).test_client()
    full = client.get('/api/terrain?detail=150').json
    assert len(full['rows']) == len(full['cols']) == 150
    assert full['rows'][-1] == full['cols'][-1] == 499
    assert full['provenance']['doi'] == '10.5066/P9REJ9JN'
    assert full['statistics']['valid_cells'] == 250000
    assert full['statistics']['elevation_max_m'] < 0
    assert len(full['slopes']) == 150
    assert len(client.get('/api/terrain').json['rows']) == 75
    assert client.get('/api/routes').status_code == 200
    for value in ['-1', '0', '151', '100000', 'bad']:
        assert client.get('/api/terrain?detail=' + value).status_code == 400


def test_catalog_distinguishes_earth_and_gale_from_jezero():
    sources = create_app(str(BUNDLE)).test_client().get('/api/science-sources').json['sources']
    catalog = {item['id']: item for item in sources}
    assert catalog['worldview']['status'] == 'excluded'
    assert catalog['mount-sharp']['role'] == 'other_region'
    assert catalog['perseverance']['status'] == 'reference'
    assert catalog['usgs-jezero']['status'] == 'integrated'


def test_remote_reader_refuses_full_file_download(monkeypatch):
    class FullResponse:
        status = 200
        headers = {}
        def __enter__(self): return self
        def __exit__(self, *args): return False
        def read(self, *args): raise AssertionError('A full file must never be read')
    monkeypatch.setattr(fetch_jezero, 'urlopen', lambda *args, **kwargs: FullResponse())
    with pytest.raises(ValueError, match='parcial'):
        fetch_jezero.read_range(0, 10)


def test_importer_never_overwrites_an_existing_directory(tmp_path):
    target = tmp_path / 'real'
    target.mkdir()
    (target / 'keep.txt').write_text('keep')
    with pytest.raises(ValueError, match='existe'):
        fetch_jezero.prepare(target)
    assert (target / 'keep.txt').read_text() == 'keep'


def test_visual_layers_keep_nodata_transparent_and_preserve_input():
    from pipeline.visualization import terrain_rgba
    dem = np.array([[1., 2.], [3., np.nan]])
    slope = np.array([[0., 25.], [10., np.nan]])
    before = dem.copy()
    for layer in ('natural', 'elevation', 'slope'):
        image = terrain_rgba(dem, slope, 4, 3.79, layer)
        assert image.shape == (2, 2, 4)
        assert image[1, 1, 3] == 0
        assert image[0, 0, 3] == 255
    np.testing.assert_equal(dem, before)
    with pytest.raises(ValueError):
        terrain_rgba(dem, slope, 4, 3.79, 'unknown')


def test_visual_layer_endpoint_returns_png_for_real_data():
    client = create_app(str(BUNDLE)).test_client()
    for layer in ('natural', 'elevation', 'slope'):
        response = client.get('/api/terrain-image?layer=' + layer)
        assert response.status_code == 200
        assert response.data.startswith(b'\x89PNG\r\n\x1a\n')
    assert client.get('/api/terrain-image?layer=unknown').status_code == 400
