"""Streaming construction of the eight-sector Climatology wind atlas."""

from __future__ import annotations

from dataclasses import dataclass
from pathlib import Path

import numpy as np

from .legacy import WindAtlas


KNOTS_PER_MPS = 1.9438444924406048


@dataclass
class WindAccumulator:
    """Sufficient statistics for one or more calendar months.

    Arrays are small enough to checkpoint. Raw ERA5 chunks never need to be
    retained after :meth:`add` returns.
    """

    months: int = 12
    latitudes: int = 360
    longitudes: int = 720

    def __post_init__(self) -> None:
        shape = (self.months, self.latitudes, self.longitudes)
        self.total = np.zeros(shape, dtype=np.uint64)
        self.calm = np.zeros(shape, dtype=np.uint64)
        self.gale = np.zeros(shape, dtype=np.uint64)
        self.direction_count = np.zeros((*shape, 8), dtype=np.uint64)
        self.direction_speed_sum = np.zeros((*shape, 8), dtype=np.float64)

    @staticmethod
    def target_indices(latitudes: np.ndarray, longitudes: np.ndarray) -> tuple[np.ndarray, np.ndarray]:
        lat = np.asarray(latitudes, dtype=np.float64)
        lon = np.mod(np.asarray(longitudes, dtype=np.float64), 360.0)
        lati = np.floor((lat + 90.0) * 2.0).astype(np.int64)
        loni = np.floor(lon * 2.0).astype(np.int64)
        return np.clip(lati, 0, 359), np.mod(loni, 720)

    def add(
        self,
        month: int,
        u_mps: np.ndarray,
        v_mps: np.ndarray,
        latitudes: np.ndarray,
        longitudes: np.ndarray,
        *,
        sea_mask: np.ndarray | None = None,
    ) -> None:
        """Fold a `[time, latitude, longitude]` chunk into one month."""
        if not 1 <= month <= self.months:
            raise ValueError("month is one-based and must be in 1..12")
        u = np.asarray(u_mps, dtype=np.float64)
        v = np.asarray(v_mps, dtype=np.float64)
        if u.shape != v.shape or u.ndim != 3:
            raise ValueError("u/v must have identical [time, latitude, longitude] shapes")
        if u.shape[1:] != (len(latitudes), len(longitudes)):
            raise ValueError("coordinate lengths do not match wind arrays")
        if sea_mask is not None and np.asarray(sea_mask).shape != u.shape[1:]:
            raise ValueError("sea mask shape does not match the spatial grid")

        lat_index, lon_index = self.target_indices(latitudes, longitudes)
        cell_index = (lat_index[:, None] * self.longitudes + lon_index[None, :]).ravel()
        cells = self.latitudes * self.longitudes
        mask2 = np.ones(u.shape[1:], dtype=bool) if sea_mask is None else np.asarray(sea_mask, dtype=bool)
        # The legacy grid represents cells rather than multiple independent
        # observations inside each cell.  Spatially average U/V first, then
        # classify the resulting vector.  Counting four 0.25-degree source
        # points as four observations would artificially broaden the wind
        # rose and overweight partially resolved coasts.
        source_lat_ok = np.asarray(latitudes, dtype=np.float64) < 90.0
        mask2 = mask2 & source_lat_ok[:, None]
        target = month - 1

        for time_index in range(u.shape[0]):
            uf = u[time_index].ravel()
            vf = v[time_index].ravel()
            valid = np.isfinite(uf) & np.isfinite(vf) & mask2.ravel()
            if not np.any(valid):
                continue
            cell = cell_index[valid]
            count = np.bincount(cell, minlength=cells)
            present = count != 0
            us = np.bincount(cell, weights=uf[valid], minlength=cells)[present] / count[present]
            vs = np.bincount(cell, weights=vf[valid], minlength=cells)[present] / count[present]
            cell = np.flatnonzero(present)
            speed = np.hypot(us, vs) * KNOTS_PER_MPS
            angle = np.mod(np.degrees(np.arctan2(-us, -vs)), 360.0)
            sector = np.floor((angle + 22.5) / 45.0).astype(np.int8) % 8

            self.total[target].ravel()[:] += np.bincount(cell, minlength=cells).astype(np.uint64)
            self.calm[target].ravel()[:] += np.bincount(
                cell[speed <= 3.0], minlength=cells
            ).astype(np.uint64)
            self.gale[target].ravel()[:] += np.bincount(
                cell[speed >= 34.0], minlength=cells
            ).astype(np.uint64)
            combined = cell * 8 + sector
            self.direction_count[target].reshape(-1)[:] += np.bincount(
                combined, minlength=cells * 8
            ).astype(np.uint64)
            self.direction_speed_sum[target].reshape(-1)[:] += np.bincount(
                combined, weights=speed, minlength=cells * 8
            )

    def atlas(
        self,
        month: int,
        *,
        minimum_samples: int = 1,
        prune_probability: float | None = None,
        direction_resolution: int = 50,
        speed_multiplier: int = 1,
    ) -> WindAtlas:
        if not 1 <= month <= self.months:
            raise ValueError("month is one-based and must be in 1..12")
        index = month - 1
        total = self.total[index]
        counts = self.direction_count[index].copy()
        valid = total >= minimum_samples
        if prune_probability is not None:
            if not 0 <= prune_probability < 1:
                raise ValueError("prune probability must be in [0, 1)")
            counts[counts <= total[:, :, None] * prune_probability] = 0
        retained = np.sum(counts, axis=2)
        valid &= retained > 0

        frequency = np.zeros_like(counts, dtype=np.uint8)
        probability = np.divide(
            counts,
            retained[:, :, None],
            out=np.zeros(counts.shape, dtype=np.float64),
            where=retained[:, :, None] != 0,
        )
        # Largest-remainder quantisation preserves an exact total of
        # ``direction_resolution`` at every valid cell. Independent rounding
        # can otherwise produce wind roses totalling 96% or 104%.
        scaled = probability[valid] * direction_resolution
        quantised = np.floor(scaled).astype(np.uint16)
        remaining = direction_resolution - np.sum(quantised, axis=1)
        order = np.argsort(-(scaled - quantised), axis=1)
        rows = np.arange(len(quantised))
        for rank in range(8):
            add = remaining > rank
            quantised[rows[add], order[add, rank]] += 1
        frequency[valid] = np.clip(quantised, 0, 255).astype(np.uint8)

        mean_speed = np.divide(
            self.direction_speed_sum[index],
            self.direction_count[index],
            out=np.zeros(counts.shape, dtype=np.float64),
            where=self.direction_count[index] != 0,
        )
        speeds = np.clip(np.rint(mean_speed * speed_multiplier), 0, 255).astype(np.uint8)
        speeds[frequency == 0] = 0

        calm = np.zeros(total.shape, dtype=np.uint8)
        gale = np.zeros(total.shape, dtype=np.uint8)
        calm[valid] = np.clip(
            np.rint(100 * self.calm[index][valid] / total[valid]), 0, 100
        ).astype(np.uint8)
        gale[valid] = np.clip(
            np.rint(100 * self.gale[index][valid] / total[valid]), 0, 100
        ).astype(np.uint8)
        return WindAtlas(
            frequency,
            speeds,
            calm,
            gale,
            valid,
            direction_resolution,
            speed_multiplier,
            1,
        )

    def save_checkpoint(self, path: str | Path) -> None:
        np.savez_compressed(
            path,
            total=self.total,
            calm=self.calm,
            gale=self.gale,
            direction_count=self.direction_count,
            direction_speed_sum=self.direction_speed_sum,
        )

    def merge(self, other: "WindAccumulator") -> None:
        """Add an independently accumulated, non-overlapping time range."""
        names = ("total", "calm", "gale", "direction_count",
                 "direction_speed_sum")
        if any(getattr(self, name).shape != getattr(other, name).shape
               for name in names):
            raise ValueError("wind checkpoint grids differ")
        for name in ("total", "calm", "gale", "direction_count"):
            left = getattr(self, name)
            right = getattr(other, name)
            if np.any(np.iinfo(left.dtype).max - left < right):
                raise OverflowError(f"wind checkpoint {name} would overflow")
            left += right
        self.direction_speed_sum += other.direction_speed_sum

    @classmethod
    def load_checkpoint(cls, path: str | Path) -> "WindAccumulator":
        with np.load(path) as values:
            obj = cls(values["total"].shape[0], values["total"].shape[1], values["total"].shape[2])
            for name in ("total", "calm", "gale", "direction_count", "direction_speed_sum"):
                setattr(obj, name, values[name])
        return obj
