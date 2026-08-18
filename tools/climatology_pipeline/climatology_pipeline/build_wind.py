"""Command-line builder for the modern legacy-compatible wind atlas."""

from __future__ import annotations

import argparse
from datetime import datetime, timezone
import json
from pathlib import Path
import sys
import time

from .budget import era5_wind_plan
from .atmosphere import AtmosphereAccumulator, ERA5_ATMOSPHERE_VARIABLES
from .era5 import accumulate_wind, google_arco_source, open_source, weatherbench_source
from .legacy import encode_wind, encode_wind_extras, write_gzip
from .locking import checkpoint_lock
from .wind import WindAccumulator


def _progress_path(checkpoint: Path) -> Path:
    return checkpoint.with_suffix(checkpoint.suffix + ".json")


def _atmosphere_path(checkpoint: Path) -> Path:
    return checkpoint.with_name(checkpoint.stem + "-atmosphere.npz")


def _load(checkpoint: Path, start_year: int, end_year: int) -> tuple[WindAccumulator, int]:
    state_path = _progress_path(checkpoint)
    if not checkpoint.exists() and not state_path.exists():
        return WindAccumulator(), start_year
    if not checkpoint.exists() or not state_path.exists():
        raise RuntimeError("checkpoint and progress metadata must either both exist or both be absent")
    state = json.loads(state_path.read_text())
    if state["start_year"] != start_year or state["end_year"] > end_year:
        raise RuntimeError("checkpoint period does not match requested period")
    if state["completed_year"] != state["end_year"] and state["end_year"] != end_year:
        raise RuntimeError("an incomplete checkpoint cannot change its requested end year")
    return WindAccumulator.load_checkpoint(checkpoint), int(state["completed_year"]) + 1


def _save(checkpoint: Path, accumulator: WindAccumulator, start: int, end: int, year: int) -> None:
    accumulator.save_checkpoint(checkpoint)
    _progress_path(checkpoint).write_text(json.dumps({
        "start_year": start,
        "end_year": end,
        "completed_year": year,
        "updated_utc": datetime.now(timezone.utc).isoformat(),
    }, indent=2) + "\n")


def build(args: argparse.Namespace) -> None:
    args.output.mkdir(parents=True, exist_ok=True)
    args.checkpoint.parent.mkdir(parents=True, exist_ok=True)
    plan = era5_wind_plan(block_samples=args.block_samples,
                          budget_gb=args.budget_gb)
    plan.validate(args.output)
    accumulator, first_year = _load(args.checkpoint, args.start_year, args.end_year)
    atmosphere = None
    if args.with_atmosphere:
        if args.end_year > 2022:
            raise RuntimeError(
                "the public six-hour WeatherBench2 atmosphere source ends in 2022; "
                "use --end-year 2022 or run wind separately for later years"
            )
        atmosphere_path = _atmosphere_path(args.checkpoint)
        if first_year > args.start_year and not atmosphere_path.exists():
            raise RuntimeError("wind checkpoint exists but its atmosphere checkpoint is missing")
        atmosphere = (AtmosphereAccumulator.load_checkpoint(atmosphere_path)
                      if atmosphere_path.exists() else AtmosphereAccumulator())
    sources = {}
    started = time.monotonic()
    for year in range(first_year, args.end_year + 1):
        source = weatherbench_source() if year <= 2022 else google_arco_source()
        if source.identifier not in sources:
            print(f"opening {source.identifier}", flush=True)
            sources[source.identifier] = open_source(source)
        dataset = sources[source.identifier]
        print(f"processing {year} from {source.identifier}", flush=True)
        samples_before = int(accumulator.total.sum())
        block_count = 0
        for stamp in accumulate_wind(
            dataset,
            accumulator,
            start=f"{year}-01-01T00:00:00",
            end=f"{year}-12-31T23:59:59",
            cadence_hours=6,
            block_samples=args.block_samples,
            workers=args.workers,
            extra_variables=ERA5_ATMOSPHERE_VARIABLES if atmosphere else (),
            block_callback=atmosphere.add_block if atmosphere else None,
        ):
            block_count += 1
            if block_count % args.report_blocks == 0:
                print(f"  through {stamp.isoformat()} elapsed={time.monotonic()-started:.1f}s", flush=True)
        samples_after = int(accumulator.total.sum())
        if samples_after <= samples_before:
            raise RuntimeError(f"ERA5 {year} added no valid ocean wind samples")
        _save(args.checkpoint, accumulator, args.start_year, args.end_year, year)
        if atmosphere:
            atmosphere.save_checkpoint(_atmosphere_path(args.checkpoint))
        print(f"checkpointed {year}", flush=True)

    for month in range(1, 13):
        atlas = accumulator.atlas(month, minimum_samples=args.minimum_samples)
        write_gzip(args.output / f"wind{month:02d}.gz", encode_wind(atlas))
        write_gzip(args.output / f"wind-extras{month:02d}.gz", encode_wind_extras(atlas))
    if atmosphere:
        atmosphere.write(args.output, minimum_samples=args.minimum_samples)
    print(f"complete elapsed={time.monotonic()-started:.1f}s", flush=True)


def main(argv: list[str] | None = None) -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--checkpoint", type=Path, required=True)
    parser.add_argument("--start-year", type=int, default=1995)
    parser.add_argument("--end-year", type=int, default=2024)
    parser.add_argument("--block-samples", type=int, default=28)
    parser.add_argument("--workers", type=int, default=16)
    parser.add_argument("--report-blocks", type=int, default=4)
    parser.add_argument("--minimum-samples", type=int, default=100)
    parser.add_argument("--budget-gb", type=float, default=100.0)
    parser.add_argument("--with-atmosphere", action="store_true",
                        help="aggregate MSLP, air temperature, cloud, precipitation and RH in the same source pass")
    args = parser.parse_args(argv)
    if args.start_year > args.end_year:
        parser.error("start year must not exceed end year")
    with checkpoint_lock(args.checkpoint):
        build(args)


if __name__ == "__main__":
    main(sys.argv[1:])
