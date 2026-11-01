"""Authoritative CPC ONI parsing and reproducible episode classification."""

from __future__ import annotations

from dataclasses import dataclass
from datetime import date
from html.parser import HTMLParser
from pathlib import Path
import re

import numpy as np
import pandas as pd


SEASONS = ("DJF", "JFM", "FMA", "MAM", "AMJ", "MJJ",
           "JJA", "JAS", "ASO", "SON", "OND", "NDJ")


class _Cells(HTMLParser):
    def __init__(self) -> None:
        super().__init__()
        self.in_cell = False
        self.parts: list[str] = []
        self.cells: list[str] = []

    def handle_starttag(self, tag: str, attrs: list[tuple[str, str | None]]) -> None:
        if tag.lower() in {"td", "th"}:
            self.in_cell = True
            self.parts = []

    def handle_data(self, data: str) -> None:
        if self.in_cell:
            self.parts.append(data)

    def handle_endtag(self, tag: str) -> None:
        if tag.lower() in {"td", "th"} and self.in_cell:
            value = " ".join(" ".join(self.parts).split())
            self.cells.append(value)
            self.in_cell = False


def parse_cpc_oni_html(payload: str) -> pd.DataFrame:
    """Return one row per centre month from CPC's ERSSTv6 ONI HTML table."""
    parser = _Cells()
    parser.feed(payload)
    cells = parser.cells
    rows: list[dict[str, object]] = []
    for index, cell in enumerate(cells):
        if not re.fullmatch(r"(?:19|20)\d{2}", cell):
            continue
        values = cells[index + 1:index + 13]
        if len(values) != 12 or not all(re.fullmatch(r"-?\d+(?:\.\d+)?", v) for v in values):
            continue
        year = int(cell)
        for month, (season, value) in enumerate(zip(SEASONS, values), start=1):
            rows.append({"date": pd.Timestamp(year, month, 1),
                         "year": year, "month": month,
                         "season": season, "oni_c": float(value)})
    frame = pd.DataFrame(rows).drop_duplicates("date", keep="last").sort_values("date")
    if frame.empty or frame.year.min() != 1950 or frame.year.max() < 2024:
        raise ValueError("CPC ONI page does not contain the expected 1950-present table")
    return frame.reset_index(drop=True)


def classify_episodes(oni: pd.DataFrame, threshold_c: float = 0.5,
                      minimum_seasons: int = 5) -> pd.DataFrame:
    """Apply CPC's threshold and five-overlapping-season episode rule.

    A short threshold excursion is neutral.  Episode IDs are stable strings and
    form the resampling unit; treating overlapping seasons as independent would
    overstate effective sample size.
    """
    result = oni.copy()
    sign = np.where(result.oni_c >= threshold_c, 1,
                    np.where(result.oni_c <= -threshold_c, -1, 0))
    phase = np.full(len(result), "neutral", dtype=object)
    event_id = np.full(len(result), "neutral", dtype=object)
    start = 0
    while start < len(sign):
        stop = start + 1
        while stop < len(sign) and sign[stop] == sign[start]:
            stop += 1
        if sign[start] != 0 and stop - start >= minimum_seasons:
            label = "el_nino" if sign[start] > 0 else "la_nina"
            begin = result.iloc[start].date.strftime("%Y-%m")
            end = result.iloc[stop - 1].date.strftime("%Y-%m")
            phase[start:stop] = label
            event_id[start:stop] = f"{label}:{begin}:{end}"
        start = stop
    result["phase"] = phase
    result["event_id"] = event_id
    result["qualified"] = phase != "neutral"
    return result


def load_classification(path: Path) -> pd.DataFrame:
    return classify_episodes(parse_cpc_oni_html(path.read_text(encoding="windows-1252")))


def compare_legacy_table(classified: pd.DataFrame, legacy_path: Path) -> pd.DataFrame:
    legacy = pd.read_csv(legacy_path, sep=r"\s+")
    long = legacy.melt(id_vars="Year", var_name="season", value_name="legacy_oni_c")
    month_for_season = {name: index + 1 for index, name in enumerate(SEASONS)}
    long["month"] = long.season.map(month_for_season)
    long["date"] = pd.to_datetime(dict(year=long.Year, month=long.month, day=1))
    joined = classified.merge(long[["date", "legacy_oni_c"]], on="date", how="inner")
    joined["difference_c"] = joined.legacy_oni_c - joined.oni_c
    return joined.loc[joined.difference_c.abs() > 1e-9].copy()
