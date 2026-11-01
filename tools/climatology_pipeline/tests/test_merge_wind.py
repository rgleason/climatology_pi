from __future__ import annotations

import json

import pytest

from climatology_pipeline.merge_wind import merge_checkpoints
from climatology_pipeline.wind import WindAccumulator


def _checkpoint(path, start: int, requested_end: int, completed: int, count: int) -> None:
    accumulator = WindAccumulator(months=1, latitudes=1, longitudes=1)
    accumulator.total[...] = count
    accumulator.direction_count[..., 0] = count
    accumulator.direction_speed_sum[..., 0] = count * 10.0
    accumulator.save_checkpoint(path)
    path.with_suffix(path.suffix + ".json").write_text(json.dumps({
        "start_year": start,
        "end_year": requested_end,
        "completed_year": completed,
    }), encoding="utf-8")


def test_merge_wind_accepts_contiguous_effective_ranges(tmp_path) -> None:
    first = tmp_path / "first.npz"
    second = tmp_path / "second.npz"
    _checkpoint(first, 1995, 2022, 1999, 5)
    _checkpoint(second, 2000, 2004, 2004, 5)
    merged, periods = merge_checkpoints([second, first])
    assert merged.total[0, 0, 0] == 10
    assert periods[0]["start_year"] == 1995
    assert periods[-1]["completed_year"] == 2004


@pytest.mark.parametrize("start", [1999, 2001])
def test_merge_wind_rejects_overlap_or_gap(tmp_path, start: int) -> None:
    first = tmp_path / "first.npz"
    second = tmp_path / "second.npz"
    _checkpoint(first, 1995, 1999, 1999, 5)
    _checkpoint(second, start, 2004, 2004, 5)
    with pytest.raises(ValueError, match="overlap|gap"):
        merge_checkpoints([first, second])
