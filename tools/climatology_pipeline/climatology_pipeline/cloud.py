"""Build the legacy cloud layer from ERA5 monthly total cloud cover."""

from __future__ import annotations

import argparse
from pathlib import Path
import sys

import numpy as np

from .legacy import encode_scalar, write_gzip
from .scalar import MonthlySourceAccumulator, field_for_definition


NCAR_ERA5_MONTHLY_DAP = (
    "https://tds.gdex.ucar.edu/thredds/dodsC/"
    "aggregations/g/d633001/2/TP"
)
NCAR_TCC = "Total_cloud_cover_surface_Mixed_intervals_Average"


def build_cloud(source: str = NCAR_ERA5_MONTHLY_DAP, *,
                start: str = "1995-01-01", end: str = "2022-12-31",
                minimum_samples: int = 20) -> np.ndarray:
    try:
        import xarray as xr
    except ImportError as exc:  # pragma: no cover
        raise RuntimeError("install the pipeline's source dependencies") from exc
    with xr.open_dataset(source) as dataset:
        name = NCAR_TCC if NCAR_TCC in dataset else "tcc"
        if name not in dataset:
            raise ValueError("ERA5 monthly source lacks total cloud cover")
        field = dataset[name]
        time_name = "time1" if "time1" in field.dims else "time"
        lat_name = "lat" if "lat" in field.dims else "latitude"
        lon_name = "lon" if "lon" in field.dims else "longitude"
        if tuple(field.dims) != (time_name, lat_name, lon_name):
            raise ValueError(f"total cloud cover has unexpected dimensions {field.dims}")
        if field.attrs.get("units") not in {"0 - 1", "1", "fraction"}:
            raise ValueError(f"unexpected total-cloud units {field.attrs.get('units')!r}")
        times = np.asarray(dataset[time_name].values)
        indices = np.flatnonzero((times >= np.datetime64(start)) &
                                 (times <= np.datetime64(end)))
        expected = (np.datetime64(end, "M").astype(int) -
                    np.datetime64(start, "M").astype(int) + 1)
        if len(indices) != expected:
            raise ValueError(f"cloud selection has {len(indices)} months, expected {expected}")
        numeric_months = times[indices].astype("datetime64[M]").astype(np.int64)
        if np.any(np.diff(numeric_months) != 1):
            raise ValueError("cloud monthly time coordinate is discontinuous")
        accumulator = MonthlySourceAccumulator(dataset[lat_name].values,
                                               dataset[lon_name].values)
        for offset in range(0, len(indices), 12):
            selected = indices[offset:offset + 12]
            block = np.asarray(field.isel({time_name: selected}).values,
                               dtype=np.float64)
            months = times[selected].astype("datetime64[M]").astype(np.int64) % 12 + 1
            accumulator.add(months, block)
            print(f"cloud: through {times[selected[-1]].astype('datetime64[M]')}",
                  flush=True)
    monthly = accumulator.monthly(minimum_samples=minimum_samples)
    if np.nanmin(monthly) < -0.001 or np.nanmax(monthly) > 1.001:
        raise ValueError("total cloud cover is outside the physical 0..1 range")
    if np.nanmax(monthly) < 0.75:
        raise ValueError("total cloud cover fails a global range sanity check")
    return field_for_definition("cloud", monthly * 100.0,
                                accumulator.latitude, accumulator.longitude)


def main(argv: list[str] | None = None) -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("output", type=Path)
    parser.add_argument("--source", default=NCAR_ERA5_MONTHLY_DAP)
    parser.add_argument("--start", default="1995-01-01")
    parser.add_argument("--end", default="2022-12-31")
    parser.add_argument("--minimum-samples", type=int, default=20)
    args = parser.parse_args(argv)
    encoded = build_cloud(args.source, start=args.start, end=args.end,
                          minimum_samples=args.minimum_samples)
    write_gzip(args.output, encode_scalar(encoded, "cloud"))


if __name__ == "__main__":
    main(sys.argv[1:])
