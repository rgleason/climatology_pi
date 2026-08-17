"""Streaming ERA5 adapters for the Climatology wind/statistical fields.

The preferred source is ECMWF's geo-chunked ARCO store because its layout is
suited to long climatological time series.  It requires a CDS API token.  The
public WeatherBench2 six-hour store is a slower, no-credentials fallback for
1995--2023; the current Google ARCO store supplies 2024 when needed.
"""

from __future__ import annotations

from dataclasses import dataclass
from datetime import datetime
import os
from pathlib import Path
from typing import Any, Callable, Iterator

import numpy as np

from .budget import era5_wind_plan
from .wind import WindAccumulator


WEATHERBENCH_6H = (
    "gs://weatherbench2/datasets/era5/"
    "1959-2023_01_10-wb13-6h-1440x721_with_derived_variables.zarr"
)
GOOGLE_ARCO_HOURLY = (
    "gs://gcp-public-data-arco-era5/ar/"
    "full_37-1h-0p25deg-chunk-1.zarr-v3"
)
ECMWF_ARCO_GEO = (
    "https://arco.datastores.ecmwf.int/cadl-arco-geo-002/arco/"
    "reanalysis_era5_single_levels/sfc/geoChunked.zarr"
)
U10 = "10m_u_component_of_wind"
V10 = "10m_v_component_of_wind"
LSM = "land_sea_mask"


@dataclass(frozen=True)
class Era5Source:
    identifier: str
    url: str
    cadence_hours: int
    authenticated: bool = False


def open_source(source: Era5Source, *, cds_token: str | None = None) -> Any:
    """Open a supported Zarr source lazily; no field data are downloaded."""
    try:
        import xarray as xr
    except ImportError as exc:  # pragma: no cover - exercised in lean installs
        raise RuntimeError("install the pipeline's 'source' dependencies") from exc
    if source.authenticated:
        token = cds_token or os.environ.get("CDS_API_KEY")
        if not token:
            raise RuntimeError("ECMWF geo-chunked ARCO access requires CDS_API_KEY")
        storage_options = {"headers": {"Authorization": f"Bearer {token}"}}
    elif source.url.startswith("gs://"):
        storage_options = {"token": "anon"}
    else:
        storage_options = {}
    return xr.open_zarr(
        source.url,
        chunks={},
        consolidated=True,
        storage_options=storage_options,
    )


def validate_wind_dataset(dataset: Any) -> None:
    required = (U10, V10, "latitude", "longitude", "time")
    missing = [name for name in required if name not in dataset]
    if missing:
        raise ValueError(f"ERA5 source lacks required fields: {', '.join(missing)}")
    for name in (U10, V10):
        field = dataset[name]
        if tuple(field.dims) != ("time", "latitude", "longitude"):
            raise ValueError(f"{name} has unexpected dimensions {field.dims}")
        if field.attrs.get("units") not in {"m s**-1", "m s^-1", "m/s"}:
            raise ValueError(f"{name} has unexpected units {field.attrs.get('units')!r}")
    latitude = np.asarray(dataset.latitude.values)
    longitude = np.asarray(dataset.longitude.values)
    if latitude.ndim != 1 or longitude.ndim != 1:
        raise ValueError("ERA5 latitude/longitude coordinates must be one-dimensional")
    if not (np.all(np.diff(latitude) > 0) or np.all(np.diff(latitude) < 0)):
        raise ValueError("ERA5 latitude coordinate is not monotonic")


def _sea_mask(dataset: Any, time_index: int) -> np.ndarray:
    if LSM in dataset:
        mask = dataset[LSM]
        if "time" in mask.dims:
            mask = mask.isel(time=time_index)
        values = np.asarray(mask.compute().values, dtype=np.float64)
        sea = np.isfinite(values) & (values < 0.5)
    elif "sea_surface_temperature" in dataset:
        values = np.asarray(
            dataset["sea_surface_temperature"].isel(time=time_index).compute().values
        )
        sea = np.isfinite(values)
    else:
        raise ValueError("ERA5 source provides neither land_sea_mask nor sea_surface_temperature")
    if not np.any(sea):
        raise ValueError("ERA5 sea mask contains no valid ocean cells at the selected date")
    return sea


def _time_indices(dataset: Any, start: str, end: str, cadence_hours: int) -> np.ndarray:
    times = np.asarray(dataset.time.values)
    start64 = np.datetime64(start)
    end64 = np.datetime64(end)
    selected = np.flatnonzero((times >= start64) & (times <= end64))
    if cadence_hours > 1:
        hours = times[selected].astype("datetime64[h]").astype(np.int64) % 24
        selected = selected[hours % cadence_hours == 0]
    if not len(selected):
        raise ValueError(f"ERA5 source has no samples in {start} .. {end}")
    return selected


def accumulate_wind(
    dataset: Any,
    accumulator: WindAccumulator,
    *,
    start: str,
    end: str,
    cadence_hours: int = 6,
    block_samples: int = 12,
    workers: int = 8,
    extra_variables: tuple[str, ...] = (),
    block_callback: Callable[[Any, np.ndarray], None] | None = None,
) -> Iterator[datetime]:
    """Stream wind blocks and yield the final timestamp of each checkpoint block."""
    validate_wind_dataset(dataset)
    era5_wind_plan().validate()
    indices = _time_indices(dataset, start, end, cadence_hours)
    latitudes = np.asarray(dataset.latitude.values, dtype=np.float64)
    longitudes = np.asarray(dataset.longitude.values, dtype=np.float64)
    # Some long ARCO stores have coordinate coverage before a variable's first
    # populated timestep.  Select the first requested sample, not array index
    # zero, or an all-NaN historical mask can silently reject the whole period.
    sea = _sea_mask(dataset, int(indices[0]))
    if sea.shape != (len(latitudes), len(longitudes)):
        raise ValueError("ERA5 land/sea mask does not match wind grid")

    for offset in range(0, len(indices), block_samples):
        block_indices = indices[offset : offset + block_samples]
        selected_variables = [U10, V10, *extra_variables]
        block = dataset[selected_variables].isel(time=block_indices).compute(
            scheduler="threads", num_workers=workers
        )
        times = np.asarray(block.time.values)
        months = times.astype("datetime64[M]").astype(np.int64) % 12 + 1
        for month in np.unique(months):
            selected = months == month
            accumulator.add(
                int(month),
                np.asarray(block[U10].values[selected]),
                np.asarray(block[V10].values[selected]),
                latitudes,
                longitudes,
                sea_mask=sea,
            )
        if block_callback is not None:
            block_callback(block, sea)
        yield times[-1].astype("datetime64[s]").astype(datetime)


def weatherbench_source() -> Era5Source:
    return Era5Source("WeatherBench2 ERA5 6-hour 0.25 degree", WEATHERBENCH_6H, 6)


def google_arco_source() -> Era5Source:
    return Era5Source("Google ARCO-ERA5 hourly 0.25 degree", GOOGLE_ARCO_HOURLY, 1)


def ecmwf_geo_source() -> Era5Source:
    return Era5Source("ECMWF ERA5 ARCO geo-chunked", ECMWF_ARCO_GEO, 1, True)
