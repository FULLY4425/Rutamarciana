import numpy as np
from scipy.ndimage import uniform_filter
from scipy.stats import rankdata


def rough_from_dem(dem: np.ndarray, win: int = 3, valid=None) -> np.ndarray:
    """Aproximación de rugosidad sin ortofoto: residuo del DEM suavizado -> percentil 0..1."""
    d = dem.astype(np.float32)
    smooth = uniform_filter(d, win)
    res = np.abs(d - smooth)
    valid = np.ones(res.shape, dtype=bool) if valid is None else valid
    r = res[valid]
    out = np.full(res.shape, np.nan, dtype=np.float32)
    out[valid] = (rankdata(r, method="min").astype(np.float32) - 1) / max(1, r.size - 1)
    return out
