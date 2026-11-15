"""Visual layers derived from the active DEM; never used as routing inputs."""
import numpy as np


def terrain_rgba(dem, slope, cell_m, cell_x_m, layer):
    valid = np.isfinite(dem)
    if not valid.any():
        raise ValueError('El terreno no contiene elevaciones válidas.')
    filled = np.where(valid, dem, np.nanmedian(dem))
    low, high = float(np.nanmin(dem)), float(np.nanmax(dem))
    if layer == 'elevation':
        t = np.clip((filled-low)/(high-low or 1), 0, 1)
        rgb = np.stack([55+185*t, 103+104*t, 111+48*t], axis=-1)
    elif layer == 'slope':
        valid &= np.isfinite(slope)
        t = np.clip(np.nan_to_num(slope)/25, 0, 1)
        rgb = np.stack([80+160*t, 175-105*t, 126-65*t], axis=-1)
    elif layer == 'natural':
        dy, dx = np.gradient(filled, cell_m, cell_x_m)
        length = np.sqrt(dx*dx+dy*dy+1)
        light = .42+.58*np.maximum(0, (.6*dx+.8+.3*dy)/(length*np.sqrt(1.09)))
        rgb = light[..., None]*np.array([190,128,87])
    else:
        raise ValueError('Capa desconocida.')
    return np.concatenate([np.clip(rgb,0,255).astype(np.uint8), (valid*255).astype(np.uint8)[...,None]], axis=-1)
