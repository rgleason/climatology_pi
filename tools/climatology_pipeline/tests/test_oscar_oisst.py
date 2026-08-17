from __future__ import annotations

from pathlib import Path

import numpy as np
import pytest

xr = pytest.importorskip("xarray")

from climatology_pipeline.oscar import ingest_oscar_files
from climatology_pipeline.oisst import build_oisst
from climatology_pipeline.scalar import decode_field


def test_oscar_transposes_official_lon_lat_layout(tmp_path: Path) -> None:
    times = np.array(["2001-01-01", "2001-02-01"], dtype="datetime64[ns]")
    data = np.zeros((2, 2, 3), dtype=np.float32)  # time, lon, lat
    data[0] = 1.0
    data[1] = 2.0
    dataset = xr.Dataset(
        {"u": (("time", "lon", "lat"), data, {"units": "m s-1"}),
         "v": (("time", "lon", "lat"), -data, {"units": "m s-1"})},
        coords={"time": times, "lat": [-80., 0., 80.], "lon": [0., 180.]},
    )
    path = tmp_path / "oscar.nc"
    dataset.to_netcdf(path)
    result = ingest_oscar_files([path], start="2001-01-01", end="2001-12-31")
    assert np.all(result.count[0] == 1)
    assert np.all(result.count[1] == 1)
    np.testing.assert_allclose(result.monthly_source(1)[0], 1.0)
    np.testing.assert_allclose(result.monthly_source(2)[1], -2.0)


def test_oscar_rejects_wrong_units(tmp_path: Path) -> None:
    dataset = xr.Dataset(
        {"u": (("time", "lat", "lon"), [[[1.]]], {"units": "knots"}),
         "v": (("time", "lat", "lon"), [[[1.]]], {"units": "knots"})},
        coords={"time": np.array(["2001-01-01"], dtype="datetime64[ns]"),
                "lat": [0.], "lon": [0.]},
    )
    path = tmp_path / "bad.nc"
    dataset.to_netcdf(path)
    with pytest.raises(ValueError, match="unexpected u units"):
        ingest_oscar_files([path])


def test_oisst_monthly_climatology(tmp_path: Path) -> None:
    times = np.arange("2000-01", "2002-01", dtype="datetime64[M]").astype("datetime64[ns]")
    months = np.arange(len(times)) % 12
    values = np.broadcast_to((15 + months[:, None, None]).astype(np.float32), (24, 2, 4)).copy()
    dataset = xr.Dataset(
        {"sst": (("time", "lat", "lon"), values, {"units": "degC"})},
        coords={"time": times, "lat": [-89.5, 89.5], "lon": [.5, 90.5, 180.5, 270.5]},
    )
    path = tmp_path / "oisst.nc"
    dataset.to_netcdf(path)
    encoded = build_oisst(str(path), start="2000-01-01", end="2001-12-31")
    decoded = decode_field("seasurfacetemperature", encoded)
    np.testing.assert_allclose(decoded[:, 90, 0], 15 + np.arange(12), atol=.11)
