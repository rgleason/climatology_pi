# Climatology 2.0.1 Plugin Manager packages

This release is the pob220 modernisation, based on scientific/runtime revision
cd00282e6ea2784a6d78ccfe47fed713269ad87e, not an unmodified build of Rick
Gleason's plugin. Rick's source and the OpenCPN master catalogue were both
1.6.37.0 when checked on 21 October 2026. Our plugin is 2.0.1.0; its independent
scientific dataset identity remains ocpn-climatology-2026.2.

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
The archive and metadata URLs are GitHub Release assets under the tag
`v2.0.1.0-modern-data` in `pob220/climatology_pi`.

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

Stock OpenCPN imports each complete Windows package in a disposable CI profile,
checks every installed plugin/data byte, opens Options > Plugins to verify the
displayed 2.0.1 version, then reinstalls/restarts and requires two normal exits
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

Version 2.0.1 changes Windows build/packaging compatibility and release checks.
The scientific dataset remains `ocpn-climatology-2026.2`, byte for byte. The
all-platform workflow builds 23 ordinary release targets plus the separate
legacy Windows x64 target; a final job validates all 23 complete packages and
all 1,196 scientific output hashes before assembling release assets. CI does
not publish assets or update the OpenCPN catalogue.
