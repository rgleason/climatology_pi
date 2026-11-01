#!/usr/bin/env bash
set -euo pipefail
climatology_python=python3
if [[ "${OS:-}" == Windows_NT ]]; then
    climatology_python=python
fi
"$climatology_python" "$(dirname "$0")/retain-packages.py" "${1:-build}" ci-artifacts
