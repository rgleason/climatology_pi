"""ERA5 six-hour marine surface-field climatology."""

from __future__ import annotations

from dataclasses import dataclass
from pathlib import Path

import numpy as np

from .legacy import encode_scalar, write_gzip
from .scalar import SCALAR_DEFINITIONS, encode_field, relative_humidity, resample_regular


T2M = "2m_temperature"
D2M = "2m_dewpoint_temperature"
MSLP = "mean_sea_level_pressure"
TCC = "total_cloud_cover"
TP6 = "total_precipitation_6hr"
ERA5_ATMOSPHERE_VARIABLES = (T2M, D2M, MSLP, TCC, TP6)


@dataclass
class TargetMonthlyAccumulator:
    name: str

    def __post_init__(self) -> None:
        shape = SCALAR_DEFINITIONS[self.name].shape
        self.total = np.zeros(shape, dtype=np.float64)
        self.count = np.zeros(shape, dtype=np.uint32)

    def add(self, months: np.ndarray, values: np.ndarray) -> None:
        samples = np.asarray(values, dtype=np.float64)
        for month in np.unique(months):
            selected = samples[months == month]
            valid = np.isfinite(selected)
            index = int(month) - 1
            self.total[index] += np.sum(np.where(valid, selected, 0.0), axis=0)
            self.count[index] += np.sum(valid, axis=0, dtype=np.uint32)

    def physical(self, minimum_samples: int = 1) -> np.ndarray:
        return np.divide(self.total, self.count,
                         out=np.full(self.total.shape, np.nan),
                         where=self.count >= minimum_samples)


class AtmosphereAccumulator:
    def __init__(self) -> None:
        self.fields = {name: TargetMonthlyAccumulator(name) for name in (
            "sealevelpressure", "airtemperature", "cloud", "precipitation",
            "relativehumidity")}

    @staticmethod
    def validate_dataset(dataset) -> None:
        expected_units = {
            T2M: {"K"}, D2M: {"K"}, MSLP: {"Pa"},
            TCC: {"(0 - 1)", "1"}, TP6: {"m", "m of water equivalent"},
        }
        for name, units in expected_units.items():
            if name not in dataset:
                raise ValueError(f"ERA5 source lacks {name}")
            if tuple(dataset[name].dims) != ("time", "latitude", "longitude"):
                raise ValueError(f"{name} has unexpected dimensions {dataset[name].dims}")
            if dataset[name].attrs.get("units") not in units:
                raise ValueError(f"{name} has unexpected units {dataset[name].attrs.get('units')!r}")

    def add_block(self, dataset, sea_mask: np.ndarray) -> None:
        self.validate_dataset(dataset)
        times = np.asarray(dataset.time.values)
        months = times.astype("datetime64[M]").astype(np.int64) % 12 + 1
        source_lat = np.asarray(dataset.latitude.values)
        source_lon = np.asarray(dataset.longitude.values)
        physical = {
            "sealevelpressure": np.asarray(dataset[MSLP].values) / 100.0,
            "airtemperature": np.asarray(dataset[T2M].values) - 273.15,
            "cloud": np.asarray(dataset[TCC].values) * 100.0,
            # Each WeatherBench value is the preceding six-hour accumulation.
            "precipitation": np.asarray(dataset[TP6].values) * 1000.0 * 4.0,
            "relativehumidity": relative_humidity(dataset[T2M].values,
                                                   dataset[D2M].values),
        }
        for name, values in physical.items():
            values[:, ~sea_mask] = np.nan
            definition = SCALAR_DEFINITIONS[name]
            target = resample_regular(values, source_lat, source_lon,
                                      definition.latitude, definition.longitude)
            self.fields[name].add(months, target)

    def write(self, directory: str | Path, minimum_samples: int = 100) -> None:
        directory = Path(directory)
        directory.mkdir(parents=True, exist_ok=True)
        for name, accumulator in self.fields.items():
            encoded = encode_field(name, accumulator.physical(minimum_samples))
            write_gzip(directory / f"{name}.gz", encode_scalar(encoded, name))

    def save_checkpoint(self, path: str | Path) -> None:
        arrays = {}
        for name, field in self.fields.items():
            arrays[f"{name}_total"] = field.total
            arrays[f"{name}_count"] = field.count
        np.savez_compressed(path, **arrays)

    @classmethod
    def load_checkpoint(cls, path: str | Path) -> "AtmosphereAccumulator":
        result = cls()
        with np.load(path) as values:
            for name, field in result.fields.items():
                field.total = values[f"{name}_total"]
                field.count = values[f"{name}_count"]
        return result
