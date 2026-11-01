"""Estimate raw and compressed enhanced-climatology storage by resolution."""

from __future__ import annotations

import argparse
from pathlib import Path
import pandas as pd


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--output", type=Path, default=Path("results/storage-estimates.csv"))
    args = parser.parse_args()
    args.output.parent.mkdir(parents=True, exist_ok=True)
    records = []
    # Enhanced field: 8 wind-sector frequency + 8 conditional speed + calm +
    # gale + vector u/v + three wave stats + two current components + pressure
    # + SST + sample count + uncertainty = 29 quantised 16-bit values.  Actual
    # coast/missing-value and compression ratios are explicitly ranged.
    fields = 29
    for resolution in (2.0, 1.0, 0.5, 0.25):
        cells = round(180 / resolution) * round(360 / resolution)
        for regimes in (1, 4):
            raw = cells * 12 * regimes * fields * 2
            for compression_ratio in (0.25, 0.40, 0.60):
                records.append({
                    "resolution_degrees": resolution,
                    "regimes": regimes,
                    "statistics": fields,
                    "raw_mib": raw / 2**20,
                    "assumed_compressed_fraction": compression_ratio,
                    "estimated_download_mib": raw * compression_ratio / 2**20,
                })
    pd.DataFrame(records).to_csv(args.output, index=False)


if __name__ == "__main__":
    main()
