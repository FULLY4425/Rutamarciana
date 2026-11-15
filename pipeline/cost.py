import numpy as np

SUIT_FACTOR = 0.4      # escala de movilidad Tobler: llano ≈2.01 km/h; no consumibles
MAX_SLOPE_DEG = 25.0   # por encima, intransitable
DOSE_MSV_PER_HOUR = 0.02  # supuesto medio de superficie (RAD); se cita en README


def tobler_kmh(slope_deg):
    s = np.tan(np.radians(slope_deg))
    return 6.0 * np.exp(-3.5 * np.abs(s + 0.05))


def seconds_per_meter(slope_deg, rough, suit=SUIT_FACTOR, k=1.0):
    v_ms = tobler_kmh(slope_deg) * suit / 3.6
    return (1.0 / v_ms) * (1.0 + k * rough)


def cost_grid(slope_deg, rough, suit=SUIT_FACTOR, k=1.0):
    """Costo en segundos por metro. Celdas > MAX_SLOPE_DEG quedan en +inf."""
    c = seconds_per_meter(slope_deg, rough, suit, k)
    c = np.array(c, dtype=np.float64, copy=True)
    c[np.asarray(slope_deg) > MAX_SLOPE_DEG] = np.inf
    return c
