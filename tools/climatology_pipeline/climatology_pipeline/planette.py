"""Build monthly ERA5 atmospheric fields from the public Planette archive.

Planette is used as a cloud-native transport for ERA5 monthly aggregates.  It
retains the ERA5/ECMWF metadata and points back to the NSF-NCAR ds633.0 source.
The output remains the historical Climatology wire format.
"""

from __future__ import annotations

import argparse
from pathlib import Path
import sys

import numpy as np

from .legacy import encode_scalar, write_gzip
from .scalar import (MonthlySourceAccumulator, field_for_definition,
                     relative_humidity)


BUCKET = "planette-era5"
GRID = "0p25latx0p25lon"
REGION = "us-east-2"
VARIABLES = {
    "slp": "Pa",
    "t2m": "K",
    "td2m": "K",
    "pr": "kg m**-2 s**-1",
}


def _prefix(variable: str) -> str:
    return (f"era5/{variable}/month/{GRID}/"
            f"era5_{variable}_month_{GRID}.zarr")


def open_monthly(variable: str):
    """Open one public anonymous Icechunk repository lazily."""
    if variable not in VARIABLES:
        raise ValueError(f"unsupported Planette variable {variable!r}")
    try:
        import icechunk as ic
        import xarray as xr
    except ImportError as exc:  # pragma: no cover
        raise RuntimeError("install the pipeline's planette dependencies") from exc
    storage = ic.s3_storage(bucket=BUCKET, prefix=_prefix(variable),
                            region=REGION, anonymous=True)
    repository = ic.Repository.open(storage)
    session = repository.readonly_session("main")
    return xr.open_dataset(session.store, engine="zarr", consolidated=False,
                           decode_timedelta=True, chunks={})


def validate_monthly(dataset, variable: str, start: str, end: str) -> np.ndarray:
    if variable not in dataset:
        raise ValueError(f"monthly ERA5 archive lacks {variable}")
    field = dataset[variable]
    if tuple(field.dims) != ("time", "lat", "lon"):
        raise ValueError(f"{variable} has unexpected dimensions {field.dims}")
    if field.attrs.get("units") != VARIABLES[variable]:
        raise ValueError(f"{variable} has unexpected units {field.attrs.get('units')!r}")
    times = np.asarray(dataset.time.values)
    indices = np.flatnonzero((times >= np.datetime64(start)) &
                             (times <= np.datetime64(end)))
    expected = (np.datetime64(end, "M").astype(int) -
                np.datetime64(start, "M").astype(int) + 1)
    if len(indices) != expected:
        raise ValueError(f"{variable} selection has {len(indices)} months, expected {expected}")
    month_numbers = times[indices].astype("datetime64[M]").astype(np.int64)
    if np.any(np.diff(month_numbers) != 1):
        raise ValueError(f"{variable} monthly time coordinate is discontinuous")
    return indices


def _accumulator(dataset, variable: str, indices: np.ndarray,
                 transform=lambda values: values, *,
                 report: bool = False) -> MonthlySourceAccumulator:
    latitude = np.asarray(dataset.lat.values, dtype=np.float64)
    longitude = np.asarray(dataset.lon.values, dtype=np.float64)
    accumulator = MonthlySourceAccumulator(latitude, longitude)
    times = np.asarray(dataset.time.values)
    for offset in range(0, len(indices), 12):
        block_indices = indices[offset:offset + 12]
        block = np.asarray(dataset[variable].isel(time=block_indices).values,
                           dtype=np.float64)
        block = transform(block)
        months = times[block_indices].astype("datetime64[M]").astype(np.int64) % 12 + 1
        accumulator.add(months, block)
        if report:
            print(f"{variable}: through {times[block_indices[-1]].astype('datetime64[M]')}",
                  flush=True)
    return accumulator


def build(output: Path, *, start: str = "1995-01-01",
          end: str = "2024-12-31", minimum_samples: int = 20) -> None:
    """Build MSLP, air temperature, RH and precipitation legacy files."""
    output.mkdir(parents=True, exist_ok=True)
    opened = {name: open_monthly(name) for name in VARIABLES}
    try:
        indices = {name: validate_monthly(dataset, name, start, end)
                   for name, dataset in opened.items()}
        slp = _accumulator(opened["slp"], "slp", indices["slp"],
                           lambda values: values / 100.0, report=True)
        precipitation = _accumulator(
            opened["pr"], "pr", indices["pr"],
            lambda values: values * 86400.0, report=True,
        )

        # Temperature and dew point share timestamps and coordinates.  Fold
        # them together so t2m is transferred only once.
        temperature = MonthlySourceAccumulator(opened["t2m"].lat.values,
                                               opened["t2m"].lon.values)
        humidity = MonthlySourceAccumulator(opened["t2m"].lat.values,
                                            opened["t2m"].lon.values)
        t_indices = indices["t2m"]
        if not np.array_equal(opened["t2m"].time.values[t_indices],
                              opened["td2m"].time.values[indices["td2m"]]):
            raise ValueError("temperature and dew-point monthly times differ")
        times = np.asarray(opened["t2m"].time.values)
        for offset in range(0, len(t_indices), 12):
            selected = t_indices[offset:offset + 12]
            t2m = np.asarray(opened["t2m"].t2m.isel(time=selected).values,
                             dtype=np.float64)
            td2m = np.asarray(opened["td2m"].td2m.isel(time=selected).values,
                              dtype=np.float64)
            months = times[selected].astype("datetime64[M]").astype(np.int64) % 12 + 1
            temperature.add(months, t2m - 273.15)
            humidity.add(months, relative_humidity(t2m, td2m))
            print(f"t2m/td2m: through {times[selected[-1]].astype('datetime64[M]')}",
                  flush=True)

        products = {
            "sealevelpressure": slp,
            "airtemperature": temperature,
            "relativehumidity": humidity,
            "precipitation": precipitation,
        }
        for name, accumulator in products.items():
            physical = accumulator.monthly(minimum_samples=minimum_samples)
            encoded = field_for_definition(
                name, physical, accumulator.latitude, accumulator.longitude,
                out_of_range="missing" if name == "airtemperature" else "error",
            )
            write_gzip(output / f"{name}.gz", encode_scalar(encoded, name))
    finally:
        for dataset in opened.values():
            dataset.close()


def main(argv: list[str] | None = None) -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("output", type=Path)
    parser.add_argument("--start", default="1995-01-01")
    parser.add_argument("--end", default="2024-12-31")
    parser.add_argument("--minimum-samples", type=int, default=20)
    args = parser.parse_args(argv)
    build(args.output, start=args.start, end=args.end,
          minimum_samples=args.minimum_samples)


if __name__ == "__main__":
    main(sys.argv[1:])
