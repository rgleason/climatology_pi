from __future__ import annotations

import json
from pathlib import Path

import numpy as np
import pytest

xr = pytest.importorskip("xarray")

from climatology_pipeline.oscar import _load_progress, _write_progress, ingest_oscar_files
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


def test_oscar_accepts_official_julian_calendar(tmp_path: Path) -> None:
    dataset = xr.Dataset(
        {"u": (("time", "longitude", "latitude"), [[[1.0]]], {"units": "m s-1"}),
         "v": (("time", "longitude", "latitude"), [[[-2.0]]], {"units": "m s-1"}),
         "lat": (("latitude",), [0.0]),
         "lon": (("longitude",), [0.0])},
        coords={"time": np.array(["1995-01-01"], dtype="datetime64[ns]"),
                "latitude": [0.0], "longitude": [0.0]},
    )
    path = tmp_path / "oscar-julian.nc"
    dataset.to_netcdf(
        path,
        encoding={"time": {"calendar": "julian", "units": "days since 1990-01-01"}},
    )
    result = ingest_oscar_files([path], start="1995-01-01", end="1995-01-01")
    np.testing.assert_allclose(result.monthly_source(1)[0], 1.0)
    np.testing.assert_allclose(result.monthly_source(1)[1], -2.0)


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


def test_oscar_progress_is_bound_to_source_and_period(tmp_path: Path) -> None:
    progress = tmp_path / "oscar.json"
    _write_progress(
        progress, start="1995-01-01", end="2022-08-05",
        completed="1995-01-31",
    )
    state = json.loads(progress.read_text(encoding="utf-8"))
    assert _load_progress(
        state, start="1995-01-01", end="2022-08-05"
    ) == "1995-01-31"
    with pytest.raises(RuntimeError, match="period or source"):
        _load_progress(state, start="2000-01-01", end="2022-08-05")


def test_oisst_monthly_climatology(tmp_path: Path) -> None:
    times = np.arange("2000-01", "2002-01", dtype="datetime64[M]").astype("datetime64[ns]")
    months = np.arange(len(times)) % 12
    latitude_pattern = np.array([-2., 20.], dtype=np.float32)[None, :, None]
    longitude_pattern = np.array([0., 2., 5., 10.], dtype=np.float32)[None, None, :]
    values = np.broadcast_to(latitude_pattern + longitude_pattern +
                             months[:, None, None] / 20., (24, 2, 4)).copy()
    dataset = xr.Dataset(
        {"sst": (("time", "lat", "lon"), values, {"units": "degC"})},
        coords={"time": times, "lat": [-89.5, 89.5], "lon": [.5, 90.5, 180.5, 270.5]},
    )
    path = tmp_path / "oisst.nc"
    dataset.to_netcdf(path)
    encoded = build_oisst(str(path), start="2000-01-01", end="2001-12-31")
    decoded = decode_field("seasurfacetemperature", encoded)
    assert np.nanmin(decoded) <= -1.8
    assert np.nanmax(decoded) >= 29.0


def test_oisst_rejects_discontinuous_months(tmp_path: Path) -> None:
    times = np.arange("2000-01", "2002-02", dtype="datetime64[M]").astype("datetime64[ns]")
    times = np.delete(times, 6)
    values = np.broadcast_to(np.array([[-2., 30.], [10., 20.]])[None],
                             (24, 2, 2)).copy()
    dataset = xr.Dataset(
        {"sst": (("time", "lat", "lon"), values, {"units": "degC"})},
        coords={"time": times, "lat": [-89.5, 89.5], "lon": [.5, 180.5]},
    )
    path = tmp_path / "gap.nc"
    dataset.to_netcdf(path)
    with pytest.raises(ValueError, match="discontinuous"):
        build_oisst(str(path), start="2000-01-01", end="2002-01-31")
