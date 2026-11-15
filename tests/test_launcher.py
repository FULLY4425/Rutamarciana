import pytest

import iniciar


def test_relative_data_path_is_anchored_to_extracted_folder(monkeypatch, tmp_path):
    monkeypatch.chdir(tmp_path)
    args = iniciar.options(['--data', 'data/real', '--port', '8090'])
    assert args.data == iniciar.ROOT / 'data/real'
    assert args.port == 8090


@pytest.mark.parametrize('port', ['0', '65536', '-1', 'abc'])
def test_invalid_port_is_rejected_before_setup(port):
    with pytest.raises(SystemExit):
        iniciar.options(['--port', port])


def test_partial_terrain_is_never_overwritten(tmp_path):
    dem = tmp_path / 'dem.npy'
    dem.write_bytes(b'existing real terrain')
    with pytest.raises(ValueError, match='incompleto'):
        iniciar.prepare_data(tmp_path)
    assert list(tmp_path.iterdir()) == [dem]
    assert dem.read_bytes() == b'existing real terrain'


def test_nonempty_unrecognized_directory_is_preserved(tmp_path):
    original = tmp_path / 'my_dem.tif'
    original.write_bytes(b'real DEM')
    with pytest.raises(ValueError, match='contiene archivos'):
        iniciar.prepare_data(tmp_path)
    assert original.read_bytes() == b'real DEM'
    assert len(list(tmp_path.iterdir())) == 1


def test_incompatible_build_is_not_replaced(tmp_path):
    for name in iniciar.DATA_FILES:
        (tmp_path / name).write_bytes(b'old data')
    before = {p.name: p.read_bytes() for p in tmp_path.iterdir()}
    with pytest.raises(ValueError, match='incompatible'):
        iniciar.prepare_data(tmp_path)
    assert before == {p.name: p.read_bytes() for p in tmp_path.iterdir()}
