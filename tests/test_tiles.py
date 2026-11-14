from pathlib import Path

import numpy as np

from pipeline.tiles import save_tile_bmp


def test_tile_is_8bit_bmp(tmp_path: Path):
    img = (np.random.default_rng(0).random((240, 320)) * 255).astype(np.uint8)
    p = tmp_path / "t.bmp"
    save_tile_bmp(img, "mars", p)
    raw = p.read_bytes()
    assert raw[:2] == b"BM"
    bpp = int.from_bytes(raw[28:30], "little")
    assert bpp == 8
    assert p.stat().st_size < 90_000
