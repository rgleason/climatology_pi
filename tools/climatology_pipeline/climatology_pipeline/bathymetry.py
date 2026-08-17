"""Stream GEBCO into the one-degree legacy sea-depth context layer."""

from __future__ import annotations

import argparse
from pathlib import Path
import sys

import numpy as np

from .legacy import encode_scalar, write_gzip


DEPTH_BINS_METRES = np.array([
    0, 10, 20, 30, 50, 75, 100, 125, 150, 200, 250, 300, 400, 500,
    600, 700, 800, 900, 1000, 1100, 1200, 1300, 1400, 1500, 1750,
    2000, 2500, 3000, 3500, 4000, 4500, 5000, 5500, 6000, 6500,
    7000, 7500, 8000, 9000, 10000,
], dtype=np.float64)


def encode_depths(depth_metres: np.ndarray) -> np.ndarray:
    depths = np.asarray(depth_metres, dtype=np.float64)
    result = np.full(depths.shape, -128, dtype=np.int8)
    wet = np.isfinite(depths) & (depths >= 0)
    if np.any(wet):
        # Nearest familiar display bin, with shallower bin winning exact ties.
        indices = np.searchsorted(DEPTH_BINS_METRES, depths[wet])
        indices = np.clip(indices, 1, len(DEPTH_BINS_METRES) - 1)
        lower = DEPTH_BINS_METRES[indices - 1]
        upper = DEPTH_BINS_METRES[indices]
        choose_lower = depths[wet] - lower <= upper - depths[wet]
        indices[choose_lower] -= 1
        result[wet] = indices.astype(np.int8)
    return result


def aggregate_elevation(elevation: np.ndarray, factor_lat: int,
                        factor_lon: int) -> np.ndarray:
    """Return median wet depth per output cell; land-only cells are missing."""
    values = np.asarray(elevation, dtype=np.float64)
    if values.ndim != 2 or values.shape[0] % factor_lat or values.shape[1] % factor_lon:
        raise ValueError("elevation dimensions must be divisible by aggregation factors")
    grouped = values.reshape(values.shape[0] // factor_lat, factor_lat,
                             values.shape[1] // factor_lon, factor_lon)
    wet_depth = np.where(grouped < 0, -grouped, np.nan)
    with np.errstate(all="ignore"):
        return np.nanmedian(wet_depth, axis=(1, 3))


def build_gebco(path: str | Path) -> np.ndarray:
    """Build north-to-south 180x360 legacy bins using bounded row slabs."""
    try:
        import xarray as xr
    except ImportError as exc:  # pragma: no cover
        raise RuntimeError("install the pipeline's source dependencies") from exc
    output = np.full((180, 360), -128, dtype=np.int8)
    with xr.open_dataset(path, chunks={}) as dataset:
        variable = "elevation" if "elevation" in dataset else "height_above_mean_sea_level"
        if variable not in dataset:
            raise ValueError("GEBCO file has no elevation variable")
        field = dataset[variable]
        lat_name = "lat" if "lat" in field.dims else "latitude"
        lon_name = "lon" if "lon" in field.dims else "longitude"
        latitude = np.asarray(dataset[lat_name].values)
        longitude = np.asarray(dataset[lon_name].values)
        if len(latitude) % 180 or len(longitude) % 360:
            raise ValueError("GEBCO dimensions are not integer multiples of 180x360")
        factor_lat, factor_lon = len(latitude) // 180, len(longitude) // 360
        ascending = latitude[0] < latitude[-1]
        for output_row in range(180):
            source_band = 179 - output_row if ascending else output_row
            start = source_band * factor_lat
            slab = field.isel({lat_name: slice(start, start + factor_lat)})
            slab = slab.transpose(lat_name, lon_name).values
            if longitude[0] >= 0:
                # GEBCO normally starts at -180.  Normalize unusual 0..360
                # inputs to the legacy west-to-east -180-origin ordering.
                split = np.searchsorted(longitude, 180.0)
                slab = np.concatenate((slab[:, split:], slab[:, :split]), axis=1)
            depths = aggregate_elevation(slab, factor_lat, factor_lon)[0]
            # Runtime longitude index 0 is centred at +0.5 E, whereas GEBCO's
            # grouped sequence starts at -179.5.  Rotate 180 degrees.
            output[output_row] = encode_depths(np.roll(depths, -180))
    return output


def main(argv: list[str] | None = None) -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args(argv)
    write_gzip(args.output, encode_scalar(build_gebco(args.source), "seadepth"))


if __name__ == "__main__":
    main(sys.argv[1:])
