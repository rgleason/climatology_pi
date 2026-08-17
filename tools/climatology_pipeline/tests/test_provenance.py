from __future__ import annotations

import json
from pathlib import Path

from climatology_pipeline.provenance import make_manifest, sha256, write_manifest


def test_manifest_has_checksums_and_two_renderings(tmp_path) -> None:
    data = tmp_path / "wind01.gz"
    data.write_bytes(b"wind")
    manifest = make_manifest(
        repository=__file__.rsplit("/tools/", 1)[0],
        files=[data],
        products=[{"name": "wind", "summary": "test"}],
        sources=[{"name": "ERA5", "product_id": "reanalysis-era5-single-levels"}],
        generated_utc="2026-08-17T00:00:00+00:00",
        validation={"summary": {"advisory_checks": 4,
                                "advisory_checks_passed": 4}},
    )
    assert manifest["outputs"][0]["sha256"] == sha256(data)
    assert manifest["climatology_period"]["kind"] == "multi-product-target"
    write_manifest(tmp_path, manifest)
    loaded = json.loads((tmp_path / "dataset-manifest.json").read_text())
    assert loaded["dataset_version"] == "ocpn-climatology-2026.1"
    assert loaded["validation"]["summary"]["advisory_checks_passed"] == 4
    rendered = (tmp_path / "DATASET_PROVENANCE.md").read_text()
    assert "ERA5" in rendered
    assert "Advisory regional checks passed: 4/4" in rendered


def test_release_metadata_names_every_packaged_product() -> None:
    root = Path(__file__).resolve().parents[1]
    products = json.loads(
        (root / "metadata/products-2026.1.json").read_text(encoding="utf-8")
    )
    sources = json.loads(
        (root / "metadata/sources-2026.1.json").read_text(encoding="utf-8")
    )
    names = {product["name"] for product in products}
    assert names == {
        "wind atlas", "ocean surface current", "mean sea-level pressure",
        "air temperature", "relative humidity", "precipitation",
        "total cloud cover", "sea-surface temperature",
        "tropical cyclone tracks", "bathymetric context", "lightning",
        "ENSO classification",
    }
    assert all(product.get("period", {}).get("start") for product in products)
    assert all(product.get("period", {}).get("end") for product in products)
    assert all(source.get("product_id") for source in sources)

    # These descriptions are part of the machine-readable release record.
    # Keep them locked to the actual legacy wire definitions rather than an
    # earlier proposed representation which the runtime cannot read.
    encodings = {product["name"]: product.get("encoding") for product in products}
    assert encodings["mean sea-level pressure"] == (
        "little-endian int16, 0.01 hPa, offset 1000 hPa, 32767 missing"
    )
    assert encodings["sea-surface temperature"] == (
        "int8, 0.2 degree C, offset 15 degree C, -128 missing"
    )
    assert encodings["air temperature"] == (
        "int8, 0.333333 degree C, offset 0 degree C, -128 missing"
    )
    assert encodings["relative humidity"] == "uint8, 0.5 percent, 255 missing"
    assert encodings["total cloud cover"] == "uint8, 0.5 percent, 255 missing"
    grids = {product["name"]: product.get("target_grid") for product in products}
    assert grids["mean sea-level pressure"] == (
        "historical 90 x 180 grid at 2 degree spacing"
    )
    assert grids["air temperature"] == "historical 90 x 180 grid at 2 degree spacing"
    assert grids["relative humidity"] == (
        "historical 180 x 360 grid at 1 degree spacing"
    )
    assert grids["total cloud cover"] == "historical 90 x 180 grid at 2 degree spacing"
    assert grids["precipitation"] == "historical 72 x 144 grid at 2.5 degree spacing"
