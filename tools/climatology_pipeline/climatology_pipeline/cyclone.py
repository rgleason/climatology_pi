"""IBTrACS v04r01 to the legacy Climatology cyclone representation.

IBTrACS' ``WMO_WIND`` field is deliberately not used: it combines agency
values with different averaging periods.  The USA agency fields provide the
only reasonably homogeneous global one-minute wind series in the archive.
Positions use the IBTrACS main-track latitude/longitude.
"""

from __future__ import annotations

from collections import defaultdict
import csv
from dataclasses import dataclass
from datetime import datetime
from pathlib import Path
from typing import Iterable, TextIO

from .legacy import CyclonePoint, CycloneTrack


PERIOD_START = datetime(1995, 1, 1)
PERIOD_END = datetime(2024, 12, 31, 23, 59, 59)
OUTPUT_BASINS = ("atl", "epa", "wpa", "spa", "nio", "she")
SOUTHERN_BASINS = frozenset(("spa", "she"))


@dataclass(frozen=True)
class ConversionStatistics:
    input_rows: int
    accepted_rows: int
    skipped_non_main: int
    skipped_time: int
    skipped_position: int
    tracks: int
    points_with_wind: int
    points_with_pressure: int


def _parse_time(value: str) -> datetime:
    return datetime.strptime(value.strip(), "%Y-%m-%d %H:%M:%S")


def _number(value: str) -> float | None:
    value = value.strip()
    if not value:
        return None
    try:
        return float(value)
    except ValueError:
        return None


def _legacy_state(nature: str, usa_status: str) -> str:
    """Map IBTrACS nature/status to the states understood by the plugin."""
    nature = nature.strip().upper()
    status = usa_status.strip().upper()
    if nature == "SS" or status in {"SS", "SD"}:
        return "S"
    if nature == "ET" or status in {"EX", "ET", "PT"}:
        return "E"
    if status == "WV":
        return "W"
    if status in {"LO", "DB"}:
        return "L"
    if nature == "TS" or status in {"TS", "TD", "TY", "HU", "ST", "TC", "HR"}:
        return "*"
    # Disturbance, mixed, not-reported and unknown records are retained as an
    # unknown state rather than invented as a tropical cyclone.
    return "D"


def _output_basin(basin: str, subbasin: str, longitude_east: float) -> str | None:
    basin = basin.strip().upper()
    subbasin = subbasin.strip().upper()
    if basin == "NA" or basin == "SA":
        return "atl"
    if basin == "EP":
        return "epa"
    if basin == "WP":
        return "wpa"
    if basin == "NI":
        return "nio"
    if basin == "SI":
        return "she"
    if basin == "SP":
        return "spa"
    # Some archive rows carry the central-Pacific designation only as a
    # sub-basin.  Keep the historical plugin's east-Pacific theatre.
    if subbasin == "CP":
        return "epa"
    # Defensive geographical fallbacks for any future basin code.
    if -60.0 <= longitude_east <= 20.0:
        return "atl"
    return None


def _legacy_longitude(longitude_east: float) -> int:
    """Encode longitude as the historical 0 .. -360 degree west convention."""
    longitude_east %= 360.0
    if longitude_east > 15.0:
        longitude_east -= 360.0
    return int(round(longitude_east * 10.0))


def _read_rows(stream: TextIO) -> tuple[dict[str, list[tuple[str, CyclonePoint]]], ConversionStatistics]:
    reader = csv.DictReader(stream)
    # NOAA places a units row immediately after the field-name row.
    try:
        next(reader)
    except StopIteration:
        return {}, ConversionStatistics(0, 0, 0, 0, 0, 0, 0, 0)

    grouped: dict[str, list[tuple[str, CyclonePoint]]] = defaultdict(list)
    input_rows = accepted = non_main = bad_time = bad_position = with_wind = with_pressure = 0
    for row in reader:
        input_rows += 1
        if row.get("TRACK_TYPE", "").strip().lower() != "main":
            non_main += 1
            continue
        try:
            moment = _parse_time(row.get("ISO_TIME", ""))
        except ValueError:
            bad_time += 1
            continue
        if not PERIOD_START <= moment <= PERIOD_END or moment.minute or moment.hour % 6:
            bad_time += 1
            continue
        latitude = _number(row.get("LAT", ""))
        longitude = _number(row.get("LON", ""))
        if latitude is None or longitude is None or not -90 < latitude < 90:
            bad_position += 1
            continue
        basin = _output_basin(row.get("BASIN", ""), row.get("SUBBASIN", ""), longitude)
        if basin is None:
            bad_position += 1
            continue

        wind = _number(row.get("USA_WIND", ""))
        pressure = _number(row.get("USA_PRES", ""))
        wind_i = 0 if wind is None else max(0, min(255, int(round(wind))))
        pressure_i = 0 if pressure is None else max(0, min(65535, int(round(pressure))))
        with_wind += wind is not None
        with_pressure += pressure is not None
        point = CyclonePoint(
            _legacy_state(row.get("NATURE", ""), row.get("USA_STATUS", "")),
            moment.year,
            moment.month,
            moment.day,
            moment.hour,
            int(round(latitude * 10.0)),
            _legacy_longitude(longitude),
            wind_i,
            pressure_i,
        )
        grouped[row["SID"].strip()].append((basin, point))
        accepted += 1

    statistics = ConversionStatistics(
        input_rows,
        accepted,
        non_main,
        bad_time,
        bad_position,
        len(grouped),
        with_wind,
        with_pressure,
    )
    return grouped, statistics


def convert_ibtracs(stream: TextIO) -> tuple[dict[str, tuple[CycloneTrack, ...]], ConversionStatistics]:
    """Convert an open IBTrACS CSV stream into six legacy theatre groups.

    A storm crossing a theatre boundary is split so every output track remains
    in the file selected by the runtime. Duplicate times are removed
    deterministically, retaining the first main-track record.
    """
    grouped, statistics = _read_rows(stream)
    outputs: dict[str, list[CycloneTrack]] = {name: [] for name in OUTPUT_BASINS}
    for sid in sorted(grouped):
        records = sorted(grouped[sid], key=lambda item: (
            item[1].year, item[1].month, item[1].day, item[1].hour
        ))
        current_basin: str | None = None
        current: list[CyclonePoint] = []
        seen_times: set[tuple[int, int, int, int]] = set()
        for basin, point in records:
            key = (point.year, point.month, point.day, point.hour)
            if key in seen_times:
                continue
            seen_times.add(key)
            if basin != current_basin and current:
                outputs[current_basin].append(CycloneTrack(tuple(current)))  # type: ignore[index]
                current = []
            current_basin = basin
            current.append(point)
        if current:
            outputs[current_basin].append(CycloneTrack(tuple(current)))  # type: ignore[index]
    return {name: tuple(outputs[name]) for name in OUTPUT_BASINS}, statistics


def load_ibtracs(path: str | Path) -> tuple[dict[str, tuple[CycloneTrack, ...]], ConversionStatistics]:
    with Path(path).open(newline="", encoding="utf-8-sig") as stream:
        return convert_ibtracs(stream)

