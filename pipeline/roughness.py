import numpy as np
from scipy.ndimage import uniform_filter


def rough_from_dem(dem: np.ndarray, win: int = 3) -> np.ndarray:
    """Aproximación de rugosidad sin ortofoto: residuo del DEM suavizado -> percentil 0..1."""
    d = dem.astype(np.float32)
    smooth = uniform_filter(d, win)
    res = np.abs(d - smooth)
    r = res.ravel()
    order = r.argsort().argsort().astype(np.float32)
    return (order / max(1, r.size - 1)).reshape(res.shape)
