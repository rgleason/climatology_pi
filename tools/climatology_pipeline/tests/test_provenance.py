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
    )
    assert manifest["outputs"][0]["sha256"] == sha256(data)
    assert manifest["climatology_period"]["kind"] == "multi-product-target"
    write_manifest(tmp_path, manifest)
    loaded = json.loads((tmp_path / "dataset-manifest.json").read_text())
    assert loaded["dataset_version"] == "ocpn-climatology-2026.1"
    assert "ERA5" in (tmp_path / "DATASET_PROVENANCE.md").read_text()


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
