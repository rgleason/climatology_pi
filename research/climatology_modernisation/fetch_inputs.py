"""Fetch the bounded public inputs used by the modernisation study."""

from __future__ import annotations

import argparse
from datetime import datetime, timezone
import hashlib
import json
from pathlib import Path

import requests


SOURCES = {
    "oni-v6.html": "https://www.cpc.ncep.noaa.gov/products/analysis_monitoring/enso/oni/v6/",
    "roni-v6.html": "https://www.cpc.ncep.noaa.gov/products/analysis_monitoring/enso/roni/",
    "ncep-uwnd-10m-monthly.nc": "https://downloads.psl.noaa.gov/Datasets/ncep.reanalysis.derived/surface_gauss/uwnd.10m.mon.mean.nc",
    "ncep-vwnd-10m-monthly.nc": "https://downloads.psl.noaa.gov/Datasets/ncep.reanalysis.derived/surface_gauss/vwnd.10m.mon.mean.nc",
    "ncep-slp-monthly.nc": "https://downloads.psl.noaa.gov/Datasets/ncep.reanalysis.derived/surface/slp.mon.mean.nc",
    "ncep-land-sea-mask.nc": "https://downloads.psl.noaa.gov/Datasets/ncep.reanalysis/surface_gauss/land.sfc.gauss.nc",
}


def digest(path: Path) -> str:
    value = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            value.update(block)
    return value.hexdigest()


def fetch(url: str, path: Path) -> None:
    if path.exists():
        return
    partial = path.with_suffix(path.suffix + ".part")
    with requests.get(url, stream=True, timeout=(30, 180)) as response:
        response.raise_for_status()
        with partial.open("wb") as stream:
            for block in response.iter_content(1024 * 1024):
                if block:
                    stream.write(block)
    partial.replace(path)


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--cache", type=Path, default=Path("cache"))
    args = parser.parse_args()
    args.cache.mkdir(parents=True, exist_ok=True)
    records = []
    for name, url in SOURCES.items():
        path = args.cache / name
        fetch(url, path)
        records.append({"name": name, "url": url, "bytes": path.stat().st_size,
                        "sha256": digest(path)})
    manifest = {"schema": 1,
                "retrieved_utc": datetime.now(timezone.utc).isoformat(),
                "sources": records}
    (args.cache / "source-manifest.json").write_text(
        json.dumps(manifest, indent=2, sort_keys=True) + "\n", encoding="utf-8")


if __name__ == "__main__":
    main()
