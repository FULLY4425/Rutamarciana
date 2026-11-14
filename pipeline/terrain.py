import numpy as np
from scipy.ndimage import uniform_filter


def slope_deg(dem: np.ndarray, cell_m: float) -> np.ndarray:
    """Pendiente en grados. `dem` en metros, `cell_m` en metros."""
    gy, gx = np.gradient(dem.astype(np.float32), cell_m, cell_m)
    return np.degrees(np.arctan(np.hypot(gx, gy))).astype(np.float32)


def roughness_pct(img: np.ndarray, win: int = 5) -> np.ndarray:
    """Rugosidad relativa 0..1 por percentil, desde la imagen de la zona."""
    f = img.astype(np.float32)
    mean = uniform_filter(f, win)
    mean_sq = uniform_filter(f * f, win)
    std = np.sqrt(np.clip(mean_sq - mean * mean, 0, None))
    if float(std.max() - std.min()) < 1e-9:
        return np.zeros(std.shape, dtype=np.float32)
    r = std.ravel()
    order = r.argsort().argsort().astype(np.float32)
    return (order / max(1, r.size - 1)).reshape(std.shape)
