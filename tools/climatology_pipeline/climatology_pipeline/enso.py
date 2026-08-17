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


class _Text(HTMLParser):
    def __init__(self) -> None:
        super().__init__()
        self.parts: list[str] = []

    def handle_data(self, data: str) -> None:
        self.parts.append(data)


def parse_oni_html(payload: str) -> dict[int, tuple[float, ...]]:
    parser = _Text()
    parser.feed(payload)
    text = " ".join(parser.parts)
    # A year followed by exactly twelve signed decimal values.  Repeated table
    # headings are naturally skipped.  Incomplete current years are omitted so
    # the legacy reader never receives a malformed row.
    pattern = re.compile(r"(?<!\d)(19\d{2}|20\d{2})\s+" +
                         r"\s+".join([r"(-?\d+(?:\.\d+)?)"] * 12) +
                         r"(?!\s+-?\d)")
    rows: dict[int, tuple[float, ...]] = {}
    for match in pattern.finditer(text):
        year = int(match.group(1))
        rows[year] = tuple(float(match.group(index)) for index in range(2, 14))
    if not rows or min(rows) > 1950 or max(rows) < 2024:
        raise ValueError("NOAA ONI page did not contain the expected complete history")
    return rows


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
    args.output.write_text(render_legacy(parse_oni_html(payload)), encoding="ascii")


if __name__ == "__main__":
    main(sys.argv[1:])
