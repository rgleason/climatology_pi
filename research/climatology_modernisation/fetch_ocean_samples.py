"""Fetch bounded, downsampled NOAA OISST and GODAS research arrays.

The source archives remain at NOAA.  OPeNDAP hyperslabs avoid downloading the
2.1 GiB OISST file or all 40 GODAS depth levels.  The derived cache is research
input, not a redistributable plugin product.
"""

from __future__ import annotations

import argparse
from datetime import datetime, timezone
import json
from pathlib import Path

from netCDF4 import Dataset, num2date
import numpy as np


OISST = "https://psl.noaa.gov/thredds/dodsC/Datasets/noaa.oisst.v2.highres/sst.mon.mean.nc"
GODAS = "https://psl.noaa.gov/thredds/dodsC/Datasets/godas/{component}.{year}.nc"


def validate_oisst(sst: np.ndarray) -> None:
    finite = sst[np.isfinite(sst)]
    if (finite.size < sst.size * 0.5 or finite.min(initial=np.inf) < -3
            or finite.max(initial=-np.inf) > 40
            or finite.max(initial=-np.inf) - finite.min(initial=np.inf) < 10):
        raise ValueError("OISST cache failed physical-range/coverage validation")


def fetch_oisst(output: Path) -> dict[str, object]:
    target = output / "oisst-monthly-2deg.npz"
    if target.exists():
        return {"path": target.name, "status": "reused"}
    with Dataset(OISST) as source:
        time_var = source.variables["time"]
        dates = num2date(time_var[:], time_var.units,
                        only_use_cftime_datetimes=False,
                        only_use_python_datetimes=True)
        selected = np.array([1981 <= value.year <= 2025 for value in dates])
        indices = np.flatnonzero(selected)
        if not len(indices) or not np.all(np.diff(indices) == 1):
            raise ValueError("OISST research interval must be one contiguous time slice")
        # Native 0.25 degree -> 2 degree.  Keep actual source samples rather
        # than pretending this is an area-conservative production regrid.
        # netCDF4's remote OPeNDAP path can silently return a fully masked
        # result for an integer-array time index.  A basic contiguous slice is
        # both smaller on the wire and reliable.
        # Keep each DAP response small.  The server has been observed to
        # return an unmasked all-zero array for one large multi-decade
        # hyperslab while correctly serving the same cells in annual blocks.
        blocks = []
        for begin in range(indices[0], indices[-1] + 1, 12):
            end = min(begin + 12, indices[-1] + 1)
            remote = source.variables["sst"][begin:end, ::8, ::8]
            blocks.append(np.asarray(np.ma.filled(remote, np.nan), dtype=np.float32))
        sst = np.concatenate(blocks, axis=0)
        sst = np.where(np.abs(sst) > 100, np.nan, sst)
        lat = np.asarray(source.variables["lat"][::8], dtype=np.float32)
        lon = np.asarray(source.variables["lon"][::8], dtype=np.float32)
        years = np.array([dates[index].year for index in indices], dtype=np.int16)
        months = np.array([dates[index].month for index in indices], dtype=np.int8)
    validate_oisst(sst)
    np.savez_compressed(target, sst=sst, lat=lat, lon=lon, year=years, month=months)
    return {"path": target.name, "status": "downloaded", "source": OISST,
            "source_resolution_degrees": 0.25, "research_stride": 8}


def fetch_godas(output: Path, first_year: int, last_year: int) -> dict[str, object]:
    target = output / f"godas-current-1deg-{first_year}-{last_year}.npz"
    if target.exists():
        return {"path": target.name, "status": "reused"}
    components: dict[str, list[np.ndarray]] = {"ucur": [], "vcur": []}
    years: list[int] = []
    months: list[int] = []
    lat = lon = None
    for year in range(first_year, last_year + 1):
        print(f"GODAS {year}", flush=True)
        year_dates = None
        for component in ("ucur", "vcur"):
            with Dataset(GODAS.format(component=component, year=year)) as source:
                if lat is None:
                    lat = np.asarray(source.variables["lat"][::3], dtype=np.float32)
                    lon = np.asarray(source.variables["lon"][::3], dtype=np.float32)
                values = np.asarray(source.variables[component][:, 0, ::3, ::3], dtype=np.float32)
                values = np.where(np.abs(values) > 100, np.nan, values)
                components[component].append(values)
                if year_dates is None:
                    time_var = source.variables["time"]
                    year_dates = num2date(time_var[:], time_var.units,
                                         only_use_cftime_datetimes=False,
                                         only_use_python_datetimes=True)
        assert year_dates is not None
        years.extend(value.year for value in year_dates)
        months.extend(value.month for value in year_dates)
    np.savez_compressed(target, u=np.concatenate(components["ucur"]),
                        v=np.concatenate(components["vcur"]), lat=lat, lon=lon,
                        year=np.asarray(years, dtype=np.int16),
                        month=np.asarray(months, dtype=np.int8))
    return {"path": target.name, "status": "downloaded",
            "source": GODAS, "depth_metres": 5,
            "native_grid": "1 degree longitude x 1/3 degree latitude",
            "research_stride": [3, 3]}


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--cache", type=Path, default=Path("cache"))
    parser.add_argument("--first-year", type=int, default=1981)
    parser.add_argument("--last-year", type=int, default=2025)
    args = parser.parse_args()
    args.cache.mkdir(parents=True, exist_ok=True)
    manifest = {
        "schema": 1,
        "retrieved_utc": datetime.now(timezone.utc).isoformat(),
        "oisst": fetch_oisst(args.cache),
        "godas": fetch_godas(args.cache, args.first_year, args.last_year),
    }
    (args.cache / "ocean-sample-manifest.json").write_text(
        json.dumps(manifest, indent=2, sort_keys=True) + "\n", encoding="utf-8")


if __name__ == "__main__":
    main()
