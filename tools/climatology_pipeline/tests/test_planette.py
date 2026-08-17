from __future__ import annotations

import numpy as np
import pytest
import xarray as xr

from climatology_pipeline.planette import _accumulator, validate_monthly
from climatology_pipeline.scalar import SCALAR_DEFINITIONS, encode_field


def monthly_dataset(variable: str = "slp", *, units: str = "Pa") -> xr.Dataset:
    times = np.arange("2000-01", "2002-01", dtype="datetime64[M]").astype("datetime64[ns]")
    values = np.arange(24, dtype=np.float32)[:, None, None] + np.ones((24, 2, 2))
    return xr.Dataset(
        {variable: (("time", "lat", "lon"), values, {"units": units})},
        coords={"time": times, "lat": [1., -1.], "lon": [-1., 1.]},
    )


def test_planette_monthly_validation_and_accumulation() -> None:
    dataset = monthly_dataset()
    indices = validate_monthly(dataset, "slp", "2000-01-01", "2001-12-31")
    accumulated = _accumulator(dataset, "slp", indices)
    monthly = accumulated.monthly(minimum_samples=2)
    np.testing.assert_allclose(monthly[:, 0, 0], 7. + np.arange(12))


def test_planette_rejects_units_and_time_gaps() -> None:
    wrong = monthly_dataset(units="hPa")
    with pytest.raises(ValueError, match="unexpected units"):
        validate_monthly(wrong, "slp", "2000-01-01", "2001-12-31")
    gap = monthly_dataset().isel(time=[index for index in range(24) if index != 5])
    with pytest.raises(ValueError, match="23 months, expected 24"):
        validate_monthly(gap, "slp", "2000-01-01", "2001-12-31")


def test_air_temperature_can_mark_unrepresentable_cells_missing() -> None:
    definition = SCALAR_DEFINITIONS["airtemperature"]
    values = np.zeros(definition.shape)
    values[0, 0, 0] = -60.0
    with pytest.raises(ValueError, match="exceeds legacy representation"):
        encode_field("airtemperature", values)
    encoded = encode_field("airtemperature", values, out_of_range="missing")
    assert encoded[0, 0, 0] == definition.missing
    assert encoded[0, 0, 1] == 0
