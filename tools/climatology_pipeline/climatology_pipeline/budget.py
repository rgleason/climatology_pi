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
    source_latitudes: int = 721,
    source_longitudes: int = 1440,
    block_samples: int = 28,
    budget_gb: float = 100.0,
) -> StoragePlan:
    """Conservative peak estimate for the actual global ERA5 read block."""
    if source_latitudes <= 0 or source_longitudes <= 0 or block_samples <= 0:
        raise ValueError("ERA5 source dimensions and block size must be positive")
    raw = block_samples * source_latitudes * source_longitudes * 2 * 4
    # Allow 4x raw for compressed transfer, decoded arrays, coordinates and
    # library overhead at the same time.
    source = raw * 4
    aggregation = 12 * 360 * 720 * (8 * 8 + 8 * 8 + 3 * 8)
    checkpoint = aggregation * 2
    output = 100_000_000
    safety = 2 * GB
    return StoragePlan(int(budget_gb * GB), source, aggregation, checkpoint, output, safety)


def oscar_plan(
    *,
    days_per_batch: int = 31,
    maximum_granule_bytes: int = 32_000_000,
    source_latitudes: int = 720,
    source_longitudes: int = 1440,
    budget_gb: float = 100.0,
) -> StoragePlan:
    """Conservative peak for one monthly OSCAR Final V2 acquisition process."""
    if (days_per_batch <= 0 or maximum_granule_bytes <= 0 or
            source_latitudes <= 0 or source_longitudes <= 0):
        raise ValueError("OSCAR batch, granule and grid dimensions must be positive")
    source = days_per_batch * maximum_granule_bytes
    aggregation = 12 * source_latitudes * source_longitudes * (8 + 8 + 4)
    # Allow the live accumulator, compressed checkpoint replacement and
    # serialization/library overhead to coexist.
    checkpoint = aggregation * 2
    output = 20_000_000
    safety = 2 * GB
    return StoragePlan(int(budget_gb * GB), source, aggregation,
                       checkpoint, output, safety)
