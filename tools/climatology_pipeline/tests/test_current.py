from __future__ import annotations

import numpy as np
import pytest

from climatology_pipeline.budget import GB, oscar_plan
from climatology_pipeline.current import CurrentAccumulator
from climatology_pipeline.wind import KNOTS_PER_MPS


def test_oscar_storage_plan_models_monthly_final_v2_batch() -> None:
    plan = oscar_plan()
    plan.validate()
    assert plan.budget_bytes == 100 * GB
    assert 3 * GB < plan.peak_bytes < 10 * GB
    assert plan.source_chunk_bytes == 31 * 32_000_000
    with pytest.raises(RuntimeError, match="exceeds budget"):
        oscar_plan(budget_gb=1).validate()


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


def test_current_checkpoint_metadata(tmp_path) -> None:
    accumulator = CurrentAccumulator(np.array([-1.0, 1.0]), np.array([0.0, 180.0]))
    path = tmp_path / "current.npz"
    metadata = {
        "schema": 1,
        "source": "OSCAR_L4_OC_FINAL_V2.0",
        "start": "1995-01-01",
        "end": "2022-08-05",
        "completed_through": "1995-01-31",
    }
    accumulator.save_checkpoint(path, metadata=metadata)
    assert CurrentAccumulator.checkpoint_metadata(path) == metadata
