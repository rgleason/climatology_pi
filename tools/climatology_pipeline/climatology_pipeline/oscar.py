"""OSCAR Final V2 local-file ingestion and legacy current output."""

from __future__ import annotations

import argparse
from datetime import date, timedelta
from pathlib import Path
import sys
import tempfile

import numpy as np

from .current import CurrentAccumulator
from .legacy import decode_current, encode_current, write_gzip


def _coordinate(dataset, *names: str) -> str:
    for name in names:
        if name in dataset.coords or name in dataset.variables:
            return name
    raise ValueError(f"none of the required coordinates exists: {', '.join(names)}")


def ingest_oscar_files(paths: list[Path], *, start: str = "1995-01-01",
                       end: str = "2022-08-05",
                       accumulator: CurrentAccumulator | None = None) -> CurrentAccumulator:
    """Read validated OSCAR NetCDF granules without retaining raw archives."""
    try:
        import xarray as xr
    except ImportError as exc:  # pragma: no cover
        raise RuntimeError("install the pipeline's source dependencies") from exc
    start64, end64 = np.datetime64(start), np.datetime64(end)
    for path in sorted(paths):
        with xr.open_dataset(path) as dataset:
            lat_name = _coordinate(dataset, "lat", "latitude")
            lon_name = _coordinate(dataset, "lon", "longitude")
            time_name = _coordinate(dataset, "time")
            for variable in ("u", "v"):
                if variable not in dataset:
                    raise ValueError(f"{path}: missing {variable}")
                units = dataset[variable].attrs.get("units", "")
                if units not in {"m s-1", "m s^-1", "m/s", "meter second-1", "meters per second"}:
                    raise ValueError(f"{path}: unexpected {variable} units {units!r}")
            latitude = np.asarray(dataset[lat_name].values, dtype=np.float64)
            longitude = np.asarray(dataset[lon_name].values, dtype=np.float64)
            if accumulator is None:
                accumulator = CurrentAccumulator(latitude, longitude)
            elif (not np.array_equal(accumulator.latitudes, latitude) or
                  not np.array_equal(accumulator.longitudes, np.mod(longitude, 360.0))):
                raise ValueError(f"{path}: OSCAR grid changed within input sequence")
            times = np.asarray(dataset[time_name].values)
            selected = np.flatnonzero((times >= start64) & (times <= end64))
            if not len(selected):
                continue
            for month in np.unique(times[selected].astype("datetime64[M]").astype(np.int64) % 12 + 1):
                indices = selected[(times[selected].astype("datetime64[M]").astype(np.int64) % 12 + 1) == month]
                u = dataset["u"].isel({time_name: indices}).transpose(time_name, lat_name, lon_name).values
                v = dataset["v"].isel({time_name: indices}).transpose(time_name, lat_name, lon_name).values
                accumulator.add(int(month), u, v)
    if accumulator is None:
        raise ValueError("no OSCAR input files supplied")
    return accumulator


def _month_ranges(start: date, end: date):
    current = start.replace(day=1)
    while current <= end:
        next_month = (current.replace(year=current.year + 1, month=1)
                      if current.month == 12 else current.replace(month=current.month + 1))
        yield max(start, current), min(end, next_month - timedelta(days=1))
        current = next_month


def acquire_oscar(workspace: Path, checkpoint: Path, *, start: str,
                   end: str) -> CurrentAccumulator:
    """Authenticated monthly acquisition; raw granules are deleted per batch."""
    try:
        import earthaccess
    except ImportError as exc:  # pragma: no cover
        raise RuntimeError("install earthaccess to acquire OSCAR granules") from exc
    authentication = earthaccess.login(strategy="netrc", persist=False)
    if not authentication.authenticated:
        raise RuntimeError("Earthdata authentication failed; configure ~/.netrc")
    workspace.mkdir(parents=True, exist_ok=True)
    accumulator = CurrentAccumulator.load_checkpoint(checkpoint) if checkpoint.exists() else None
    progress = checkpoint.with_suffix(".json")
    completed = progress.read_text().strip() if progress.exists() else ""
    start_date, end_date = date.fromisoformat(start), date.fromisoformat(end)
    for month_start, month_end in _month_ranges(start_date, end_date):
        key = month_end.isoformat()
        if completed and key <= completed:
            continue
        results = earthaccess.search_data(
            short_name="OSCAR_L4_OC_FINAL_V2.0",
            temporal=(month_start.isoformat(), month_end.isoformat()),
        )
        if not results:
            raise RuntimeError(f"OSCAR has no granules for {month_start} .. {month_end}")
        with tempfile.TemporaryDirectory(prefix="oscar-", dir=workspace) as temporary:
            files = [Path(path) for path in earthaccess.download(results, temporary)]
            accumulator = ingest_oscar_files(files, start=month_start.isoformat(),
                                             end=month_end.isoformat(),
                                             accumulator=accumulator)
        accumulator.save_checkpoint(checkpoint)
        progress.write_text(key + "\n", encoding="ascii")
        completed = key
    if accumulator is None:
        raise RuntimeError("no OSCAR data were accumulated")
    return accumulator


def main(argv: list[str] | None = None) -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("output", type=Path)
    parser.add_argument("inputs", type=Path, nargs="*")
    parser.add_argument("--start", default="1995-01-01")
    parser.add_argument("--end", default="2022-08-05")
    parser.add_argument("--minimum-samples", type=int, default=100)
    parser.add_argument("--earthdata", action="store_true",
                        help="acquire monthly batches using Earthdata Login")
    parser.add_argument("--workspace", type=Path)
    parser.add_argument("--checkpoint", type=Path)
    args = parser.parse_args(argv)
    args.output.mkdir(parents=True, exist_ok=True)
    if args.earthdata:
        if args.inputs:
            parser.error("inputs cannot be combined with --earthdata")
        if args.workspace is None or args.checkpoint is None:
            parser.error("--earthdata requires --workspace and --checkpoint")
        accumulator = acquire_oscar(args.workspace, args.checkpoint,
                                    start=args.start, end=args.end)
    else:
        if not args.inputs:
            parser.error("supply NetCDF inputs or use --earthdata")
        accumulator = ingest_oscar_files(args.inputs, start=args.start, end=args.end)
    for month in range(1, 13):
        path = args.output / f"current{month:02d}.gz"
        write_gzip(path, encode_current(accumulator.field(month, minimum_samples=args.minimum_samples)))
        # Independent read-back catches wire-level and range failures now.
        decode_current(path)


if __name__ == "__main__":
    main(sys.argv[1:])
