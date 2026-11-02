# Climatology 2.0.2 Plugin Manager packages

The plugin name is **Climatology** and the current version is **2.0.2**.
The toolkit records this as **2.0.2.0** in package XMLs. Every current package
contains the independent scientific dataset **ocpn-climatology-2026.2**,
verified against its manifest and 52 output checksums.

The `Plugin Manager packages` GitHub Actions workflow builds native Linux,
Windows x86/x64, macOS universal, Flatpak and Android arm64/armhf artifacts.
It does not publish them. Dispatch `groups=all`, or use comma-separated group
names to rebuild selected targets. Package generation verifies the architecture
and all 52 dataset output hashes before creating an archive. Windows checkouts
preserve the dataset's exact bytes through `.gitattributes`.

Each tarball has a single enclosing directory containing `metadata.xml`, the
native plugin library, complete data, icons, translations, GPL text and
`data/package-provenance.json`. The companion XML includes the SHA256 of the
finished archive; embedded metadata omits that self-referential checksum.
Metadata uses OpenCPN's official schema, copied under `ci/public-packages`.
GitHub workflow packages use Release asset URLs under the tag
`v2.0.2.0-modern-data` in `pob220/climatology_pi`. CircleCI primary x64
packages instead embed their final Cloudsmith download URL before the host
qualification runs; deployment preserves the exact tested archive bytes.

Windows x86 keeps the existing wxWidgets 3.1 and 3.2 build lanes. Windows
x64 builds twice: wxWidgets 3.3.4 for OpenCPN 5.15 (`e41f0de`), and wxWidgets
3.2.9 for the older OpenCPN 5.14.2 testing host (`de7e706`). Both installers and
SDKs are checksum-pinned. Native import libraries come from each host's actual
exports, and every OpenCPN and wxWidgets C++ import is checked. The finished
DLL's AMD64 PE import table must match its declared wxWidgets version before
packaging and again during release collection.

The normal x64 release package ends in `-wx334.tar.gz`. The `-wx329.tar.gz`
package stays under `compatibility/windows-x64-wx329`, outside the catalogue
collection. Both hosts advertise `msvc-64`; these binaries are incompatible
and must not be offered as interchangeable entries for that shared target.
The workflow retains the legacy package separately for older-host users.

Both Windows x64 variants are Climatology 2.0.2 with the identical 2026.2
scientific dataset. The `wx334` and `wx329` suffixes identify the host ABI.

CircleCI also runs `build-msvc-64-wx334-2022` and
`build-msvc-64-wx329-2022`, using this same checksum-pinned builder and stock-host
qualification. Each job retains its matching package, metadata, checksum,
SDK/import evidence and runtime results as artifacts. After qualification,
authorized non-PR builds publish the wxWidgets 3.3.4 tarball and its companion
XML to Cloudsmith, then verify both public downloads and their SHA-256 hashes.
Master builds use `opencpn/climatology-beta`; tagged master builds use prod,
and development branches use alpha, matching the existing toolkit policy.
The existing `CLOUDSMITH_*_REPO` overrides are respected. PR builds retain
artifacts without publishing. Legacy wxWidgets 3.2.9 remains separate.

Version 2.0.2 fixes the missing x64 publication step in 2.0.1. Metadata and
archive filenames have one plugin/version prefix. Catalogue imports should
retain those basenames, rather than add another `climatology_pi-2.0.2-` prefix.
The compiler and complete-package scripts now read the same CMake version.

Every CircleCI artifact archive requires a companion XML. Retention finalizes a
copy with the archive's actual CI download URL and SHA-256, and gives that copy
the same basename as its tarball. The original build XML remains available to
the production upload step. Missing or ambiguous XML/archive pairs fail the job.

Stock OpenCPN imports each complete Windows package in a disposable CI profile,
checks every installed plugin/data byte, opens Options > Plugins to verify the
displayed 2.0.2 version, then reinstalls/restarts and requires two normal exits
with code 0. OpenCPN omits the final zero version component in its GUI.

Android packaging checks both native architecture and the library's unique
`libclimatology_pi.so` SONAME, its `libgorp.so` dependency and 16 KiB ELF segment
alignment. Touch-specific layout is applied after Qt styling: readable labels,
large controls, read-only sampled values and scrollable configuration pages.
Only the title recognises panel-drag gestures, leaving slider gestures native.
The previously disabled Android colour-map path is implemented with a GLES2
shader: monthly interpolation, transparency, Mercator rotation and longitude
wrapping work without requiring repeating NPOT textures. The shader restores
host texture, program, buffer and vertex attribute state after drawing.

Download the matching tarball and use OpenCPN Options > Plugins > Import
plugin. Publishing public assets does not add this release to OpenCPN's main
catalogue. That requires a separately reviewed metadata submission.

`collect.py` accepts explicit CI artifact directories and validates all archives
before assembling the public assets, SHA256SUMS, a package index and a combined
catalogue XML. It rejects missing translations, path traversal, native architecture
mismatches, changed binaries, invalid metadata or changed scientific outputs.
Flatpak locale catalogs are collected from the installed extension tree. Retained
Flatpak binaries repackaged with the unchanged PO catalogs record both their
original binary source revision and the packaging revision.

Version 2.0.1 changed Windows build/packaging compatibility and release checks.
The scientific dataset remains `ocpn-climatology-2026.2`, byte for byte. The
all-platform workflow builds 23 ordinary release targets plus the separate
legacy Windows x64 target; a final job validates all 23 complete packages and
all 1,196 scientific output hashes before assembling release assets. The GitHub workflow does
not publish assets or update the OpenCPN catalogue; CircleCI publishes as above.
