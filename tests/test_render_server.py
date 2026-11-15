import pytest

import server


def test_render_entrypoint_loads_real_bundle_and_serves_assets(monkeypatch):
    monkeypatch.delenv('RUTA_DATA_DIR', raising=False)
    client = server.application().test_client()
    assert client.get('/health').status_code == 200
    assert client.get('/api/routes').json['site'] == 'jez_dtm_1m'
    assert client.get('/').status_code == 200
    assert client.get('/streetview.js').status_code == 200


def test_render_startup_refuses_to_generate_missing_terrain(monkeypatch, tmp_path):
    monkeypatch.setenv('RUTA_DATA_DIR', str(tmp_path))
    with pytest.raises(ValueError, match='Falta el paquete'):
        server.application()
    assert list(tmp_path.iterdir()) == []
