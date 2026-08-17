from __future__ import annotations

import json

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
    write_manifest(tmp_path, manifest)
    loaded = json.loads((tmp_path / "dataset-manifest.json").read_text())
    assert loaded["dataset_version"] == "ocpn-climatology-2026.1"
    assert "ERA5" in (tmp_path / "DATASET_PROVENANCE.md").read_text()
