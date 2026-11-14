import numpy as np


def ramp(slope: float = 0.25, size: int = 40, cell_m: float = 4.0):
    """DEM en rampa: sube `slope` metros por metro hacia +x."""
    x = np.arange(size, dtype=np.float32) * cell_m
    dem = np.tile(x * slope, (size, 1))
    return dem, cell_m


def hills(size: int = 40, cell_m: float = 4.0, seed: int = 7):
    """Terreno con lomas suaves y un muro con hueco: sirve para pruebas de ruta."""
    rng = np.random.default_rng(seed)
    x = np.arange(size, dtype=np.float32) * cell_m
    z = 5.0 * np.sin(x / 30.0)[None, :] + 3.0 * np.cos(x / 47.0)[:, None]
    z = z + rng.normal(0, 0.05, z.shape)
    wall_col = size // 2
    gap_row = int(size * 0.75)
    z[:gap_row, wall_col:wall_col + 2] += 8.0   # muro que obliga a rodear por el hueco
    return z.astype(np.float32), cell_m
