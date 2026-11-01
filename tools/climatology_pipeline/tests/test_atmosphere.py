from __future__ import annotations

from pathlib import Path

import numpy as np
import pytest

xr = pytest.importorskip("xarray")

from climatology_pipeline.atmosphere import AtmosphereAccumulator
from climatology_pipeline.scalar import decode_field


def sample_dataset() -> xr.Dataset:
    shape = (2, 3, 4)
    variables = {
        "2m_temperature": (("time", "latitude", "longitude"), np.full(shape, 293.15), {"units": "K"}),
        "2m_dewpoint_temperature": (("time", "latitude", "longitude"), np.full(shape, 293.15), {"units": "K"}),
        "mean_sea_level_pressure": (("time", "latitude", "longitude"), np.full(shape, 101325.), {"units": "Pa"}),
        "total_cloud_cover": (("time", "latitude", "longitude"), np.full(shape, .4), {"units": "(0 - 1)"}),
        "total_precipitation_6hr": (("time", "latitude", "longitude"), np.full(shape, .001), {"units": "m"}),
    }
    return xr.Dataset(variables, coords={
        "time": np.array(["2001-01-01", "2001-01-01T06:00"], dtype="datetime64[ns]"),
        "latitude": [90., 0., -90.], "longitude": [0., 90., 180., 270.],
    })


def test_atmosphere_physical_conversions_and_sea_mask() -> None:
    accumulator = AtmosphereAccumulator()
    sea = np.ones((3, 4), dtype=bool)
    sea[:, 1] = False
    accumulator.add_block(sample_dataset(), sea)
    expected = {"sealevelpressure": 1013.25, "airtemperature": 20.,
                "cloud": 40., "precipitation": 4., "relativehumidity": 100.}
    for name, value in expected.items():
        physical = accumulator.fields[name].physical()
        assert np.nanmedian(physical[0]) == pytest.approx(value, abs=.01)
        assert np.count_nonzero(np.isfinite(physical[0])) > 0


def test_atmosphere_checkpoint(tmp_path: Path) -> None:
    accumulator = AtmosphereAccumulator()
    accumulator.add_block(sample_dataset(), np.ones((3, 4), dtype=bool))
    path = tmp_path / "atmosphere.npz"
    accumulator.save_checkpoint(path)
    restored = AtmosphereAccumulator.load_checkpoint(path)
    for name in accumulator.fields:
        np.testing.assert_allclose(restored.fields[name].total,
                                   accumulator.fields[name].total)
