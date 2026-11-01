#!/usr/bin/env bash
set -euo pipefail
shopt -s nullglob

packages=(build/*.tar.gz)
if (( ${#packages[@]} == 0 )); then
    echo "No built plugin archive found in build/" >&2
    exit 1
fi
mkdir -p ci-artifacts
cp "${packages[@]}" ci-artifacts/
metadata=(build/*.xml)
if (( ${#metadata[@]} )); then
    cp "${metadata[@]}" ci-artifacts/
fi
(
    cd ci-artifacts
    if command -v sha256sum >/dev/null 2>&1; then
        sha256sum *.tar.gz > SHA256SUMS
    else
        shasum -a 256 *.tar.gz > SHA256SUMS
    fi
)
