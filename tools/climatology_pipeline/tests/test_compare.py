from __future__ import annotations

import numpy as np

from climatology_pipeline.compare import current_sample, wind_sample
from climatology_pipeline.legacy import (CurrentField, WindAtlas, encode_current,
                                         encode_wind, encode_wind_extras,
                                         write_gzip)


def test_geographical_wind_and_current_samples(tmp_path) -> None:
    frequencies = np.zeros((360, 720, 8), dtype=np.uint8)
    speeds = np.zeros_like(frequencies)
    valid = np.ones((360, 720), dtype=bool)
    frequencies[:, :, 6] = 50
    speeds[:, :, 6] = 20
    calm = np.full((360, 720), 7, dtype=np.uint8)
    gale = np.full((360, 720), 3, dtype=np.uint8)
    atlas = WindAtlas(frequencies, speeds, calm, gale, valid)
    write_gzip(tmp_path / "wind01.gz", encode_wind(atlas))
    write_gzip(tmp_path / "wind-extras01.gz", encode_wind_extras(atlas))
    sampled_wind = wind_sample(tmp_path, 1, 50., -30.)
    assert sampled_wind["prevailing_from"] == "W"
    assert sampled_wind["frequency_sum_percent"] == 100.
    assert sampled_wind["calm_percent"] == 7.
    assert sampled_wind["gale_percent"] == 3.

    u = np.full((481, 1080), .5)
    v = np.full((481, 1080), .5)
    write_gzip(tmp_path / "current01.gz", encode_current(CurrentField(u, v)))
    sampled_current = current_sample(tmp_path, 1, 38., -68.)
    assert sampled_current["speed_kn"] == np.hypot(.5, .5)
    assert sampled_current["bearing_to_deg_true"] == 45.
