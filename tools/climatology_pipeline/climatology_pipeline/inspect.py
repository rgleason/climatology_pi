"""Command line inspection of packaged Climatology data."""

from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path

import numpy as np

from .legacy import SCALAR_SCHEMAS, decode_current, decode_cyclones, decode_scalar, decode_wind
from .scalar import SCALAR_DEFINITIONS, decode_field


def _stats(values: np.ndarray, missing: int | None = None) -> dict[str, object]:
    selected = values if missing is None else values[values != missing]
    return {
        "shape": list(values.shape),
        "dtype": str(values.dtype),
        "valid": int(selected.size),
        "minimum": None if not selected.size else float(np.min(selected)),
        "maximum": None if not selected.size else float(np.max(selected)),
    }


def inspect(path: Path, kind: str | None = None, *, southern: bool = False) -> dict[str, object]:
    if kind is None:
        name = path.name.removesuffix(".gz")
        if name.startswith("wind"):
            kind = "wind"
        elif name.startswith("current"):
            kind = "current"
        elif name.startswith("cyclone-"):
            kind = "cyclone"
        else:
            kind = name
    result: dict[str, object] = {
        "path": str(path),
        "sha256": hashlib.sha256(path.read_bytes()).hexdigest(),
        "kind": kind,
    }
    if kind == "wind":
        atlas = decode_wind(path)
        result.update(
            grid=[atlas.frequencies.shape[0], atlas.frequencies.shape[1]],
            sectors=atlas.frequencies.shape[2],
            direction_resolution=atlas.direction_resolution,
            speed_multiplier=atlas.speed_multiplier,
            valid_cells=int(np.count_nonzero(atlas.valid)),
            calm_percent=_stats(atlas.calm[atlas.valid]),
            gale_percent=_stats(atlas.gale[atlas.valid]),
            sector_frequency_units=f"1/{atlas.direction_resolution}",
            speed_units="knots",
        )
    elif kind == "current":
        current = decode_current(path)
        result.update(
            grid=list(current.u.shape),
            multiplier=current.multiplier,
            units="knots",
            u=_stats(current.u[np.isfinite(current.u)]),
            v=_stats(current.v[np.isfinite(current.v)]),
            missing=int(np.count_nonzero(~np.isfinite(current.u) | ~np.isfinite(current.v))),
        )
    elif kind == "cyclone":
        tracks = decode_cyclones(path, southern=southern)
        points = [point for track in tracks for point in track.points]
        result.update(
            tracks=len(tracks),
            points=len(points),
            first_year=min((point.year for point in points), default=None),
            last_year=max((point.year for point in points), default=None),
            wind_units="knots (source averaging convention not encoded)",
            pressure_units="hPa",
        )
    elif kind in SCALAR_SCHEMAS:
        values = decode_scalar(path, kind)
        missing = 32767 if values.dtype == np.dtype("<i2") else (-128 if values.dtype == np.int8 else 255)
        result["raw"] = _stats(values, missing)
        if kind in SCALAR_DEFINITIONS:
            definition = SCALAR_DEFINITIONS[kind]
            physical = decode_field(kind, values)
            finite = physical[np.isfinite(physical)]
            result["physical"] = {
                **_stats(finite),
                "units": definition.units,
                "scale": definition.scale,
                "offset": definition.offset,
                "missing": definition.missing,
            }
    else:
        raise ValueError(f"cannot infer a supported schema for {path}")
    return result


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("paths", nargs="+", type=Path)
    parser.add_argument("--kind", choices=["wind", "current", "cyclone", *SCALAR_SCHEMAS])
    parser.add_argument("--southern", action="store_true", help="invert legacy southern-basin latitude")
    args = parser.parse_args()
    print(json.dumps([inspect(path, args.kind, southern=args.southern) for path in args.paths], indent=2))


if __name__ == "__main__":
    main()
