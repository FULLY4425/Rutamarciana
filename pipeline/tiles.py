from pathlib import Path

import numpy as np
from PIL import Image

LANCZOS = getattr(getattr(Image, "Resampling", Image), "LANCZOS")

PALETTES = {
    "mars": ((12, 8, 6), (235, 190, 150)),
    "slope": ((10, 30, 10), (250, 240, 120)),
    "route": ((8, 8, 20), (120, 200, 255)),
}


def _palette(name: str):
    a, b = PALETTES[name]
    pal = []
    for i in range(256):
        t = i / 255.0
        pal += [int(a[j] + (b[j] - a[j]) * t) for j in range(3)]
    return pal


def to_u8(a: np.ndarray) -> np.ndarray:
    lo, hi = float(np.nanmin(a)), float(np.nanmax(a))
    if not np.isfinite(lo) or hi - lo < 1e-9:
        return np.zeros(a.shape, dtype=np.uint8)
    return np.clip((a - lo) / (hi - lo) * 255.0, 0, 255).astype(np.uint8)


def save_tile_bmp(img_u8: np.ndarray, palette: str, path: Path) -> Path:
    """Guarda 320x240 en BMP paletizado de 8 bits (lo que lee el firmware)."""
    path = Path(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    im = Image.fromarray(np.asarray(img_u8, dtype=np.uint8), mode="L")
    im = im.resize((320, 240), LANCZOS).convert("P")
    im.putpalette(_palette(palette))
    im.save(path, format="BMP")
    return path


def save_png(img_u8: np.ndarray, path: Path, size=(1000, 1000)) -> Path:
    path = Path(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    im = Image.fromarray(np.asarray(img_u8, dtype=np.uint8), mode="L")
    im.resize(size, LANCZOS).save(path, format="PNG")
    return path
