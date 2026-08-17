from __future__ import annotations

import numpy as np

from climatology_pipeline.current import CurrentAccumulator
from climatology_pipeline.wind import KNOTS_PER_MPS


def test_current_monthly_mean_and_units() -> None:
    accumulator = CurrentAccumulator(np.array([-90.0, 90.0]), np.array([0.0, 180.0]))
    accumulator.add(1, np.array([[[1.0, 1.0], [1.0, 1.0]], [[3.0, 3.0], [3.0, 3.0]]]),
                    np.zeros((2, 2, 2)))
    field = accumulator.field(1)
    np.testing.assert_allclose(field.u, 2.0 * KNOTS_PER_MPS)
    np.testing.assert_allclose(field.v, 0.0)


def test_current_missing_renormalisation_and_dateline() -> None:
    accumulator = CurrentAccumulator(np.array([-1.0, 1.0]), np.array([0.0, 180.0]))
    u = np.array([[1.0, np.nan], [1.0, 1.0]])
    v = np.array([[2.0, np.nan], [2.0, 2.0]])
    accumulator.add(6, u, v)
    field = accumulator.field(6)
    assert np.isfinite(field.u[240, 0])
    assert np.isfinite(field.u[240, -1])
    np.testing.assert_allclose(field.v[np.isfinite(field.v)] / field.u[np.isfinite(field.u)], 2.0)


def test_current_checkpoint(tmp_path) -> None:
    accumulator = CurrentAccumulator(np.array([-1.0, 1.0]), np.array([0.0, 180.0]))
    accumulator.add(1, np.ones((2, 2)), np.ones((2, 2)) * 2)
    path = tmp_path / "current.npz"
    accumulator.save_checkpoint(path)
    restored = CurrentAccumulator.load_checkpoint(path)
    np.testing.assert_array_equal(restored.count, accumulator.count)
    np.testing.assert_allclose(restored.u_sum, accumulator.u_sum)
