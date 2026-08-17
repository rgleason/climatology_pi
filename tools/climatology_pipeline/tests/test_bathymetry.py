from __future__ import annotations

import numpy as np

from climatology_pipeline.bathymetry import aggregate_elevation, encode_depths


def test_wet_cell_median_ignores_land() -> None:
    elevation = np.array([[10., -10., -30., -50.],
                          [20., -20., -40., 5.]])
    result = aggregate_elevation(elevation, 2, 2)
    np.testing.assert_allclose(result, [[15., 40.]])


def test_land_missing_and_depth_bins() -> None:
    encoded = encode_depths(np.array([[np.nan, 0., 18., 260., 12000.]]))
    np.testing.assert_array_equal(encoded, [[-128, 0, 2, 10, 39]])
