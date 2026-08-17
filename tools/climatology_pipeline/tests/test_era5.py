from __future__ import annotations

import numpy as np
import pytest

from climatology_pipeline.era5 import U10, V10, validate_wind_dataset


class Field:
    def __init__(self, dims, units):
        self.dims = dims
        self.attrs = {"units": units}


class Coordinate:
    def __init__(self, values):
        self.values = np.asarray(values)


class Dataset(dict):
    latitude = Coordinate([-90, 0, 90])
    longitude = Coordinate([0, 180])


def test_era5_schema_validation() -> None:
    dataset = Dataset({
        U10: Field(("time", "latitude", "longitude"), "m s**-1"),
        V10: Field(("time", "latitude", "longitude"), "m s**-1"),
        "latitude": object(),
        "longitude": object(),
        "time": object(),
    })
    validate_wind_dataset(dataset)


def test_era5_schema_rejects_wrong_units() -> None:
    dataset = Dataset({
        U10: Field(("time", "latitude", "longitude"), "knots"),
        V10: Field(("time", "latitude", "longitude"), "m s**-1"),
        "latitude": object(),
        "longitude": object(),
        "time": object(),
    })
    with pytest.raises(ValueError, match="units"):
        validate_wind_dataset(dataset)
