"""Build legacy-compatible cyclone theatres from NOAA IBTrACS v04r01."""

from __future__ import annotations

import argparse
from dataclasses import asdict
import json
from pathlib import Path
import sys

from .cyclone import load_ibtracs, OUTPUT_BASINS, SOUTHERN_BASINS
from .legacy import decode_cyclones, encode_cyclones, write_gzip
from .provenance import sha256


def main(argv: list[str] | None = None) -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args(argv)
    args.output.mkdir(parents=True, exist_ok=True)
    theatres, statistics = load_ibtracs(args.source)
    report = {
        "source": str(args.source),
        "source_sha256": sha256(args.source),
        "selection_period": ["1995-01-01", "2024-12-31"],
        "track_type": "main",
        "cadence": "six-hourly",
        "position": "IBTrACS main-track LAT/LON",
        "wind": "USA_WIND homogeneous one-minute; zero denotes unavailable in legacy format",
        "pressure": "USA_PRES; zero denotes unavailable in legacy format",
        "statistics": asdict(statistics),
        "theatres": {},
    }
    for basin in OUTPUT_BASINS:
        path = args.output / f"cyclone-{basin}.gz"
        write_gzip(path, encode_cyclones(theatres[basin], southern=basin in SOUTHERN_BASINS))
        decoded = decode_cyclones(path, southern=basin in SOUTHERN_BASINS)
        points = [point for track in decoded for point in track.points]
        report["theatres"][basin] = {
            "tracks": len(decoded),
            "points": len(points),
            "first_year": min(point.year for point in points),
            "last_year": max(point.year for point in points),
            "wind_available": sum(point.wind_knots > 0 for point in points),
            "pressure_available": sum(point.pressure_hpa > 0 for point in points),
            "file_sha256": sha256(path),
        }
    (args.output / "cyclone-conversion.json").write_text(
        json.dumps(report, indent=2, sort_keys=True) + "\n", encoding="utf-8"
    )


if __name__ == "__main__":
    main(sys.argv[1:])
