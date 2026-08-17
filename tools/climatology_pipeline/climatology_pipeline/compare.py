"""Create deterministic before/after geographical climatology comparisons."""

from __future__ import annotations

import argparse
import json
from pathlib import Path

import numpy as np

from .legacy import (decode_current, decode_wind, decode_wind_extras,
                     decode_scalar)
from .scalar import SCALAR_DEFINITIONS, decode_field


WIND_REGIONS = (
    ("North Atlantic westerlies", 50., -30.),
    ("NE trade-wind Atlantic", 20., -30.),
    ("tropical convergence region", 5., -25.),
    ("Southern Ocean", -50., 0.),
    ("Mediterranean", 35., 18.),
    ("Indian Ocean monsoon", 15., 70.),
)
CURRENT_SYSTEMS = (
    ("Gulf Stream", 38., -68.),
    ("North Atlantic Drift", 50., -30.),
    ("Canary Current", 28., -17.),
    ("North Equatorial Current", 10., -40.),
    ("Equatorial Countercurrent", 5., -25.),
    ("Kuroshio", 32., 140.),
    ("Agulhas", -35., 25.),
    ("Antarctic Circumpolar Current", -50., 0.),
)
SECTOR_NAMES = ("N", "NE", "E", "SE", "S", "SW", "W", "NW")
WIND_EXPECTED = {
    "North Atlantic westerlies": {"SW", "W", "NW"},
    "NE trade-wind Atlantic": {"N", "NE", "E"},
    "tropical convergence region": {"NE", "E", "SE", "S", "SW"},
    "Southern Ocean": {"SW", "W", "NW"},
    "Mediterranean": {"N", "NE", "W", "NW"},
    "Indian Ocean monsoon": {"N", "NE", "E", "SW", "W"},
}
CURRENT_EXPECTED = {
    "Gulf Stream": (0.0, 100.0),
    "North Atlantic Drift": (20.0, 140.0),
    "Canary Current": (120.0, 240.0),
    "North Equatorial Current": (220.0, 320.0),
    "Equatorial Countercurrent": (40.0, 140.0),
    "Kuroshio": (0.0, 110.0),
    "Agulhas": (120.0, 240.0),
    "Antarctic Circumpolar Current": (40.0, 140.0),
}


def _wind_cell(latitude: float, longitude: float) -> tuple[int, int]:
    return (int(np.clip(np.floor((latitude + 90.) * 2.), 0, 359)),
            int(np.floor(longitude % 360. * 2.)) % 720)


def wind_sample(directory: Path, month: int, latitude: float,
                longitude: float) -> dict[str, object]:
    atlas = decode_wind(directory / f"wind{month:02d}.gz")
    row, column = _wind_cell(latitude, longitude)
    frequencies = atlas.frequencies[row, column].astype(float) * 100. / atlas.direction_resolution
    calm = float(atlas.calm[row, column])
    gale = float(atlas.gale[row, column])
    extras = directory / f"wind-extras{month:02d}.gz"
    if extras.exists():
        enhanced_calm, enhanced_gale, valid = decode_wind_extras(extras)
        if valid[row, column]:
            calm, gale = float(enhanced_calm[row, column]), float(enhanced_gale[row, column])
    prevailing = int(np.argmax(frequencies))
    return {
        "valid": bool(atlas.valid[row, column]),
        "prevailing_from": SECTOR_NAMES[prevailing],
        "prevailing_frequency_percent": float(frequencies[prevailing]),
        "prevailing_mean_speed_kn": float(atlas.speeds[row, column, prevailing]) * atlas.speed_multiplier,
        "calm_percent": calm,
        "gale_percent": gale,
        "frequency_sum_percent": float(np.sum(frequencies)),
    }


def current_sample(directory: Path, month: int, latitude: float,
                   longitude: float) -> dict[str, object]:
    field = decode_current(directory / f"current{month:02d}.gz")
    row = int(np.clip(np.rint((80. - latitude) * 3.), 0, 480))
    column = int(np.rint((longitude % 360.) * 3.)) % 1080
    u, v = float(field.u[row, column]), float(field.v[row, column])
    if not np.isfinite(u) or not np.isfinite(v):
        return {"valid": False}
    return {
        "valid": True,
        "u_east_kn": u,
        "v_north_kn": v,
        "speed_kn": float(np.hypot(u, v)),
        "bearing_to_deg_true": float(np.degrees(np.arctan2(u, v)) % 360.),
    }


def scalar_sample(directory: Path, name: str, month: int,
                  latitude: float, longitude: float) -> float | None:
    definition = SCALAR_DEFINITIONS[name]
    raw = decode_scalar(directory / f"{name}.gz", name)
    values = decode_field(name, raw)
    row = int(np.abs(definition.latitude - latitude).argmin())
    column = int(np.abs(((definition.longitude - longitude + 180.) % 360.) - 180.).argmin())
    value = values[month - 1, row, column]
    return None if not np.isfinite(value) else float(value)


def _wind_check(name: str, sample: dict[str, object]) -> dict[str, object]:
    expected = sorted(WIND_EXPECTED[name])
    passed = bool(
        sample.get("valid") and
        sample.get("prevailing_from") in WIND_EXPECTED[name] and
        abs(float(sample.get("frequency_sum_percent", 0.0)) - 100.0) < 0.01 and
        0.0 < float(sample.get("prevailing_mean_speed_kn", 0.0)) < 60.0
    )
    return {
        "passed": passed,
        "severity": "advisory",
        "expected_prevailing_from": expected,
        "note": "Broad physical sanity range; a failure must be investigated, not hidden",
    }


def _current_check(name: str, sample: dict[str, object]) -> dict[str, object]:
    lower, upper = CURRENT_EXPECTED[name]
    bearing = float(sample.get("bearing_to_deg_true", -1.0))
    passed = bool(
        sample.get("valid") and lower <= bearing <= upper and
        0.02 <= float(sample.get("speed_kn", 0.0)) <= 6.5
    )
    return {
        "passed": passed,
        "severity": "advisory",
        "expected_bearing_to_deg_true": [lower, upper],
        "note": "Broad current-system check; coastal position and season can legitimately matter",
    }


def comparison(old: Path, new: Path) -> dict[str, object]:
    result: dict[str, object] = {"wind": [], "current": [], "scalar": []}
    for month in (1, 7):
        for name, latitude, longitude in WIND_REGIONS:
            old_sample = wind_sample(old, month, latitude, longitude)
            new_sample = wind_sample(new, month, latitude, longitude)
            result["wind"].append({
                "region": name, "month": month, "latitude": latitude,
                "longitude": longitude,
                "old": old_sample,
                "new": new_sample,
                "new_sanity": _wind_check(name, new_sample),
            })
        for name, latitude, longitude in CURRENT_SYSTEMS:
            old_sample = current_sample(old, month, latitude, longitude)
            new_sample = current_sample(new, month, latitude, longitude)
            result["current"].append({
                "system": name, "month": month, "latitude": latitude,
                "longitude": longitude,
                "old": old_sample,
                "new": new_sample,
                "new_sanity": _current_check(name, new_sample),
            })
    for field in SCALAR_DEFINITIONS:
        if not (old / f"{field}.gz").exists() or not (new / f"{field}.gz").exists():
            continue
        for name, latitude, longitude in (
            ("North Atlantic", 50., -30.), ("tropics", 5., -25.),
            ("Mediterranean", 35., 18.),
        ):
            for month in (1, 7):
                result["scalar"].append({
                    "field": field, "region": name, "month": month,
                    "old": scalar_sample(old, field, month, latitude, longitude),
                    "new": scalar_sample(new, field, month, latitude, longitude),
                    "units": SCALAR_DEFINITIONS[field].units,
                })
    return result


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("old", type=Path)
    parser.add_argument("new", type=Path)
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    report = json.dumps(comparison(args.old, args.new), indent=2) + "\n"
    if args.output:
        args.output.write_text(report, encoding="utf-8")
    else:
        print(report, end="")


if __name__ == "__main__":
    main()
