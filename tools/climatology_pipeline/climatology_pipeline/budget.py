"""Storage planning and enforcement for source acquisition."""

from __future__ import annotations

from dataclasses import dataclass
from pathlib import Path
import shutil


GB = 1_000_000_000


@dataclass(frozen=True)
class StoragePlan:
    budget_bytes: int
    source_chunk_bytes: int
    aggregation_bytes: int
    checkpoint_bytes: int
    output_bytes: int
    safety_bytes: int

    @property
    def peak_bytes(self) -> int:
        return (
            self.source_chunk_bytes
            + self.aggregation_bytes
            + self.checkpoint_bytes
            + self.output_bytes
            + self.safety_bytes
        )

    def validate(self, workspace: str | Path | None = None) -> None:
        if self.peak_bytes > self.budget_bytes:
            raise RuntimeError(
                f"planned peak {self.peak_bytes / GB:.2f} GB exceeds "
                f"budget {self.budget_bytes / GB:.2f} GB"
            )
        if workspace is not None:
            free = shutil.disk_usage(Path(workspace)).free
            if self.peak_bytes > free:
                raise RuntimeError(
                    f"planned peak {self.peak_bytes / GB:.2f} GB exceeds "
                    f"available space {free / GB:.2f} GB"
                )


def era5_wind_plan(
    *,
    tile_latitudes: int = 80,
    tile_longitudes: int = 160,
    days_per_chunk: int = 31,
    samples_per_day: int = 4,
    budget_gb: float = 100.0,
) -> StoragePlan:
    """Conservative peak estimate for a two-variable float32 ERA5 chunk."""
    raw = days_per_chunk * samples_per_day * tile_latitudes * tile_longitudes * 2 * 4
    # Allow 4x raw for compressed transfer, decoded arrays, coordinates and
    # library overhead at the same time.
    source = raw * 4
    aggregation = 12 * 360 * 720 * (8 * 8 + 8 * 8 + 3 * 8)
    checkpoint = aggregation * 2
    output = 100_000_000
    safety = 2 * GB
    return StoragePlan(int(budget_gb * GB), source, aggregation, checkpoint, output, safety)
