from pathlib import Path

import pandas as pd

from enso import classify_episodes, parse_cpc_oni_html
from fetch_ocean_samples import validate_oisst
from run_study import angular_difference, distance_nm, research_polar_speed


def test_structural_oni_parser_preserves_styled_negative_value() -> None:
    cells = "".join(f"<td>{value}</td>" for value in
                    ["1950", "-1.3", "-1.2", "-1.1", "-1.1", "-1.1", "-0.9",
                     "-0.6", "-0.6", "-0.6", "-0.6", "-0.7", "-0.8"])
    frame = parse_cpc_oni_html("<table><tr>" + cells + "</tr></table>" +
                               "<table><tr>" + cells.replace("1950", "2024", 1) + "</tr></table>")
    assert frame.loc[(frame.year == 1950) & (frame.month == 12), "oni_c"].item() == -0.8


def test_short_excursion_is_not_an_episode() -> None:
    frame = pd.DataFrame({"date": pd.date_range("2000-01-01", periods=8, freq="MS"),
                          "year": [2000] * 8, "month": range(1, 9),
                          "season": ["x"] * 8,
                          "oni_c": [0.6, 0.7, 0.8, 0.9, 0.4, -0.6, -0.7, -0.8]})
    result = classify_episodes(frame)
    assert set(result.phase) == {"neutral"}


def test_five_seasons_qualifies_whole_run() -> None:
    frame = pd.DataFrame({"date": pd.date_range("2000-01-01", periods=6, freq="MS"),
                          "year": [2000] * 6, "month": range(1, 7),
                          "season": ["x"] * 6, "oni_c": [0.5] * 5 + [0.4]})
    result = classify_episodes(frame)
    assert list(result.phase[:5]) == ["el_nino"] * 5
    assert result.phase.iloc[5] == "neutral"


def test_route_math_and_polar_are_bounded() -> None:
    assert angular_difference(350, 10) == 20
    assert 59 < distance_nm(0, 0, 0, 1) < 61
    assert 0 < research_polar_speed(20, 90) <= 8.5


def test_oisst_validation_rejects_syntactically_valid_zero_cache() -> None:
    import numpy as np
    with np.testing.assert_raises_regex(ValueError, "physical-range/coverage"):
        validate_oisst(np.zeros((12, 8, 8), dtype=np.float32))
