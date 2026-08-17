"""Create deterministic before/after geographical climatology comparisons."""

from __future__ import annotations

import argparse
import json
from pathlib import Path

import numpy as np

from .legacy import (decode_current, decode_wind, decode_wind_extras,
                     decode_scalar)
from .current import CurrentAccumulator
from .scalar import SCALAR_DEFINITIONS, decode_field
from .wind import WindAccumulator


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


def wind_global_comparison(old: Path, new: Path, month: int) -> dict[str, object]:
    old_atlas = decode_wind(old / f"wind{month:02d}.gz")
    new_atlas = decode_wind(new / f"wind{month:02d}.gz")

    def calm_gale(directory: Path, atlas):
        extras = directory / f"wind-extras{month:02d}.gz"
        if extras.exists():
            calm, gale, valid = decode_wind_extras(extras)
            return calm, gale, valid
        return atlas.calm, atlas.gale, atlas.valid

    old_calm, old_gale, old_valid = calm_gale(old, old_atlas)
    new_calm, new_gale, new_valid = calm_gale(new, new_atlas)
    paired = old_atlas.valid & new_atlas.valid
    old_probability = old_atlas.frequencies.astype(np.float64) / old_atlas.direction_resolution
    new_probability = new_atlas.frequencies.astype(np.float64) / new_atlas.direction_resolution
    frequency_sum = np.sum(new_atlas.frequencies, axis=2)
    return {
        "month": month,
        "old_valid_cells": int(np.count_nonzero(old_atlas.valid)),
        "new_valid_cells": int(np.count_nonzero(new_atlas.valid)),
        "paired_valid_cells": int(np.count_nonzero(paired)),
        "new_cells_with_nonunit_frequency_sum": int(np.count_nonzero(
            new_atlas.valid & (frequency_sum != new_atlas.direction_resolution)
        )),
        "old_mean_calm_percent": float(np.mean(old_calm[old_valid])),
        "new_mean_calm_percent": float(np.mean(new_calm[new_valid])),
        "old_mean_gale_percent": float(np.mean(old_gale[old_valid])),
        "new_mean_gale_percent": float(np.mean(new_gale[new_valid])),
        "paired_prevailing_sector_changed_fraction": float(np.mean(
            np.argmax(old_probability[paired], axis=1) !=
            np.argmax(new_probability[paired], axis=1)
        )),
        "paired_mean_absolute_sector_probability_difference": float(np.mean(
            np.abs(new_probability[paired] - old_probability[paired])
        )),
        "old_calm_gale_note": (
            "The historical marker retains calm or gale, not both; its missing "
            "counterpart decodes as zero"
        ),
    }


def current_global_comparison(old: Path, new: Path, month: int) -> dict[str, object]:
    old_field = decode_current(old / f"current{month:02d}.gz")
    new_field = decode_current(new / f"current{month:02d}.gz")
    old_valid = np.isfinite(old_field.u) & np.isfinite(old_field.v)
    new_valid = np.isfinite(new_field.u) & np.isfinite(new_field.v)
    paired = old_valid & new_valid
    difference_squared = ((new_field.u[paired] - old_field.u[paired]) ** 2 +
                          (new_field.v[paired] - old_field.v[paired]) ** 2)
    return {
        "month": month,
        "old_valid_cells": int(np.count_nonzero(old_valid)),
        "new_valid_cells": int(np.count_nonzero(new_valid)),
        "paired_valid_cells": int(np.count_nonzero(paired)),
        "old_mean_speed_kn": float(np.mean(np.hypot(
            old_field.u[old_valid], old_field.v[old_valid]
        ))),
        "new_mean_speed_kn": float(np.mean(np.hypot(
            new_field.u[new_valid], new_field.v[new_valid]
        ))),
        "paired_vector_root_mean_square_difference_kn": float(
            np.sqrt(np.mean(difference_squared))
        ),
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


def scalar_global_comparison(old: Path, new: Path, name: str) -> dict[str, object]:
    """Summarise complete decoded fields without conflating missing cells."""
    old_values = decode_field(name, decode_scalar(old / f"{name}.gz", name))
    new_values = decode_field(name, decode_scalar(new / f"{name}.gz", name))
    old_finite = np.isfinite(old_values)
    new_finite = np.isfinite(new_values)
    paired = old_finite & new_finite
    difference = new_values[paired] - old_values[paired]
    return {
        "field": name,
        "units": SCALAR_DEFINITIONS[name].units,
        "cells": int(old_values.size),
        "old_valid": int(np.count_nonzero(old_finite)),
        "new_valid": int(np.count_nonzero(new_finite)),
        "paired_valid": int(np.count_nonzero(paired)),
        "old_mean": float(np.mean(old_values[old_finite])),
        "new_mean": float(np.mean(new_values[new_finite])),
        "paired_mean_difference_new_minus_old": float(np.mean(difference)),
        "paired_mean_absolute_difference": float(np.mean(np.abs(difference))),
        "paired_root_mean_square_difference": float(np.sqrt(np.mean(difference ** 2))),
    }


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


def source_binding_validation(
    directory: Path,
    wind_checkpoint: Path,
    current_checkpoint: Path,
    *,
    minimum_samples: int = 100,
) -> dict[str, object]:
    """Bind released bytes back to the unquantised source accumulators.

    The checkpoints are sufficient statistics accumulated directly from the
    authoritative source grids.  Comparing their independently decoded output
    with the expected fields catches orientation, resampling, encoding and
    packaging mistakes which a simple old-versus-new comparison cannot.
    """
    wind = WindAccumulator.load_checkpoint(wind_checkpoint)
    current = CurrentAccumulator.load_checkpoint(current_checkpoint)
    wind_results: list[dict[str, object]] = []
    current_results: list[dict[str, object]] = []
    checks: list[bool] = []

    for month in (1, 7):
        expected = wind.atlas(month, minimum_samples=minimum_samples)
        actual = decode_wind(directory / f"wind{month:02d}.gz")
        calm, gale, extras_valid = decode_wind_extras(
            directory / f"wind-extras{month:02d}.gz"
        )
        shape_matches = expected.valid.shape == actual.valid.shape
        valid_matches = bool(
            shape_matches
            and np.array_equal(expected.valid, actual.valid)
            and np.array_equal(expected.valid, extras_valid)
        )
        if shape_matches:
            frequency_error = int(np.max(np.abs(
                expected.frequencies.astype(np.int16)
                - actual.frequencies.astype(np.int16)
            )))
            speed_error = int(np.max(np.abs(
                expected.speeds.astype(np.int16)
                - actual.speeds.astype(np.int16)
            )))
            calm_error = int(np.max(np.abs(
                expected.calm.astype(np.int16) - calm.astype(np.int16)
            )))
            gale_error = int(np.max(np.abs(
                expected.gale.astype(np.int16) - gale.astype(np.int16)
            )))
        else:
            frequency_error = speed_error = calm_error = gale_error = -1
        passed = bool(
            valid_matches and frequency_error == 0 and speed_error == 0
            and calm_error == 0 and gale_error == 0
        )
        checks.append(passed)
        wind_results.append({
            "month": month,
            "passed": passed,
            "valid_mask_exact": valid_matches,
            "maximum_frequency_byte_error": frequency_error,
            "maximum_speed_byte_error": speed_error,
            "maximum_calm_percentage_point_error": calm_error,
            "maximum_gale_percentage_point_error": gale_error,
            "note": "Exact byte equality is required after source-statistic quantisation",
        })

        expected_current = current.field(
            month, minimum_samples=minimum_samples
        )
        actual_current = decode_current(directory / f"current{month:02d}.gz")
        shape_matches = expected_current.u.shape == actual_current.u.shape
        expected_valid = np.isfinite(expected_current.u) & np.isfinite(expected_current.v)
        actual_valid = np.isfinite(actual_current.u) & np.isfinite(actual_current.v)
        valid_matches = bool(shape_matches and np.array_equal(expected_valid, actual_valid))
        if shape_matches and np.any(expected_valid):
            u_error = float(np.max(np.abs(
                expected_current.u[expected_valid] - actual_current.u[expected_valid]
            )))
            v_error = float(np.max(np.abs(
                expected_current.v[expected_valid] - actual_current.v[expected_valid]
            )))
        else:
            u_error = v_error = float("inf")
        tolerance = 0.5 / actual_current.multiplier + 1e-12
        passed = bool(valid_matches and u_error <= tolerance and v_error <= tolerance)
        checks.append(passed)
        current_results.append({
            "month": month,
            "passed": passed,
            "valid_mask_exact": valid_matches,
            "maximum_u_quantisation_error_kn": u_error,
            "maximum_v_quantisation_error_kn": v_error,
            "allowed_half_step_error_kn": 0.5 / actual_current.multiplier,
            "note": "Expected U/V is independently decoded after source-grid averaging and resampling",
        })

    return {
        "wind": wind_results,
        "current": current_results,
        "summary": {
            "checks": len(checks),
            "checks_passed": sum(checks),
            "all_checks_passed": all(checks),
            "wind_checkpoint": wind_checkpoint.name,
            "current_checkpoint": current_checkpoint.name,
        },
    }


def comparison(
    old: Path,
    new: Path,
    *,
    wind_checkpoint: Path | None = None,
    current_checkpoint: Path | None = None,
) -> dict[str, object]:
    result: dict[str, object] = {
        "wind": [], "wind_global": [], "current": [], "current_global": [],
        "scalar": [], "scalar_global": []
    }
    for month in (1, 7):
        result["wind_global"].append(wind_global_comparison(old, new, month))
        result["current_global"].append(current_global_comparison(old, new, month))
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
        result["scalar_global"].append(scalar_global_comparison(old, new, field))
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
    checks = [
        item["new_sanity"]
        for group in (result["wind"], result["current"])
        for item in group
    ]
    passed = sum(bool(check["passed"]) for check in checks)
    result["summary"] = {
        "advisory_checks": len(checks),
        "advisory_checks_passed": passed,
        "all_advisory_checks_passed": passed == len(checks),
        "note": (
            "Broad physical sanity checks supplement source/grid validation; "
            "they are not statistical confidence intervals"
        ),
    }
    if (wind_checkpoint is None) != (current_checkpoint is None):
        raise ValueError("both wind and current checkpoints are required for source binding")
    if wind_checkpoint is not None and current_checkpoint is not None:
        result["source_binding"] = source_binding_validation(
            new, wind_checkpoint, current_checkpoint
        )
    return result


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("old", type=Path)
    parser.add_argument("new", type=Path)
    parser.add_argument("--output", type=Path)
    parser.add_argument("--wind-checkpoint", type=Path)
    parser.add_argument("--current-checkpoint", type=Path)
    args = parser.parse_args()
    report = json.dumps(comparison(
        args.old,
        args.new,
        wind_checkpoint=args.wind_checkpoint,
        current_checkpoint=args.current_checkpoint,
    ), indent=2) + "\n"
    if args.output:
        args.output.write_text(report, encoding="utf-8")
    else:
        print(report, end="")


if __name__ == "__main__":
    main()
