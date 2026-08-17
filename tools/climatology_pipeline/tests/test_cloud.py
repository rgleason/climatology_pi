from __future__ import annotations

import numpy as np
import pytest
import xarray as xr

from climatology_pipeline.cloud import build_cloud
from climatology_pipeline.scalar import decode_field


def test_monthly_cloud_build_and_units(tmp_path) -> None:
    times = np.arange("2000-01", "2002-01", dtype="datetime64[M]").astype("datetime64[ns]")
    pattern = np.array([[0., .25], [.75, 1.]], dtype=np.float32)
    values = np.broadcast_to(pattern, (24, 2, 2)).copy()
    dataset = xr.Dataset(
        {"tcc": (("time", "latitude", "longitude"), values,
                 {"units": "fraction"})},
        coords={"time": times, "latitude": [89.5, -89.5],
                "longitude": [.5, 180.5]},
    )
    source = tmp_path / "cloud.nc"
    dataset.to_netcdf(source)
    encoded = build_cloud(str(source), start="2000-01-01", end="2001-12-31",
                          minimum_samples=2)
    physical = decode_field("cloud", encoded)
    assert np.nanmin(physical) == pytest.approx(0.)
    assert np.nanmax(physical) == pytest.approx(100.)


def test_cloud_rejects_wrong_units(tmp_path) -> None:
    times = np.arange("2000-01", "2002-01", dtype="datetime64[M]").astype("datetime64[ns]")
    dataset = xr.Dataset(
        {"tcc": (("time", "lat", "lon"), np.ones((24, 2, 2)),
                 {"units": "percent"})},
        coords={"time": times, "lat": [1., -1.], "lon": [0., 180.]},
    )
    source = tmp_path / "wrong.nc"
    dataset.to_netcdf(source)
    with pytest.raises(ValueError, match="unexpected total-cloud units"):
        build_cloud(str(source), start="2000-01-01", end="2001-12-31")
