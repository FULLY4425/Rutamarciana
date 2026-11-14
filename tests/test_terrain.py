import numpy as np
from pipeline.synth import ramp


def test_ramp_has_expected_slope():
    dem, cell_m = ramp(slope=0.25, size=20, cell_m=4.0)
    assert dem.shape == (20, 20)
    assert abs((dem[0, 1] - dem[0, 0]) - 1.0) < 1e-6
    assert cell_m == 4.0


def test_slope_of_ramp_is_atan_of_gradient():
    from pipeline.terrain import slope_deg

    dem, cell_m = ramp(slope=0.25, size=20, cell_m=4.0)
    s = slope_deg(dem, cell_m)
    interior = s[2:-2, 2:-2]
    assert np.allclose(interior, np.degrees(np.arctan(0.25)), atol=0.2)


def test_roughness_is_zero_on_flat_and_higher_on_step():
    from pipeline.terrain import roughness_pct

    flat = np.zeros((30, 30), dtype=np.float32)
    assert float(roughness_pct(flat).max()) <= 1e-6

    step = flat.copy()
    step[:, 15:] = 10.0
    r = roughness_pct(step)
    assert float(r[:, 13:18].mean()) > float(r[:, :5].mean())
