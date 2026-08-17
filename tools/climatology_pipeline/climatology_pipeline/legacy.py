"""Strict codecs for the binary formats consumed by climatology_pi.

The historical files have no general container or byte-order marker.  The
released files are little-endian, and this module deliberately uses explicit
little-endian types rather than reproducing the generators' host-endian bug.
"""

from __future__ import annotations

from dataclasses import dataclass
import gzip
from pathlib import Path
import struct
from typing import BinaryIO, Iterable

import numpy as np


class FormatError(ValueError):
    """Raised when a climatology binary is malformed or inconsistent."""


WIND_MAGIC = 0xFEFE
WIND_EXTRAS_MAGIC = b"WEX1"
WIND_DIRECTIONS = 8
MISSING_U8 = 255
MISSING_I8 = -128


@dataclass(frozen=True)
class WindAtlas:
    frequencies: np.ndarray  # uint8 [lat, lon, direction], units 1/resolution
    speeds: np.ndarray  # uint8 [lat, lon, direction], units 1/speed_multiplier kn
    calm: np.ndarray  # uint8 [lat, lon], percent; legacy stores calm OR gale
    gale: np.ndarray  # uint8 [lat, lon], percent; legacy stores calm OR gale
    valid: np.ndarray  # bool [lat, lon]
    direction_resolution: int = 50
    speed_numerator: int = 1
    speed_denominator: int = 1

    @property
    def speed_multiplier(self) -> float:
        return self.speed_numerator / self.speed_denominator


@dataclass(frozen=True)
class CurrentField:
    u: np.ndarray  # float64 [lat, lon], knots
    v: np.ndarray  # float64 [lat, lon], knots
    multiplier: int = 20


@dataclass(frozen=True)
class CyclonePoint:
    state: str
    year: int
    month: int
    day: int
    hour: int
    latitude_tenths: int
    longitude_tenths: int
    wind_knots: int
    pressure_hpa: int


@dataclass(frozen=True)
class CycloneTrack:
    points: tuple[CyclonePoint, ...]


def _read_payload(source: str | Path | bytes | bytearray | memoryview) -> bytes:
    if isinstance(source, (bytes, bytearray, memoryview)):
        return bytes(source)
    path = Path(source)
    try:
        if path.suffix.lower() == ".gz":
            with gzip.open(path, "rb") as stream:
                return stream.read()
        return path.read_bytes()
    except (OSError, EOFError) as exc:
        raise FormatError(f"cannot read {path}: {exc}") from exc


def _checked_cells(latitudes: int, longitudes: int, *, maximum: int) -> int:
    if latitudes <= 0 or longitudes <= 0:
        raise FormatError("grid dimensions must be positive")
    cells = latitudes * longitudes
    if cells > maximum:
        raise FormatError(f"grid is unreasonably large: {latitudes} x {longitudes}")
    return cells


def decode_wind(source: str | Path | bytes) -> WindAtlas:
    payload = _read_payload(source)
    if len(payload) < 14:
        raise FormatError("wind file is shorter than its 14-byte header")
    magic, nlat, nlon, ndir, resolution, speed_num, speed_den = struct.unpack_from(
        "<7H", payload
    )
    if magic != WIND_MAGIC:
        raise FormatError(f"unsupported wind magic 0x{magic:04x}")
    cells = _checked_cells(nlat, nlon, maximum=180 * 16 * 360 * 16)
    if ndir != WIND_DIRECTIONS:
        raise FormatError(f"expected 8 wind sectors, found {ndir}")
    if not resolution or not speed_num or not speed_den:
        raise FormatError("wind scale values must be non-zero")

    cursor = 14
    if len(payload) < cursor + cells:
        raise FormatError("wind file is truncated in the calm/gale plane")
    marker = np.frombuffer(payload, dtype=np.uint8, count=cells, offset=cursor).copy()
    cursor += cells
    valid_flat = marker <= 200
    valid_count = int(np.count_nonzero(valid_flat))

    frequencies = np.zeros((cells, ndir), dtype=np.uint8)
    for sector in range(ndir):
        end = cursor + valid_count
        if end > len(payload):
            raise FormatError(f"wind file is truncated in direction sector {sector}")
        frequencies[valid_flat, sector] = np.frombuffer(
            payload, dtype=np.uint8, count=valid_count, offset=cursor
        )
        cursor = end

    speeds = np.zeros((cells, ndir), dtype=np.uint8)
    for sector in range(ndir):
        present = valid_flat & (frequencies[:, sector] != 0)
        count = int(np.count_nonzero(present))
        end = cursor + count
        if end > len(payload):
            raise FormatError(f"wind file is truncated in speed sector {sector}")
        speeds[present, sector] = np.frombuffer(
            payload, dtype=np.uint8, count=count, offset=cursor
        )
        cursor = end
    if cursor != len(payload):
        raise FormatError(f"wind file contains {len(payload) - cursor} trailing bytes")

    calm = np.zeros(cells, dtype=np.uint8)
    gale = np.zeros(cells, dtype=np.uint8)
    calm[valid_flat & (marker < 100)] = marker[valid_flat & (marker < 100)]
    gale_mask = valid_flat & (marker >= 100)
    gale[gale_mask] = marker[gale_mask] - 100
    shape = (nlat, nlon)
    return WindAtlas(
        frequencies.reshape((*shape, ndir)),
        speeds.reshape((*shape, ndir)),
        calm.reshape(shape),
        gale.reshape(shape),
        valid_flat.reshape(shape),
        resolution,
        speed_num,
        speed_den,
    )


def encode_wind(atlas: WindAtlas) -> bytes:
    frequencies = np.asarray(atlas.frequencies, dtype=np.uint8)
    speeds = np.asarray(atlas.speeds, dtype=np.uint8)
    calm = np.asarray(atlas.calm)
    gale = np.asarray(atlas.gale)
    valid = np.asarray(atlas.valid, dtype=bool)
    if frequencies.ndim != 3 or frequencies.shape[2] != WIND_DIRECTIONS:
        raise ValueError("frequencies must have shape [lat, lon, 8]")
    if speeds.shape != frequencies.shape or calm.shape != frequencies.shape[:2]:
        raise ValueError("wind array shapes are inconsistent")
    if gale.shape != calm.shape or valid.shape != calm.shape:
        raise ValueError("wind marker array shapes are inconsistent")
    if np.any(calm < 0) or np.any(calm > 100) or np.any(gale < 0) or np.any(gale > 100):
        raise ValueError("calm and gale values must be percentages in [0, 100]")
    nlat, nlon, _ = frequencies.shape
    _checked_cells(nlat, nlon, maximum=180 * 16 * 360 * 16)
    if not atlas.direction_resolution or not atlas.speed_numerator or not atlas.speed_denominator:
        raise ValueError("wind scale values must be non-zero")

    marker = np.full((nlat, nlon), MISSING_U8, dtype=np.uint8)
    # This intentionally reproduces the lossy historical choice.  The enhanced
    # schema keeps both percentages; a legacy file physically cannot.
    use_gale = valid & (1.5 * gale > calm)
    # 100 is the gale discriminator in the old reader, so an exact 100% calm
    # cell is only representable as 99%.
    marker[valid & ~use_gale] = np.minimum(
        99, np.rint(calm[valid & ~use_gale])
    ).astype(np.uint8)
    marker[use_gale] = 100 + np.rint(gale[use_gale]).astype(np.uint8)

    parts = [
        struct.pack(
            "<7H",
            WIND_MAGIC,
            nlat,
            nlon,
            WIND_DIRECTIONS,
            atlas.direction_resolution,
            atlas.speed_numerator,
            atlas.speed_denominator,
        ),
        marker.tobytes(order="C"),
    ]
    for sector in range(WIND_DIRECTIONS):
        parts.append(frequencies[:, :, sector][valid].tobytes(order="C"))
    for sector in range(WIND_DIRECTIONS):
        present = valid & (frequencies[:, :, sector] != 0)
        parts.append(speeds[:, :, sector][present].tobytes(order="C"))
    return b"".join(parts)


def encode_wind_extras(atlas: WindAtlas) -> bytes:
    """Encode optional calm *and* gale planes without breaking old readers."""
    calm = np.asarray(atlas.calm)
    gale = np.asarray(atlas.gale)
    valid = np.asarray(atlas.valid, dtype=bool)
    if calm.ndim != 2 or gale.shape != calm.shape or valid.shape != calm.shape:
        raise ValueError("wind extras arrays must share a two-dimensional shape")
    if np.any(calm < 0) or np.any(calm > 100) or np.any(gale < 0) or np.any(gale > 100):
        raise ValueError("calm/gale values must be percentages in [0, 100]")
    nlat, nlon = calm.shape
    _checked_cells(nlat, nlon, maximum=180 * 16 * 360 * 16)
    calm_out = np.full(calm.shape, MISSING_U8, dtype=np.uint8)
    gale_out = np.full(gale.shape, MISSING_U8, dtype=np.uint8)
    calm_out[valid] = np.rint(calm[valid]).astype(np.uint8)
    gale_out[valid] = np.rint(gale[valid]).astype(np.uint8)
    return WIND_EXTRAS_MAGIC + struct.pack("<2H", nlat, nlon) + calm_out.tobytes() + gale_out.tobytes()


def decode_wind_extras(source: str | Path | bytes) -> tuple[np.ndarray, np.ndarray, np.ndarray]:
    payload = _read_payload(source)
    if len(payload) < 8 or payload[:4] != WIND_EXTRAS_MAGIC:
        raise FormatError("unsupported wind-extras header")
    nlat, nlon = struct.unpack_from("<2H", payload, 4)
    cells = _checked_cells(nlat, nlon, maximum=180 * 16 * 360 * 16)
    if len(payload) != 8 + 2 * cells:
        raise FormatError("wind-extras payload has the wrong length")
    raw = np.frombuffer(payload, dtype=np.uint8, count=2 * cells, offset=8).reshape(2, nlat, nlon).copy()
    valid = (raw[0] != MISSING_U8) & (raw[1] != MISSING_U8)
    if np.any(raw[:, valid] > 100):
        raise FormatError("wind-extras percentage exceeds 100")
    calm = raw[0]
    gale = raw[1]
    calm[~valid] = 0
    gale[~valid] = 0
    return calm, gale, valid


def decode_current(source: str | Path | bytes) -> CurrentField:
    payload = _read_payload(source)
    if len(payload) < 6:
        raise FormatError("current file is shorter than its 6-byte header")
    nlat, nlon, multiplier = struct.unpack_from("<3H", payload)
    cells = _checked_cells(nlat, nlon, maximum=4096 * 8192)
    if multiplier == 0:
        raise FormatError("current multiplier is zero")
    expected = 6 + 2 * cells
    if len(payload) != expected:
        raise FormatError(f"current payload length is {len(payload)}, expected {expected}")
    raw = np.frombuffer(payload, dtype=np.int8, count=2 * cells, offset=6).reshape(2, nlat, nlon)
    values = raw.astype(np.float64) / multiplier
    values[raw == MISSING_I8] = np.nan
    return CurrentField(values[0], values[1], multiplier)


def encode_current(field: CurrentField) -> bytes:
    u = np.asarray(field.u, dtype=np.float64)
    v = np.asarray(field.v, dtype=np.float64)
    if u.ndim != 2 or u.shape != v.shape:
        raise ValueError("current u/v arrays must have the same two-dimensional shape")
    nlat, nlon = u.shape
    _checked_cells(nlat, nlon, maximum=4096 * 8192)
    if not 1 <= field.multiplier <= 65535:
        raise ValueError("current multiplier must fit a non-zero uint16")
    parts = [struct.pack("<3H", nlat, nlon, field.multiplier)]
    for values in (u, v):
        scaled = np.rint(values * field.multiplier)
        finite = np.isfinite(values)
        if np.any(finite & ((scaled < -127) | (scaled > 127))):
            raise ValueError("current value cannot be represented without int8 overflow")
        raw = np.full(values.shape, MISSING_I8, dtype=np.int8)
        raw[finite] = scaled[finite].astype(np.int8)
        parts.append(raw.tobytes(order="C"))
    return b"".join(parts)


SCALAR_SCHEMAS: dict[str, tuple[tuple[int, ...], np.dtype]] = {
    "sealevelpressure": ((12, 90, 180), np.dtype("<i2")),
    "seasurfacetemperature": ((12, 180, 360), np.dtype("i1")),
    "airtemperature": ((12, 90, 180), np.dtype("i1")),
    "cloud": ((12, 90, 180), np.dtype("u1")),
    "precipitation": ((12, 72, 144), np.dtype("u1")),
    "relativehumidity": ((12, 180, 360), np.dtype("u1")),
    "lightning": ((12, 180, 360), np.dtype("u1")),
    "seadepth": ((180, 360), np.dtype("i1")),
}


def decode_scalar(source: str | Path | bytes, schema: str) -> np.ndarray:
    try:
        shape, dtype = SCALAR_SCHEMAS[schema]
    except KeyError as exc:
        raise ValueError(f"unknown scalar schema {schema!r}") from exc
    payload = _read_payload(source)
    expected = int(np.prod(shape)) * dtype.itemsize
    if len(payload) != expected:
        raise FormatError(f"{schema} payload length is {len(payload)}, expected {expected}")
    return np.frombuffer(payload, dtype=dtype).copy().reshape(shape)


def encode_scalar(values: np.ndarray, schema: str) -> bytes:
    try:
        shape, dtype = SCALAR_SCHEMAS[schema]
    except KeyError as exc:
        raise ValueError(f"unknown scalar schema {schema!r}") from exc
    array = np.asarray(values)
    if array.shape != shape:
        raise ValueError(f"{schema} must have shape {shape}, got {array.shape}")
    info = np.iinfo(dtype)
    if np.any(array < info.min) or np.any(array > info.max):
        raise ValueError(f"{schema} contains values outside {dtype} range")
    return array.astype(dtype).tobytes(order="C")


_CYCLONE_STATES = frozenset("*SEWL DX".replace(" ", ""))


def decode_cyclones(source: str | Path | bytes, *, southern: bool = False) -> tuple[CycloneTrack, ...]:
    payload = _read_payload(source)
    cursor = 0
    tracks: list[CycloneTrack] = []
    while cursor < len(payload):
        if cursor + 2 > len(payload):
            raise FormatError("cyclone file ends within a year field")
        year = struct.unpack_from("<H", payload, cursor)[0]
        cursor += 2
        points: list[CyclonePoint] = []
        last_month = 0
        while True:
            if cursor >= len(payload):
                raise FormatError("cyclone track has no terminator")
            state_byte = struct.unpack_from("<b", payload, cursor)[0]
            cursor += 1
            if state_byte == MISSING_I8:
                break
            state = chr(state_byte & 0xFF)
            if state not in _CYCLONE_STATES:
                raise FormatError(f"invalid cyclone state byte {state_byte} at {cursor - 1}")
            if cursor + 9 > len(payload):
                raise FormatError("cyclone point is truncated")
            dayhour, month, lat, lon, wind, pressure = struct.unpack_from(
                "<BBhhBH", payload, cursor
            )
            cursor += 9
            if month < last_month:
                year += 1
            last_month = month
            day, quarter = divmod(dayhour, 4)
            hour = quarter * 6
            if not 1 <= month <= 12 or not 1 <= day <= 31:
                raise FormatError(f"invalid cyclone date month={month} day={day}")
            latitude = -lat if southern else lat
            if not -900 < latitude < 900 or not -3600 <= lon <= 3600:
                raise FormatError("cyclone coordinate is outside the supported range")
            points.append(
                CyclonePoint(state, year, month, day, hour, latitude, lon, wind, pressure)
            )
        # Empty tracks occur in the released files and are accepted by the
        # production reader.  Preserve them for an exact legacy decode.
        tracks.append(CycloneTrack(tuple(points)))
    return tuple(tracks)


def encode_cyclones(tracks: Iterable[CycloneTrack], *, southern: bool = False) -> bytes:
    parts: list[bytes] = []
    for track in tracks:
        points = tuple(track.points)
        if not points:
            raise ValueError("cyclone tracks cannot be empty")
        parts.append(struct.pack("<H", points[0].year))
        previous_month = 0
        current_year = points[0].year
        for point in points:
            if point.state not in _CYCLONE_STATES:
                raise ValueError(f"unsupported cyclone state {point.state!r}")
            if point.year not in (current_year, current_year + 1):
                raise ValueError("legacy cyclone tracks can only advance one year at a month wrap")
            if point.year == current_year + 1:
                if point.month >= previous_month:
                    raise ValueError("year advance requires month wrap in legacy cyclone format")
                current_year += 1
            dayhour = point.day * 4 + point.hour // 6
            lat = -point.latitude_tenths if southern else point.latitude_tenths
            parts.append(point.state.encode("ascii"))
            parts.append(
                struct.pack(
                    "<BBhhBH",
                    dayhour,
                    point.month,
                    lat,
                    point.longitude_tenths,
                    point.wind_knots,
                    point.pressure_hpa,
                )
            )
            previous_month = point.month
        parts.append(struct.pack("<b", MISSING_I8))
    return b"".join(parts)


def write_gzip(path: str | Path, payload: bytes, *, mtime: int = 0) -> None:
    """Write deterministic gzip output."""
    with Path(path).open("wb") as raw:
        with gzip.GzipFile(filename="", mode="wb", fileobj=raw, mtime=mtime) as stream:
            stream.write(payload)
