from __future__ import annotations

import gzip
from pathlib import Path
import struct

import numpy as np
import pytest

from climatology_pipeline.legacy import (
    CyclonePoint,
    CycloneTrack,
    CurrentField,
    FormatError,
    SCALAR_SCHEMAS,
    WindAtlas,
    decode_current,
    decode_cyclones,
    decode_scalar,
    decode_wind,
    decode_wind_extras,
    encode_current,
    encode_cyclones,
    encode_scalar,
    encode_wind,
    encode_wind_extras,
)


ROOT = Path(__file__).resolve().parents[3]
DATA = ROOT / "data"


@pytest.mark.parametrize("month", range(1, 13))
def test_every_released_wind_file_decodes(month: int) -> None:
    atlas = decode_wind(DATA / f"wind{month:02d}.gz")
    assert atlas.frequencies.shape == (360, 720, 8)
    assert atlas.direction_resolution == 50
    assert atlas.speed_multiplier == 1
    assert 100_000 < np.count_nonzero(atlas.valid) < 259_200
    assert np.max(atlas.speeds) < 100
    assert np.all(atlas.frequencies[~atlas.valid] == 0)


@pytest.mark.parametrize("month", range(1, 13))
def test_every_released_current_file_decodes(month: int) -> None:
    current = decode_current(DATA / f"current{month:02d}.gz")
    assert current.u.shape == (481, 1080)
    assert current.multiplier == 20
    assert np.nanmax(np.hypot(current.u, current.v)) < 8
    assert np.count_nonzero(np.isnan(current.u)) > 0


@pytest.mark.parametrize("schema", SCALAR_SCHEMAS)
def test_released_scalar_file_has_exact_schema(schema: str) -> None:
    values = decode_scalar(DATA / f"{schema}.gz", schema)
    assert values.shape == SCALAR_SCHEMAS[schema][0]


@pytest.mark.parametrize(("filename", "southern"), [
    ("cyclone-atl.gz", False), ("cyclone-epa.gz", False),
    ("cyclone-nio.gz", False), ("cyclone-wpa.gz", False),
    ("cyclone-she.gz", True), ("cyclone-spa.gz", True),
])
def test_every_released_cyclone_file_decodes(filename: str, southern: bool) -> None:
    tracks = decode_cyclones(DATA / filename, southern=southern)
    assert len(tracks) > 100
    populated = [track for track in tracks if track.points]
    assert len(populated) > 100
    assert min(point.year for track in populated for point in track.points) >= 1850


def test_released_atlantic_history_exposes_generator_cutoff_mismatch() -> None:
    tracks = decode_cyclones(DATA / "cyclone-atl.gz")
    # Despite gencyclonedata.cpp claiming a 1968 cutoff, the released Atlantic
    # file begins in 1851.  This mismatch is intentionally regression-tested.
    assert min(point.year for track in tracks for point in track.points) == 1851


def test_wind_round_trip_and_independent_wire_layout() -> None:
    valid = np.array([[True, True], [False, True]])
    frequency = np.zeros((2, 2, 8), dtype=np.uint8)
    speed = np.zeros_like(frequency)
    frequency[0, 0, 0] = 25
    frequency[0, 0, 2] = 25
    speed[0, 0, 0] = 12
    speed[0, 0, 2] = 20
    frequency[0, 1, 7] = 50
    speed[0, 1, 7] = 7
    frequency[1, 1, 4] = 50
    speed[1, 1, 4] = 35
    atlas = WindAtlas(
        frequency,
        speed,
        np.array([[5, 80], [0, 2]], dtype=np.uint8),
        np.array([[3, 1], [0, 20]], dtype=np.uint8),
        valid,
    )
    encoded = encode_wind(atlas)
    assert encoded[:14] == struct.pack("<7H", 0xFEFE, 2, 2, 8, 50, 1, 1)
    # marker plane: calm 5, calm 80, missing, gale 20
    assert encoded[14:18] == bytes([5, 80, 255, 120])
    decoded = decode_wind(encoded)
    np.testing.assert_array_equal(decoded.valid, valid)
    np.testing.assert_array_equal(decoded.frequencies, frequency)
    np.testing.assert_array_equal(decoded.speeds, speed)
    assert decoded.calm[0, 0] == 5 and decoded.gale[0, 0] == 0
    assert decoded.gale[1, 1] == 20 and decoded.calm[1, 1] == 0


def test_current_round_trip_quantisation_and_missing() -> None:
    field = CurrentField(
        np.array([[0.0, 1.26], [np.nan, -2.49]]),
        np.array([[0.5, -0.5], [np.nan, 3.0]]),
        multiplier=20,
    )
    decoded = decode_current(encode_current(field))
    np.testing.assert_allclose(decoded.u, [[0, 1.25], [np.nan, -2.5]], equal_nan=True)
    np.testing.assert_allclose(decoded.v, [[0.5, -0.5], [np.nan, 3]], equal_nan=True)


def test_scalar_round_trip() -> None:
    shape, _ = SCALAR_SCHEMAS["seadepth"]
    values = np.arange(np.prod(shape), dtype=np.int64).reshape(shape) % 127
    np.testing.assert_array_equal(
        decode_scalar(encode_scalar(values, "seadepth"), "seadepth"), values
    )


def test_cyclone_round_trip_across_year() -> None:
    track = CycloneTrack(
        (
            CyclonePoint("*", 2023, 12, 31, 18, 120, -450, 50, 990),
            CyclonePoint("E", 2024, 1, 1, 0, 125, -455, 45, 995),
        )
    )
    assert decode_cyclones(encode_cyclones([track])) == (track,)


@pytest.mark.parametrize(
    "decoder,payload",
    [
        (decode_wind, b"\xfe\xfe"),
        (decode_current, struct.pack("<3H", 481, 1080, 20) + b"\0"),
        (decode_cyclones, struct.pack("<H", 2000) + b"*"),
    ],
)
def test_malformed_files_are_rejected(decoder, payload: bytes) -> None:
    with pytest.raises(FormatError):
        decoder(payload)


def test_trailing_wind_data_is_rejected() -> None:
    frequency = np.zeros((1, 1, 8), dtype=np.uint8)
    atlas = WindAtlas(
        frequency,
        frequency.copy(),
        np.zeros((1, 1), dtype=np.uint8),
        np.zeros((1, 1), dtype=np.uint8),
        np.ones((1, 1), dtype=bool),
    )
    with pytest.raises(FormatError, match="trailing"):
        decode_wind(encode_wind(atlas) + b"x")


def test_wind_extras_preserve_calm_and_gale_together() -> None:
    shape = (2, 3)
    atlas = WindAtlas(
        np.zeros((*shape, 8), dtype=np.uint8),
        np.zeros((*shape, 8), dtype=np.uint8),
        np.array([[4, 8, 0], [1, 2, 3]], dtype=np.uint8),
        np.array([[2, 9, 0], [8, 7, 6]], dtype=np.uint8),
        np.array([[True, True, False], [True, True, True]]),
    )
    calm, gale, valid = decode_wind_extras(encode_wind_extras(atlas))
    np.testing.assert_array_equal(calm[valid], atlas.calm[valid])
    np.testing.assert_array_equal(gale[valid], atlas.gale[valid])
    np.testing.assert_array_equal(valid, atlas.valid)
