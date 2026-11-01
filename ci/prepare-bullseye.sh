#!/usr/bin/env bash
# Run as root in the disposable Debian build environment.
set -euo pipefail
. /etc/os-release
if [[ "${VERSION_CODENAME:-}" != bullseye ]]; then
    exit 0
fi

# Match the archive and security snapshot used by the qualified 2.0.1 builds.
# Live Bullseye security indexes now refer to packages which have been removed.
if [[ -f /tmp/climatology-build-ca.crt ]]; then
    mkdir -p /etc/ssl/certs
    cp /tmp/climatology-build-ca.crt /etc/ssl/certs/ca-certificates.crt
fi
rm -f /etc/apt/sources.list.d/debian.sources
for source_list in /etc/apt/sources.list.d/*.list; do
    if [[ -f "$source_list" ]]; then
        sed -i '/bullseye/d' "$source_list"
    fi
done
printf '%s\n' \
    'deb https://archive.debian.org/debian bullseye main' \
    'deb [check-valid-until=no] https://snapshot.debian.org/archive/debian-security/20260831T235959Z bullseye-security main' \
    > /etc/apt/sources.list
printf '%s\n' 'Acquire::Retries "3";' > /etc/apt/apt.conf.d/99-climatology-build
