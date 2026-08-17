from __future__ import annotations

import numpy as np
import pytest

from climatology_pipeline.current import CurrentAccumulator
from climatology_pipeline.merge_current import merge


def _checkpoint(path, start: str, end: str, u: float) -> None:
    accumulator = CurrentAccumulator(np.array([-1.0, 1.0]), np.array([0.0, 180.0]))
    accumulator.add(1, np.full((2, 2), u), np.full((2, 2), -u))
    accumulator.save_checkpoint(path, metadata={
        "schema": 1,
        "source": "OSCAR_L4_OC_FINAL_V2.0",
        "start": start,
        "end": end,
        "completed_through": end,
    })


def test_merge_current_adds_exact_sufficient_statistics(tmp_path) -> None:
    first = tmp_path / "first.npz"
    second = tmp_path / "second.npz"
    _checkpoint(first, "2000-01-01", "2000-01-31", 1.0)
    _checkpoint(second, "2000-02-01", "2000-02-29", 3.0)
    merged, metadata = merge([second, first])
    np.testing.assert_allclose(merged.monthly_source(1)[0], 2.0)
    np.testing.assert_allclose(merged.monthly_source(1)[1], -2.0)
    assert metadata["start"] == "2000-01-01"
    assert metadata["end"] == "2000-02-29"


@pytest.mark.parametrize("second_start", ["2000-01-31", "2000-02-02"])
def test_merge_current_rejects_overlap_or_gap(tmp_path, second_start: str) -> None:
    first = tmp_path / "first.npz"
    second = tmp_path / "second.npz"
    _checkpoint(first, "2000-01-01", "2000-01-31", 1.0)
    _checkpoint(second, second_start, "2000-02-29", 3.0)
    with pytest.raises(ValueError, match="overlap|gap"):
        merge([first, second])
