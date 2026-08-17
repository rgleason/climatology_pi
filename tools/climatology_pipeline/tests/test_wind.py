from __future__ import annotations

import numpy as np

from climatology_pipeline.budget import GB, era5_wind_plan
from climatology_pipeline.wind import KNOTS_PER_MPS, WindAccumulator


def uv_from(speed_knots: float, from_degrees: float) -> tuple[float, float]:
    speed = speed_knots / KNOTS_PER_MPS
    radians = np.radians(from_degrees)
    return -speed * np.sin(radians), -speed * np.cos(radians)


def test_sector_probability_speed_calm_and_gale() -> None:
    accumulator = WindAccumulator(months=1)
    samples = [
        (*uv_from(2, 0),),
        (*uv_from(10, 0),),
        (*uv_from(20, 90),),
        (*uv_from(40, 90),),
    ]
    u = np.array([sample[0] for sample in samples])[:, None, None]
    v = np.array([sample[1] for sample in samples])[:, None, None]
    accumulator.add(1, u, v, np.array([0.1]), np.array([0.1]))
    atlas = accumulator.atlas(1)
    row, column = 180, 0
    assert atlas.valid[row, column]
    assert atlas.frequencies[row, column, 0] == 25
    assert atlas.frequencies[row, column, 2] == 25
    assert atlas.speeds[row, column, 0] == 6
    assert atlas.speeds[row, column, 2] == 30
    assert atlas.calm[row, column] == 25
    assert atlas.gale[row, column] == 25
    assert np.sum(atlas.frequencies[row, column]) == 50


def test_meteorological_sector_boundaries_and_dateline() -> None:
    accumulator = WindAccumulator(months=1)
    directions = [359.9, 22.4, 22.6, 337.4, 337.6]
    uv = [uv_from(12, direction) for direction in directions]
    u = np.array([item[0] for item in uv])[:, None, None]
    v = np.array([item[1] for item in uv])[:, None, None]
    accumulator.add(1, u, v, np.array([10.0]), np.array([-0.1]))
    atlas = accumulator.atlas(1)
    assert atlas.frequencies[200, 719, 0] == 30
    assert atlas.frequencies[200, 719, 1] == 10
    assert atlas.frequencies[200, 719, 7] == 10


def test_missing_and_sea_mask_are_not_counted() -> None:
    accumulator = WindAccumulator(months=1)
    u = np.array([[[1.0, np.nan]]])
    v = np.array([[[1.0, 1.0]]])
    accumulator.add(
        1,
        u,
        v,
        np.array([0.0]),
        np.array([0.0, 1.0]),
        sea_mask=np.array([[False, True]]),
    )
    assert not np.any(accumulator.total)


def test_source_points_are_vector_averaged_before_sector_counting() -> None:
    accumulator = WindAccumulator(months=1)
    east = uv_from(10, 90)
    west = uv_from(10, 270)
    u = np.array([[[east[0], west[0]], [east[0], east[0]]]])
    v = np.array([[[east[1], west[1]], [east[1], east[1]]]])
    accumulator.add(
        1,
        u,
        v,
        np.array([0.0, 0.25]),
        np.array([0.0, 0.25]),
    )
    # Four source points form one 0.5-degree cell and therefore one temporal
    # observation, with the mean vector pointing from the east.
    assert accumulator.total[0, 180, 0] == 1
    assert accumulator.direction_count[0, 180, 0, 2] == 1


def test_checkpoint_round_trip(tmp_path) -> None:
    accumulator = WindAccumulator(months=1)
    accumulator.add(
        1,
        np.ones((1, 1, 1)),
        np.ones((1, 1, 1)),
        np.array([0.0]),
        np.array([0.0]),
    )
    path = tmp_path / "wind.npz"
    accumulator.save_checkpoint(path)
    restored = WindAccumulator.load_checkpoint(path)
    np.testing.assert_array_equal(restored.total, accumulator.total)
    np.testing.assert_array_equal(restored.direction_count, accumulator.direction_count)


def test_default_storage_plan_is_well_inside_100gb() -> None:
    plan = era5_wind_plan()
    plan.validate()
    assert plan.budget_bytes == 100 * GB
    assert plan.peak_bytes < 10 * GB


def test_storage_plan_rejects_tiny_budget() -> None:
    plan = era5_wind_plan(budget_gb=0.01)
    try:
        plan.validate()
    except RuntimeError as error:
        assert "exceeds budget" in str(error)
    else:
        raise AssertionError("tiny storage budget should have been rejected")
