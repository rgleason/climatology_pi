"""Merge non-overlapping wind checkpoints and encode one final atlas."""

from __future__ import annotations

import argparse
import json
from pathlib import Path

from .legacy import encode_wind, encode_wind_extras, write_gzip
from .wind import WindAccumulator


def merge_checkpoints(paths: list[Path]) -> WindAccumulator:
    if not paths:
        raise ValueError("at least one checkpoint is required")
    merged = WindAccumulator.load_checkpoint(paths[0])
    for path in paths[1:]:
        merged.merge(WindAccumulator.load_checkpoint(path))
    return merged


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
    accumulator = merge_checkpoints(args.checkpoint)
    if args.merged_checkpoint:
        accumulator.save_checkpoint(args.merged_checkpoint)
        metadata = args.merged_checkpoint.with_suffix(
            args.merged_checkpoint.suffix + ".json")
        metadata.write_text(json.dumps({
            "component_checkpoints": [str(path) for path in args.checkpoint],
        }, indent=2) + "\n", encoding="utf-8")
    write_atlas(accumulator, args.output, args.minimum_samples)


if __name__ == "__main__":
    main()
