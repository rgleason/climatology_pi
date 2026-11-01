"""Convert NOAA CPC ERSSTv6 ONI into the plugin's strict monthly table."""

from __future__ import annotations

import argparse
from html.parser import HTMLParser
from pathlib import Path
import re
import sys
from urllib.request import urlopen


ONI_V6_URL = "https://www.cpc.ncep.noaa.gov/products/analysis_monitoring/enso/oni/v6/"
HEADER = "Year DJF JFM FMA MAM AMJ MJJ JJA JAS ASO SON OND NDJ"


class _TableRows(HTMLParser):
    def __init__(self) -> None:
        super().__init__()
        self.in_cell = False
        self.cell_parts: list[str] = []
        self.row: list[str] = []
        self.rows: list[list[str]] = []

    def handle_starttag(self, tag: str,
                        attrs: list[tuple[str, str | None]]) -> None:
        del attrs
        if tag.lower() in {"td", "th"}:
            self.in_cell = True
            self.cell_parts = []

    def handle_data(self, data: str) -> None:
        if self.in_cell:
            self.cell_parts.append(data)

    def handle_endtag(self, tag: str) -> None:
        tag = tag.lower()
        if tag in {"td", "th"} and self.in_cell:
            self.row.append(" ".join(" ".join(self.cell_parts).split()))
            self.in_cell = False
        elif tag == "tr":
            if self.row:
                self.rows.append(self.row)
            self.row = []


def parse_oni_html(payload: str) -> dict[int, tuple[float, ...]]:
    parser = _TableRows()
    parser.feed(payload)
    rows: dict[int, tuple[float, ...]] = {}
    number = re.compile(r"-?\d+(?:\.\d+)?")
    for cells in parser.rows:
        # Some mirrors put a complete row in one TD; CPC normally uses 13.
        tokens = [token for cell in cells for token in cell.split()]
        if len(tokens) != 13 or not re.fullmatch(r"(?:19|20)\d{2}", tokens[0]):
            continue
        if not all(number.fullmatch(value) for value in tokens[1:]):
            continue
        rows[int(tokens[0])] = tuple(float(value) for value in tokens[1:])
    if not rows or min(rows) > 1950 or max(rows) < 2024:
        raise ValueError("NOAA ONI page did not contain the expected complete history")
    return rows


def apply_episode_rule(rows: dict[int, tuple[float, ...]],
                       threshold: float = 0.5,
                       minimum_seasons: int = 5) -> dict[int, tuple[float, ...]]:
    """Neutralise excursions which do not meet CPC's episode-duration rule.

    Qualified values retain the official anomaly so the unchanged runtime can
    continue applying its +/-0.5 threshold.  Non-qualified threshold values are
    written as zero.  Missing calendar years break a run.
    """
    ordered = [(year, month, rows[year][month])
               for year in sorted(rows) for month in range(12)]
    sign = [1 if value >= threshold else -1 if value <= -threshold else 0
            for _, _, value in ordered]
    qualified = [False] * len(ordered)
    start = 0
    while start < len(ordered):
        stop = start + 1
        while (stop < len(ordered) and sign[stop] == sign[start]
               and (ordered[stop][0] * 12 + ordered[stop][1]) ==
                   (ordered[stop - 1][0] * 12 + ordered[stop - 1][1] + 1)):
            stop += 1
        if sign[start] and stop - start >= minimum_seasons:
            qualified[start:stop] = [True] * (stop - start)
        start = stop
    result = {year: list(values) for year, values in rows.items()}
    for index, (keep, (year, month, _)) in enumerate(zip(qualified, ordered)):
        if sign[index] and not keep:
            result[year][month] = 0.0
    return {year: tuple(values) for year, values in result.items()}


def render_legacy(rows: dict[int, tuple[float, ...]]) -> str:
    lines = [HEADER]
    for year, values in sorted(rows.items()):
        if len(values) != 12:
            raise ValueError(f"{year} does not have twelve seasons")
        lines.append(str(year) + " " + " ".join(f"{value:.1f}" for value in values))
    return "\n".join(lines) + "\n"


def main(argv: list[str] | None = None) -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("output", type=Path)
    parser.add_argument("--source", default=ONI_V6_URL)
    args = parser.parse_args(argv)
    if args.source.startswith(("http://", "https://")):
        with urlopen(args.source, timeout=60) as response:
            payload = response.read().decode("utf-8")
    else:
        payload = Path(args.source).read_text(encoding="utf-8")
    rows = apply_episode_rule(parse_oni_html(payload))
    args.output.write_text(render_legacy(rows), encoding="ascii")


if __name__ == "__main__":
    main(sys.argv[1:])
