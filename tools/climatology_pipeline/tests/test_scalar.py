from __future__ import annotations

from pathlib import Path

import numpy as np
import pytest

from climatology_pipeline.legacy import decode_scalar, encode_scalar
from climatology_pipeline.scalar import (
    MonthlySourceAccumulator,
    SCALAR_DEFINITIONS,
    decode_field,
    encode_field,
    field_for_definition,
    relative_humidity,
    resample_regular,
)


@pytest.mark.parametrize("name", SCALAR_DEFINITIONS)
def test_physical_encoding_round_trip(name: str) -> None:
    definition = SCALAR_DEFINITIONS[name]
    values = np.full(definition.shape, definition.offset + 10 * definition.scale)
    values[(0,) * len(values.shape)] = np.nan
    encoded = encode_field(name, values)
    wire = encode_scalar(encoded, name)
    decoded = decode_field(name, decode_scalar(wire, name))
    np.testing.assert_allclose(decoded, values, equal_nan=True, atol=definition.scale / 2)


def test_negative_air_temperature_is_preserved() -> None:
    definition = SCALAR_DEFINITIONS["airtemperature"]
    values = np.full(definition.shape, -12.4)
    decoded = decode_field("airtemperature", encode_field("airtemperature", values))
    np.testing.assert_allclose(decoded, -12.333333333333, atol=1e-10)


def test_relative_humidity_reference_cases() -> None:
    result = relative_humidity(np.array([293.15, 273.15]), np.array([293.15, 263.15]))
    assert result[0] == pytest.approx(100.0)
    # 2.599 hPa saturation over ice at -10 C divided by 6.112 hPa at 0 C.
    assert result[1] == pytest.approx(42.52, abs=.05)


def test_monthly_accumulator_and_checkpoint(tmp_path: Path) -> None:
    accumulator = MonthlySourceAccumulator(np.array([-1., 1.]), np.array([0., 180.]))
    values = np.array([[[1., np.nan], [3., 5.]], [[3., 7.], [5., 9.]]])
    accumulator.add(np.array([1, 1]), values)
    np.testing.assert_allclose(accumulator.monthly()[0], [[2., 7.], [4., 7.]])
    path = tmp_path / "monthly.npz"
    accumulator.save_checkpoint(path)
    restored = MonthlySourceAccumulator.load_checkpoint(path)
    np.testing.assert_array_equal(restored.count, accumulator.count)
    np.testing.assert_allclose(restored.total, accumulator.total)


def test_resampling_wraps_dateline_and_renormalises_missing() -> None:
    latitude = np.array([-1., 1.])
    longitude = np.array([0., 90., 180., 270.])
    values = np.array([[0., 90., 180., 270.], [0., 90., np.nan, 270.]])
    result = resample_regular(values, latitude, longitude,
                              np.array([0.]), np.array([315., 45., 180.]))
    np.testing.assert_allclose(result[0, :2], [135., 45.])
    assert result[0, 2] == pytest.approx(180.)


def test_definition_output_has_exact_legacy_shape() -> None:
    source_lat = np.array([-89.5, 0., 89.5])
    source_lon = np.array([0., 120., 240.])
    source = np.full((12, 3, 3), 1013.25)
    result = field_for_definition("sealevelpressure", source, source_lat, source_lon)
    assert result.shape == (12, 90, 180)
    decoded = decode_field("sealevelpressure", result)
    np.testing.assert_allclose(decoded, 1013.25)
