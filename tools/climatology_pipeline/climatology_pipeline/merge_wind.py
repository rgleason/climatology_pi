"""Merge non-overlapping wind checkpoints and encode one final atlas."""

from __future__ import annotations

import argparse
import json
from pathlib import Path

from .legacy import encode_wind, encode_wind_extras, write_gzip
from .wind import WindAccumulator


def _partition(path: Path) -> tuple[int, int, Path]:
    metadata = path.with_suffix(path.suffix + ".json")
    if not metadata.exists():
        raise ValueError(f"{path}: wind checkpoint progress metadata is missing")
    try:
        state = json.loads(metadata.read_text(encoding="utf-8"))
        start = int(state["start_year"])
        completed = int(state["completed_year"])
        requested_end = int(state["end_year"])
    except (KeyError, TypeError, ValueError, json.JSONDecodeError) as exc:
        raise ValueError(f"{path}: wind checkpoint progress metadata is invalid") from exc
    if completed < start or completed > requested_end:
        raise ValueError(f"{path}: wind checkpoint completion range is invalid")
    return start, completed, path


def merge_checkpoints(paths: list[Path]) -> tuple[WindAccumulator, list[dict[str, object]]]:
    if not paths:
        raise ValueError("at least one checkpoint is required")
    partitions = sorted(
        (_partition(path) for path in paths),
        key=lambda item: (item[0], item[1], str(item[2])),
    )
    for previous, current in zip(partitions, partitions[1:]):
        if current[0] <= previous[1]:
            raise ValueError(f"wind checkpoint periods overlap: {previous[2]} and {current[2]}")
        if current[0] != previous[1] + 1:
            raise ValueError(f"wind checkpoint periods have a gap: {previous[2]} and {current[2]}")
    merged = WindAccumulator.load_checkpoint(partitions[0][2])
    for _, _, path in partitions[1:]:
        merged.merge(WindAccumulator.load_checkpoint(path))
    periods = [
        {"start_year": start, "completed_year": completed, "file": str(path)}
        for start, completed, path in partitions
    ]
    return merged, periods


def write_atlas(accumulator: WindAccumulator, output: Path,
                minimum_samples: int = 100) -> None:
    output.mkdir(parents=True, exist_ok=True)
    for month in range(1, 13):
        atlas = accumulator.atlas(month, minimum_samples=minimum_samples)
        write_gzip(output / f"wind{month:02d}.gz", encode_wind(atlas))
        write_gzip(output / f"wind-extras{month:02d}.gz",
                   encode_wind_extras(atlas))


def main(argv: list[str] | None = None) -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--checkpoint", type=Path, action="append", required=True)
    parser.add_argument("--merged-checkpoint", type=Path)
    parser.add_argument("--minimum-samples", type=int, default=100)
    args = parser.parse_args(argv)
    accumulator, periods = merge_checkpoints(args.checkpoint)
    if args.merged_checkpoint:
        accumulator.save_checkpoint(args.merged_checkpoint)
        metadata = args.merged_checkpoint.with_suffix(
            args.merged_checkpoint.suffix + ".json")
        metadata.write_text(json.dumps({
            "component_checkpoints": periods,
            "start_year": periods[0]["start_year"],
            "completed_year": periods[-1]["completed_year"],
        }, indent=2) + "\n", encoding="utf-8")
    write_atlas(accumulator, args.output, args.minimum_samples)


if __name__ == "__main__":
    main()
