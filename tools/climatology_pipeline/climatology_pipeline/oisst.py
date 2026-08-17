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


def build_oisst(source: str, *, start: str = "1995-01-01",
                end: str = "2024-12-31") -> np.ndarray:
    try:
        import xarray as xr
    except ImportError as exc:  # pragma: no cover
        raise RuntimeError("install the pipeline's source dependencies") from exc
    with xr.open_dataset(source, chunks={"time": 12}) as dataset:
        if "sst" not in dataset:
            raise ValueError("OISST dataset lacks sst")
        units = dataset.sst.attrs.get("units", "")
        if units not in {"degC", "degree_Celsius", "degrees C", "Celsius"}:
            raise ValueError(f"unexpected OISST units {units!r}")
        selected = dataset.sst.sel(time=slice(start, end))
        if selected.sizes.get("time", 0) < 12:
            raise ValueError("OISST selection contains less than one year")
        monthly = selected.groupby("time.month").mean("time", skipna=True).compute().values
        if monthly.shape[0] != 12:
            raise ValueError("OISST selection does not contain all calendar months")
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
