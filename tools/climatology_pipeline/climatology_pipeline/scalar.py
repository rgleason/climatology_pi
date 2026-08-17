"""Physical-value aggregation and legacy scalar encoders.

The public API in this module deals in physical units.  Quantisation into the
historical wire representation is intentionally confined to ``encode_field``
so source acquisition and scientific validation never operate on encoded
bytes.
"""

from __future__ import annotations

from dataclasses import dataclass
from pathlib import Path

import numpy as np


@dataclass(frozen=True)
class ScalarDefinition:
    name: str
    shape: tuple[int, int, int]
    latitude: np.ndarray
    longitude: np.ndarray
    scale: float
    offset: float
    dtype: np.dtype
    missing: int
    units: str


def _centres(start: float, step: float, count: int) -> np.ndarray:
    return start + step * np.arange(count, dtype=np.float64)


SCALAR_DEFINITIONS = {
    # Coordinates match the existing runtime's index equations exactly.  The
    # unusual 1.5E and 2E offsets are historical format semantics.
    "sealevelpressure": ScalarDefinition(
        "sealevelpressure", (12, 90, 180), _centres(89.0, -2.0, 90),
        np.mod(_centres(1.5, 2.0, 180), 360), .01, 1000.0,
        np.dtype("<i2"), 32767, "hPa"),
    "seasurfacetemperature": ScalarDefinition(
        "seasurfacetemperature", (12, 180, 360), _centres(89.5, -1.0, 180),
        _centres(.5, 1.0, 360), .2, 15.0, np.dtype("i1"), -128, "degree_Celsius"),
    "airtemperature": ScalarDefinition(
        "airtemperature", (12, 90, 180), _centres(89.0, -2.0, 90),
        np.mod(_centres(.5, 2.0, 180), 360), 1 / 3, 0.0,
        np.dtype("i1"), -128, "degree_Celsius"),
    "cloud": ScalarDefinition(
        "cloud", (12, 90, 180), _centres(89.0, -2.0, 90),
        np.mod(_centres(.5, 2.0, 180), 360), .5, 0.0,
        np.dtype("u1"), 255, "percent"),
    "precipitation": ScalarDefinition(
        "precipitation", (12, 72, 144), _centres(90.0, -2.5, 72),
        np.mod(_centres(2.0, 2.5, 144), 360), .2, 0.0,
        np.dtype("u1"), 255, "mm day-1"),
    "relativehumidity": ScalarDefinition(
        "relativehumidity", (12, 180, 360), _centres(90.0, -1.0, 180),
        _centres(.5, 1.0, 360), .5, 0.0, np.dtype("u1"), 255, "percent"),
}


def encode_field(name: str, values: np.ndarray, *,
                 out_of_range: str = "error") -> np.ndarray:
    """Encode physical monthly values using the legacy quantisation."""
    definition = SCALAR_DEFINITIONS[name]
    physical = np.asarray(values, dtype=np.float64)
    if physical.shape != definition.shape:
        raise ValueError(f"{name} must have shape {definition.shape}, got {physical.shape}")
    finite = np.isfinite(physical)
    encoded_float = np.rint((physical - definition.offset) / definition.scale)
    info = np.iinfo(definition.dtype)
    representable = finite & (encoded_float >= info.min) & (encoded_float <= info.max)
    representable &= encoded_float != definition.missing
    outside = finite & ~representable
    if out_of_range not in {"error", "missing", "clip"}:
        raise ValueError("out_of_range must be 'error', 'missing' or 'clip'")
    if np.any(outside) and out_of_range == "error":
        minimum = np.nanmin(physical[finite]) if np.any(finite) else np.nan
        maximum = np.nanmax(physical[finite]) if np.any(finite) else np.nan
        raise ValueError(f"{name} range {minimum} .. {maximum} exceeds legacy representation")
    result = np.full(definition.shape, definition.missing, dtype=definition.dtype)
    result[representable] = encoded_float[representable].astype(definition.dtype)
    if out_of_range == "clip":
        # Reserve the legacy missing sentinel while saturating real finite
        # values at the nearest representable value.  This is preferable to
        # inventing holes in genuinely extreme climatological regions.
        lower = info.min + (definition.missing == info.min)
        upper = info.max - (definition.missing == info.max)
        clipped = np.clip(encoded_float[outside], lower, upper)
        result[outside] = clipped.astype(definition.dtype)
    return result


def decode_field(name: str, encoded: np.ndarray) -> np.ndarray:
    definition = SCALAR_DEFINITIONS[name]
    raw = np.asarray(encoded)
    if raw.shape != definition.shape:
        raise ValueError(f"{name} must have shape {definition.shape}, got {raw.shape}")
    values = raw.astype(np.float64) * definition.scale + definition.offset
    values[raw == definition.missing] = np.nan
    return values


def relative_humidity(temperature_k: np.ndarray, dewpoint_k: np.ndarray) -> np.ndarray:
    """ERA5 2 m relative humidity using WMO-recommended saturation formulae.

    Water coefficients are used at/above 0 C and ice coefficients below 0 C,
    following ECMWF parameter conversion guidance.  The output is clipped to
    the physical 0..100 percent interval.
    """
    temperature_c = np.asarray(temperature_k, dtype=np.float64) - 273.15
    dewpoint_c = np.asarray(dewpoint_k, dtype=np.float64) - 273.15
    if temperature_c.shape != dewpoint_c.shape:
        raise ValueError("temperature and dewpoint shapes differ")

    def vapour_pressure(celsius: np.ndarray) -> np.ndarray:
        water = 6.112 * np.exp(17.62 * celsius / (243.12 + celsius))
        ice = 6.112 * np.exp(22.46 * celsius / (272.62 + celsius))
        return np.where(celsius >= 0.0, water, ice)

    result = 100.0 * vapour_pressure(dewpoint_c) / vapour_pressure(temperature_c)
    return np.clip(result, 0.0, 100.0)


@dataclass
class MonthlySourceAccumulator:
    """Streaming monthly means on one regular source grid."""

    latitude: np.ndarray
    longitude: np.ndarray

    def __post_init__(self) -> None:
        self.latitude = np.asarray(self.latitude, dtype=np.float64)
        self.longitude = np.mod(np.asarray(self.longitude, dtype=np.float64), 360.0)
        if self.latitude.ndim != 1 or self.longitude.ndim != 1:
            raise ValueError("coordinates must be one-dimensional")
        shape = (12, len(self.latitude), len(self.longitude))
        self.total = np.zeros(shape, dtype=np.float64)
        self.count = np.zeros(shape, dtype=np.uint32)

    def add(self, months: np.ndarray, values: np.ndarray) -> None:
        samples = np.asarray(values, dtype=np.float64)
        month_numbers = np.asarray(months, dtype=np.int8)
        if samples.ndim == 2:
            samples = samples[None]
        if samples.shape[0] != len(month_numbers) or samples.shape[1:] != self.total.shape[1:]:
            raise ValueError("sample dimensions do not match coordinates/months")
        for month in np.unique(month_numbers):
            if not 1 <= month <= 12:
                raise ValueError("month must be in 1..12")
            selected = samples[month_numbers == month]
            finite = np.isfinite(selected)
            index = int(month) - 1
            self.total[index] += np.sum(np.where(finite, selected, 0.0), axis=0)
            self.count[index] += np.sum(finite, axis=0, dtype=np.uint32)

    def monthly(self, minimum_samples: int = 1) -> np.ndarray:
        return np.divide(self.total, self.count,
                         out=np.full(self.total.shape, np.nan),
                         where=self.count >= minimum_samples)

    def save_checkpoint(self, path: str | Path) -> None:
        np.savez_compressed(path, latitude=self.latitude, longitude=self.longitude,
                            total=self.total, count=self.count)

    @classmethod
    def load_checkpoint(cls, path: str | Path) -> "MonthlySourceAccumulator":
        with np.load(path) as stored:
            result = cls(stored["latitude"], stored["longitude"])
            result.total = stored["total"]
            result.count = stored["count"]
        return result


def resample_regular(values: np.ndarray, source_latitude: np.ndarray,
                     source_longitude: np.ndarray, target_latitude: np.ndarray,
                     target_longitude: np.ndarray) -> np.ndarray:
    """Missing-aware bilinear interpolation with cyclic longitude."""
    field = np.asarray(values, dtype=np.float64)
    lat = np.asarray(source_latitude, dtype=np.float64)
    lon = np.mod(np.asarray(source_longitude, dtype=np.float64), 360.0)
    if field.shape[-2:] != (len(lat), len(lon)):
        raise ValueError("field does not match source coordinates")
    if lat[0] > lat[-1]:
        lat = lat[::-1]
        field = field[..., ::-1, :]
    order = np.argsort(lon)
    lon = lon[order]
    field = field[..., order]
    target_lat = np.clip(np.asarray(target_latitude, dtype=np.float64), lat[0], lat[-1])
    target_lon = np.mod(np.asarray(target_longitude, dtype=np.float64), 360.0)
    yi = np.clip(np.searchsorted(lat, target_lat, side="right") - 1, 0, len(lat) - 2)
    wy = (target_lat - lat[yi]) / (lat[yi + 1] - lat[yi])
    xi = np.searchsorted(lon, target_lon, side="right") - 1
    xi %= len(lon)
    xi1 = (xi + 1) % len(lon)
    lon1 = np.where(xi1 == 0, lon[xi1] + 360.0, lon[xi1])
    adjusted = np.where(target_lon < lon[xi], target_lon + 360.0, target_lon)
    wx = (adjusted - lon[xi]) / (lon1 - lon[xi])
    corners = np.stack((field[..., yi[:, None], xi[None, :]],
                        field[..., yi[:, None], xi1[None, :]],
                        field[..., (yi + 1)[:, None], xi[None, :]],
                        field[..., (yi + 1)[:, None], xi1[None, :]]))
    weight_shape = (1,) * (field.ndim - 2)
    weights = np.stack((((1-wy)[:, None] * (1-wx)[None, :]),
                        ((1-wy)[:, None] * wx[None, :]),
                        (wy[:, None] * (1-wx)[None, :]),
                        (wy[:, None] * wx[None, :])))
    weights = weights.reshape((4,) + weight_shape + weights.shape[-2:])
    valid = np.isfinite(corners)
    denominator = np.sum(np.where(valid, weights, 0.0), axis=0)
    return np.divide(np.sum(np.where(valid, corners * weights, 0.0), axis=0),
                     denominator, out=np.full(denominator.shape, np.nan),
                     where=denominator > 0)


def field_for_definition(name: str, monthly_source: np.ndarray,
                         source_latitude: np.ndarray,
                         source_longitude: np.ndarray, *,
                         out_of_range: str = "error") -> np.ndarray:
    definition = SCALAR_DEFINITIONS[name]
    resampled = resample_regular(monthly_source, source_latitude, source_longitude,
                                 definition.latitude, definition.longitude)
    return encode_field(name, resampled, out_of_range=out_of_range)
