"""Compare official CPC ONI and relative-ONI episode classifications."""

from __future__ import annotations

import argparse
from pathlib import Path

import pandas as pd

from enso import load_classification


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--oni", type=Path, required=True)
    parser.add_argument("--roni", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    oni = load_classification(args.oni)[["date", "oni_c", "phase"]].rename(
        columns={"oni_c": "oni_c", "phase": "oni_phase"})
    roni = load_classification(args.roni)[["date", "oni_c", "phase"]].rename(
        columns={"oni_c": "roni_c", "phase": "roni_phase"})
    joined = oni.merge(roni, on="date", validate="one_to_one")
    args.output.parent.mkdir(parents=True, exist_ok=True)
    joined.to_csv(args.output, index=False)
    table = pd.crosstab(joined.oni_phase, joined.roni_phase)
    table.to_csv(args.output.with_name("enso-index-confusion-matrix.csv"))


if __name__ == "__main__":
    main()
