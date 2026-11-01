"""Assess whether the packaged 1995-2024 IBTrACS record supports ENSO modes."""

from __future__ import annotations

import argparse
from collections import defaultdict
from pathlib import Path

import numpy as np
import pandas as pd

from climatology_pipeline.legacy import decode_cyclones
from enso import load_classification


BASINS = ("atl", "epa", "wpa", "spa", "nio", "she")


def counts_by_basin_month(data: Path) -> pd.DataFrame:
    records = []
    for basin in BASINS:
        tracks = decode_cyclones(data / f"cyclone-{basin}.gz", southern=basin in {"spa", "she"})
        counts: dict[tuple[int, int], int] = defaultdict(int)
        for track in tracks:
            active = {(point.year, point.month) for point in track.points}
            for key in active:
                counts[key] += 1
        for year in range(1995, 2025):
            for month in range(1, 13):
                records.append({"basin": basin, "year": year, "month": month,
                                "active_tracks": counts[(year, month)]})
    return pd.DataFrame(records)


def analyse(counts: pd.DataFrame, classification: pd.DataFrame) -> tuple[pd.DataFrame, pd.DataFrame]:
    merged = counts.merge(classification[["year", "month", "phase", "event_id"]],
                          on=["year", "month"], how="left")
    composites = merged.groupby(["basin", "month", "phase"]).agg(
        sample_years=("year", "count"), mean_active_tracks=("active_tracks", "mean"),
        median_active_tracks=("active_tracks", "median"),
        standard_deviation=("active_tracks", "std"),
    ).reset_index()
    loo_records = []
    for (basin, month, phase), subset in merged.loc[merged.phase != "neutral"].groupby(
            ["basin", "month", "phase"]):
        for event, held in subset.groupby("event_id"):
            train_phase = subset.loc[subset.event_id != event, "active_tracks"]
            train_all = merged.loc[(merged.basin == basin) & (merged.month == month) &
                                   ~merged.event_id.eq(event), "active_tracks"]
            if len(train_phase) < 3:
                continue
            observed = float(held.active_tracks.mean())
            phase_error = abs(float(train_phase.mean()) - observed)
            all_error = abs(float(train_all.mean()) - observed)
            loo_records.append({"basin": basin, "month": month, "phase": phase,
                                "event_id": event, "observed_active_tracks": observed,
                                "phase_absolute_error": phase_error,
                                "all_year_absolute_error": all_error,
                                "phase_better": phase_error < all_error})
    detail = pd.DataFrame(loo_records)
    skill = detail.groupby(["basin", "month", "phase"]).agg(
        omitted_events=("event_id", "count"),
        phase_better_fraction=("phase_better", "mean"),
        phase_mae=("phase_absolute_error", "mean"),
        all_year_mae=("all_year_absolute_error", "mean"),
    ).reset_index()
    skill["skill_vs_all_pct"] = 100 * (skill.all_year_mae - skill.phase_mae) / skill.all_year_mae
    return composites, skill


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--data", type=Path, default=Path("data"))
    parser.add_argument("--oni", type=Path, required=True)
    parser.add_argument("--output", type=Path, default=Path("results"))
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    counts = counts_by_basin_month(args.data)
    composites, skill = analyse(counts, load_classification(args.oni))
    counts.to_csv(args.output / "cyclone-basin-month-counts.csv", index=False)
    composites.to_csv(args.output / "cyclone-composites.csv", index=False)
    skill.to_csv(args.output / "cyclone-leave-one-event-out.csv", index=False)


if __name__ == "__main__":
    main()
