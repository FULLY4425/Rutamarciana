import numpy as np
from pipeline.cost import seconds_per_meter, tobler_kmh


def test_tobler_reference_values():
    assert abs(tobler_kmh(0.0) - 5.037) < 0.01
    assert abs(tobler_kmh(14.036) - 2.100) < 0.02


def test_flat_cell_costs_expected_seconds():
    spm = seconds_per_meter(np.array([[0.0]]), rough=np.array([[0.0]]), suit=0.4, k=1.0)
    assert abs(spm[0, 0] - 1.7867) < 0.01


def test_rough_multiplies_time():
    calm = seconds_per_meter(np.array([[0.0]]), rough=np.array([[0.0]]), suit=0.4, k=1.0)
    rough = seconds_per_meter(np.array([[0.0]]), rough=np.array([[1.0]]), suit=0.4, k=1.0)
    assert abs(rough[0, 0] / calm[0, 0] - 2.0) < 1e-6


def test_steep_cells_are_blocked():
    from pipeline.cost import cost_grid

    c = cost_grid(np.array([[0.0, 30.0]]), np.zeros((1, 2)))
    assert np.isfinite(c[0, 0])
    assert not np.isfinite(c[0, 1])
