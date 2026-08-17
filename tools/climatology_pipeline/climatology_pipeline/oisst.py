"""NOAA OISST v2.1 monthly climatology ingestion."""

from __future__ import annotations

import argparse
from pathlib import Path
import sys

import numpy as np

from .legacy import encode_scalar, write_gzip
from .scalar import SCALAR_DEFINITIONS, encode_field, resample_regular


OISST_MONTHLY_OPENDAP = (
    "https://psl.noaa.gov/thredds/dodsC/Datasets/"
    "noaa.oisst.v2.highres/sst.mon.mean.nc"
)
OISST_MONTHLY_HTTP = (
    "https://psl.noaa.gov/thredds/fileServer/Datasets/"
    "noaa.oisst.v2.highres/sst.mon.mean.nc"
)


def build_oisst(source: str, *, start: str = "1995-01-01",
                end: str = "2024-12-31") -> np.ndarray:
    try:
        import xarray as xr
    except ImportError as exc:  # pragma: no cover
        raise RuntimeError("install the pipeline's source dependencies") from exc
    # Do not use a dask groupby against the DAP2 endpoint: netCDF4's remote
    # fancy-index path has returned silent all-zero reductions in practice.
    # Contiguous one-year slabs are bounded (~50 MB) and independently summed.
    with xr.open_dataset(source) as dataset:
        if "sst" not in dataset:
            raise ValueError("OISST dataset lacks sst")
        units = dataset.sst.attrs.get("units", "")
        if units not in {"degC", "degree_Celsius", "degrees C", "Celsius"}:
            raise ValueError(f"unexpected OISST units {units!r}")
        times = np.asarray(dataset.time.values)
        indices = np.flatnonzero((times >= np.datetime64(start)) &
                                 (times <= np.datetime64(end)))
        if len(indices) < 12:
            raise ValueError("OISST selection contains less than one year")
        selected_months = times[indices].astype("datetime64[M]").astype(np.int64)
        if np.any(np.diff(selected_months) != 1):
            raise ValueError("OISST time selection is unexpectedly discontinuous")
        shape = (12, dataset.sizes["lat"], dataset.sizes["lon"])
        total = np.zeros(shape, dtype=np.float64)
        count = np.zeros(shape, dtype=np.uint16)
        for offset in range(0, len(indices), 12):
            block_indices = indices[offset:offset + 12]
            time_slice = slice(int(block_indices[0]), int(block_indices[-1]) + 1)
            block = np.asarray(dataset.sst.isel(time=time_slice).values,
                               dtype=np.float64)
            months = times[block_indices].astype("datetime64[M]").astype(np.int64) % 12
            for position, month in enumerate(months):
                valid = np.isfinite(block[position])
                total[month] += np.where(valid, block[position], 0.0)
                count[month] += valid
        monthly = np.divide(total, count, out=np.full(shape, np.nan), where=count > 0)
        if np.any(np.count_nonzero(count, axis=(1, 2)) == 0):
            raise ValueError("OISST selection does not contain all calendar months")
        # Detect a known DAP2/netCDF4 failure mode which silently returns zero
        # for full-grid slabs even though point queries are correct.
        if np.nanmin(monthly) > -1.0 or np.nanmax(monthly) < 25.0:
            raise ValueError(
                "OISST values fail global physical-range checks; use the official "
                f"HTTP NetCDF file ({OISST_MONTHLY_HTTP}) instead of this DAP endpoint"
            )
        latitude = np.asarray(dataset.lat.values)
        longitude = np.asarray(dataset.lon.values)
    definition = SCALAR_DEFINITIONS["seasurfacetemperature"]
    physical = resample_regular(monthly, latitude, longitude,
                                definition.latitude, definition.longitude)
    return encode_field("seasurfacetemperature", physical)


def main(argv: list[str] | None = None) -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("output", type=Path)
    parser.add_argument("--source", default=OISST_MONTHLY_OPENDAP)
    parser.add_argument("--start", default="1995-01-01")
    parser.add_argument("--end", default="2024-12-31")
    args = parser.parse_args(argv)
    encoded = build_oisst(args.source, start=args.start, end=args.end)
    write_gzip(args.output, encode_scalar(encoded, "seasurfacetemperature"))


if __name__ == "__main__":
    main(sys.argv[1:])
