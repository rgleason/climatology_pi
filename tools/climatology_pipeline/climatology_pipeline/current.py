"""Streaming OSCAR V2 current climatology aggregation and resampling."""

from __future__ import annotations

from dataclasses import dataclass
import json
import os
from pathlib import Path
import tempfile

import numpy as np

from .legacy import CurrentField
from .wind import KNOTS_PER_MPS


@dataclass
class CurrentAccumulator:
    latitudes: np.ndarray
    longitudes: np.ndarray

    def __post_init__(self) -> None:
        self.latitudes = np.asarray(self.latitudes, dtype=np.float64)
        self.longitudes = np.mod(np.asarray(self.longitudes, dtype=np.float64), 360.0)
        if self.latitudes.ndim != 1 or self.longitudes.ndim != 1:
            raise ValueError("current coordinates must be one-dimensional")
        shape = (12, len(self.latitudes), len(self.longitudes))
        self.u_sum = np.zeros(shape, dtype=np.float64)
        self.v_sum = np.zeros(shape, dtype=np.float64)
        self.count = np.zeros(shape, dtype=np.uint32)

    def add(self, month: int, u_mps: np.ndarray, v_mps: np.ndarray) -> None:
        if not 1 <= month <= 12:
            raise ValueError("month must be in 1..12")
        u = np.asarray(u_mps, dtype=np.float64)
        v = np.asarray(v_mps, dtype=np.float64)
        if u.shape != v.shape:
            raise ValueError("OSCAR U/V shapes differ")
        if u.ndim == 2:
            u = u[None, :, :]
            v = v[None, :, :]
        if u.ndim != 3 or u.shape[1:] != (len(self.latitudes), len(self.longitudes)):
            raise ValueError("OSCAR fields must be [time, latitude, longitude]")
        valid = np.isfinite(u) & np.isfinite(v)
        index = month - 1
        self.u_sum[index] += np.sum(np.where(valid, u, 0.0), axis=0)
        self.v_sum[index] += np.sum(np.where(valid, v, 0.0), axis=0)
        self.count[index] += np.sum(valid, axis=0, dtype=np.uint32)

    def monthly_source(self, month: int, minimum_samples: int = 1) -> tuple[np.ndarray, np.ndarray]:
        index = month - 1
        valid = self.count[index] >= minimum_samples
        u = np.full(valid.shape, np.nan)
        v = np.full(valid.shape, np.nan)
        u[valid] = self.u_sum[index][valid] / self.count[index][valid]
        v[valid] = self.v_sum[index][valid] / self.count[index][valid]
        return u, v

    @staticmethod
    def _bilinear(
        values: np.ndarray,
        source_lat: np.ndarray,
        source_lon: np.ndarray,
        target_lat: np.ndarray,
        target_lon: np.ndarray,
    ) -> np.ndarray:
        lat = np.asarray(source_lat)
        field = np.asarray(values)
        if lat[0] > lat[-1]:
            lat = lat[::-1]
            field = field[::-1]
        lon_order = np.argsort(np.mod(source_lon, 360.0))
        lon = np.mod(source_lon, 360.0)[lon_order]
        field = field[:, lon_order]

        yi = np.searchsorted(lat, target_lat, side="right") - 1
        yi = np.clip(yi, 0, len(lat) - 2)
        yweight = (target_lat - lat[yi]) / (lat[yi + 1] - lat[yi])
        target_lon = np.mod(target_lon, 360.0)
        xi = np.searchsorted(lon, target_lon, side="right") - 1
        xi %= len(lon)
        xi1 = (xi + 1) % len(lon)
        lon0 = lon[xi]
        lon1 = lon[xi1]
        lon1 = np.where(xi1 == 0, lon1 + 360.0, lon1)
        adjusted = np.where(target_lon < lon0, target_lon + 360.0, target_lon)
        xweight = (adjusted - lon0) / (lon1 - lon0)

        corners = np.stack((
            field[yi[:, None], xi[None, :]],
            field[yi[:, None], xi1[None, :]],
            field[(yi + 1)[:, None], xi[None, :]],
            field[(yi + 1)[:, None], xi1[None, :]],
        ))
        weights = np.stack((
            (1-yweight)[:, None] * (1-xweight)[None, :],
            (1-yweight)[:, None] * xweight[None, :],
            yweight[:, None] * (1-xweight)[None, :],
            yweight[:, None] * xweight[None, :],
        ))
        finite = np.isfinite(corners)
        denominator = np.sum(np.where(finite, weights, 0.0), axis=0)
        return np.divide(
            np.sum(np.where(finite, corners * weights, 0.0), axis=0),
            denominator,
            out=np.full(denominator.shape, np.nan),
            where=denominator > 0,
        )

    def field(self, month: int, *, minimum_samples: int = 1, multiplier: int = 20) -> CurrentField:
        u, v = self.monthly_source(month, minimum_samples)
        # Exact coordinate convention consumed by CurrentData::InterpCurrent.
        target_lat = 80.0 - np.arange(481) / 3.0
        target_lon = np.arange(1080) / 3.0
        target_u = self._bilinear(u, self.latitudes, self.longitudes, target_lat, target_lon)
        target_v = self._bilinear(v, self.latitudes, self.longitudes, target_lat, target_lon)
        both = np.isfinite(target_u) & np.isfinite(target_v)
        target_u[~both] = np.nan
        target_v[~both] = np.nan
        return CurrentField(target_u * KNOTS_PER_MPS, target_v * KNOTS_PER_MPS, multiplier)

    def save_checkpoint(self, path: str | Path,
                        metadata: dict[str, object] | None = None) -> None:
        path = Path(path)
        path.parent.mkdir(parents=True, exist_ok=True)
        payload: dict[str, object] = {
            "latitudes": self.latitudes,
            "longitudes": self.longitudes,
            "u_sum": self.u_sum,
            "v_sum": self.v_sum,
            "count": self.count,
        }
        if metadata is not None:
            payload["metadata_json"] = np.asarray(json.dumps(metadata, sort_keys=True))
        handle, temporary_name = tempfile.mkstemp(
            prefix=path.name + ".", suffix=".tmp", dir=path.parent
        )
        try:
            with os.fdopen(handle, "wb") as temporary:
                np.savez_compressed(temporary, **payload)
                temporary.flush()
                os.fsync(temporary.fileno())
            os.replace(temporary_name, path)
        except Exception:
            try:
                os.unlink(temporary_name)
            except FileNotFoundError:
                pass
            raise

    @staticmethod
    def checkpoint_metadata(path: str | Path) -> dict[str, object] | None:
        with np.load(path) as values:
            if "metadata_json" not in values:
                return None
            parsed = json.loads(str(values["metadata_json"]))
            if not isinstance(parsed, dict):
                raise ValueError("current checkpoint metadata is not an object")
            return parsed

    def merge(self, other: "CurrentAccumulator") -> None:
        """Add a non-overlapping time partition without quantisation loss."""
        if (not np.array_equal(self.latitudes, other.latitudes) or
                not np.array_equal(self.longitudes, other.longitudes)):
            raise ValueError("current checkpoint grids differ")
        if np.any(np.iinfo(self.count.dtype).max - self.count < other.count):
            raise OverflowError("current checkpoint sample count would overflow")
        self.u_sum += other.u_sum
        self.v_sum += other.v_sum
        self.count += other.count

    @classmethod
    def load_checkpoint(cls, path: str | Path) -> "CurrentAccumulator":
        with np.load(path) as values:
            result = cls(values["latitudes"], values["longitudes"])
            result.u_sum = values["u_sum"]
            result.v_sum = values["v_sum"]
            result.count = values["count"]
        return result
