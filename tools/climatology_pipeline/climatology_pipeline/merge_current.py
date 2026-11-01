"""Merge exact, non-overlapping OSCAR checkpoints and encode current files."""

from __future__ import annotations

import argparse
from datetime import date, timedelta
from pathlib import Path

from .current import CurrentAccumulator
from .legacy import decode_current, encode_current, write_gzip


SOURCE = "OSCAR_L4_OC_FINAL_V2.0"


def _partition(path: Path) -> tuple[date, date, dict[str, object]]:
    metadata = CurrentAccumulator.checkpoint_metadata(path)
    if metadata is None:
        raise ValueError(f"{path}: checkpoint has no source/period metadata")
    if metadata.get("schema") != 1 or metadata.get("source") != SOURCE:
        raise ValueError(f"{path}: checkpoint is not OSCAR Final V2 schema 1")
    try:
        start = date.fromisoformat(str(metadata["start"]))
        end = date.fromisoformat(str(metadata["end"]))
        completed = date.fromisoformat(str(metadata["completed_through"]))
    except (KeyError, ValueError) as exc:
        raise ValueError(f"{path}: checkpoint period metadata is invalid") from exc
    if completed != end:
        raise ValueError(f"{path}: checkpoint is incomplete through {completed}")
    if end < start:
        raise ValueError(f"{path}: checkpoint period is reversed")
    return start, end, metadata


def merge(checkpoints: list[Path]) -> tuple[CurrentAccumulator, dict[str, object]]:
    if not checkpoints:
        raise ValueError("at least one current checkpoint is required")
    partitions = [(*_partition(path), path) for path in checkpoints]
    partitions.sort(key=lambda item: (item[0], item[1], str(item[3])))
    for previous, current in zip(partitions, partitions[1:]):
        if current[0] <= previous[1]:
            raise ValueError(
                f"current checkpoint periods overlap: {previous[3]} and {current[3]}"
            )
        if current[0] != previous[1] + timedelta(days=1):
            raise ValueError(
                f"current checkpoint periods have a gap: {previous[3]} and {current[3]}"
            )
    accumulator = CurrentAccumulator.load_checkpoint(partitions[0][3])
    for _, _, _, path in partitions[1:]:
        accumulator.merge(CurrentAccumulator.load_checkpoint(path))
    metadata: dict[str, object] = {
        "schema": 1,
        "source": SOURCE,
        "start": partitions[0][0].isoformat(),
        "end": partitions[-1][1].isoformat(),
        "completed_through": partitions[-1][1].isoformat(),
        "merged_partitions": len(partitions),
    }
    return accumulator, metadata


def main(argv: list[str] | None = None) -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--merged-checkpoint", type=Path, required=True)
    parser.add_argument("--checkpoint", type=Path, action="append", required=True)
    parser.add_argument("--minimum-samples", type=int, default=100)
    args = parser.parse_args(argv)
    accumulator, metadata = merge(args.checkpoint)
    accumulator.save_checkpoint(args.merged_checkpoint, metadata=metadata)
    args.output.mkdir(parents=True, exist_ok=True)
    for month in range(1, 13):
        path = args.output / f"current{month:02d}.gz"
        write_gzip(
            path,
            encode_current(accumulator.field(month, minimum_samples=args.minimum_samples)),
        )
        decode_current(path)


if __name__ == "__main__":
    main()
