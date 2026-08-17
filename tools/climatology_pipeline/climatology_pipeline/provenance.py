"""Machine-readable and human-readable dataset provenance manifests."""

from __future__ import annotations

import argparse
from datetime import datetime, timezone
import hashlib
import importlib.metadata
import json
from pathlib import Path
import subprocess
from typing import Any, Iterable


DATASET_VERSION = "ocpn-climatology-2026.1"


def sha256(path: str | Path, block_size: int = 1024 * 1024) -> str:
    digest = hashlib.sha256()
    with Path(path).open("rb") as stream:
        while block := stream.read(block_size):
            digest.update(block)
    return digest.hexdigest()


def generator_revision(repository: str | Path) -> tuple[str, bool]:
    root = Path(repository)
    commit = subprocess.run(
        ["git", "rev-parse", "HEAD"], cwd=root, check=True,
        text=True, capture_output=True,
    ).stdout.strip()
    dirty = bool(subprocess.run(
        ["git", "status", "--porcelain"], cwd=root, check=True,
        text=True, capture_output=True,
    ).stdout.strip())
    return commit, dirty


def dependency_versions(names: Iterable[str] = ("numpy", "xarray", "dask", "zarr", "netCDF4")) -> dict[str, str]:
    result = {}
    for name in names:
        try:
            result[name] = importlib.metadata.version(name)
        except importlib.metadata.PackageNotFoundError:
            result[name] = "not-installed"
    return result


def make_manifest(
    repository: str | Path,
    files: Iterable[str | Path],
    products: list[dict[str, Any]],
    sources: list[dict[str, Any]],
    *,
    generated_utc: str | None = None,
) -> dict[str, Any]:
    commit, dirty = generator_revision(repository)
    outputs = []
    for item in sorted((Path(path) for path in files), key=lambda path: path.name):
        outputs.append({
            "file": item.name,
            "bytes": item.stat().st_size,
            "sha256": sha256(item),
        })
    return {
        "dataset_version": DATASET_VERSION,
        "manifest_schema": 1,
        "generated_utc": generated_utc or datetime.now(timezone.utc).isoformat(),
        "generator": {"git_commit": commit, "dirty": dirty},
        "climatology_period": {
            "start": "1995-01-01",
            "end": "2024-12-31",
            "kind": "multi-product-target",
            "note": (
                "Target rolling period; authoritative per-product periods below "
                "take precedence where source coverage is shorter"
            ),
        },
        "storage_budget_bytes": 100_000_000_000,
        "products": products,
        "sources": sources,
        "outputs": outputs,
        "software": dependency_versions(),
    }


def write_manifest(directory: str | Path, manifest: dict[str, Any]) -> None:
    directory = Path(directory)
    directory.mkdir(parents=True, exist_ok=True)
    (directory / "dataset-manifest.json").write_text(
        json.dumps(manifest, indent=2, sort_keys=True) + "\n", encoding="utf-8"
    )
    lines = [
        f"# OpenCPN Climatology dataset {manifest['dataset_version']}",
        "",
        f"Generated: {manifest['generated_utc']}",
        f"Target period: {manifest['climatology_period']['start']} through {manifest['climatology_period']['end']} ({manifest['climatology_period']['kind']})",
        manifest["climatology_period"].get("note", ""),
        f"Generator commit: `{manifest['generator']['git_commit']}`" + (" (dirty)" if manifest["generator"]["dirty"] else ""),
        "",
        "## Products",
        "",
    ]
    for product in manifest["products"]:
        period = product.get("period", {})
        coverage = ""
        if period.get("start") and period.get("end"):
            coverage = f" ({period['start']} through {period['end']})"
        lines.append(f"* **{product['name']}**{coverage} — {product.get('summary', '')}")
    lines.extend(("", "## Sources", ""))
    for source in manifest["sources"]:
        identifier = source.get("doi") or source.get("product_id") or source.get("url", "")
        lines.append(f"* **{source['name']}** — {identifier}")
    lines.extend(("", "## Output checksums", ""))
    for output in manifest["outputs"]:
        lines.append(f"* `{output['file']}` — {output['bytes']} bytes — SHA-256 `{output['sha256']}`")
    lines.extend(("", "This climatology is for planning and routing context; it is not a substitute for current forecasts or official navigational data.", ""))
    (directory / "DATASET_PROVENANCE.md").write_text("\n".join(lines), encoding="utf-8")


def main(argv: list[str] | None = None) -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("repository", type=Path)
    parser.add_argument("directory", type=Path)
    parser.add_argument("files", nargs="*", type=Path)
    parser.add_argument("--products", type=Path, required=True,
                        help="JSON array describing generated products")
    parser.add_argument("--sources", type=Path, required=True,
                        help="JSON array describing source products")
    args = parser.parse_args(argv)
    products = json.loads(args.products.read_text(encoding="utf-8"))
    sources = json.loads(args.sources.read_text(encoding="utf-8"))
    if not isinstance(products, list) or not isinstance(sources, list):
        parser.error("products and sources must each contain a JSON array")
    manifest = make_manifest(args.repository, args.files, products, sources)
    write_manifest(args.directory, manifest)
