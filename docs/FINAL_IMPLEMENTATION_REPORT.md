# Climatology modernisation implementation report

This report records the measured implementation, release, and deployment
evidence.  The 1.6.38.0 modern-dataset baseline and its earlier deployments
are retained below as provenance; the current Phase 0-10 candidate is 1.6.39.0.

## Scope and baseline

The implementation starts from Rick Gleason's maintained API-1.18 build at
`0af2303f932f8aead5f029b3ad083e08d4ce51e4`, while retaining the process-local
three-function Climatology interface consumed by Weather Routing.  Sean
D'Epagnier's `master` at `1e9f04072a11f227e58fde8c45efc8e1348f6597` was
audited independently.  Rick's tree is the appropriate OpenCPN build baseline;
neither tree had modernised the scientific data-generation workflow.

The modern-dataset baseline plugin was **1.6.38.0**.  The Phase 0-10
reconstruction release candidate is **1.6.39.0**.  The dataset has a separate
identity, **ocpn-climatology-2026.1**, and reports that identity in the About
panel and the additive Climatology JSON response.  This is research/planning
climatology, not a forecast or official navigational data.

## Phase 0-10 reconstruction release candidate

Version 1.6.39.0 starts from the complete, working 1.6.38.0 feature set and
implements the reconstruction architecture behind compatibility gates.  It
does not replace the UI or remove any field, overlay, cyclone control, timeline
behaviour, or Weather Routing integration.

The runtime now loads and validates the complete versioned dataset on a joined,
cancellable worker, then atomically publishes an immutable snapshot.  A
stateless query engine is shared by the UI, rendering, and both legacy and
typed provider APIs.  Dataset failure is explicit and fail-closed; no partial
modern/legacy mixture can be published.

Render settings are captured on the GUI thread as typed value state.  Isobar
calculation, texture raster preparation, and cyclone filtering/spatial indexing
run without wxWidgets UI access on worker threads.  Only final drawing-resource
publication and OpenGL upload remain on the render thread.  Workers are
cancellable, joined during shutdown, and never display dialogs.  The cyclone
index is immutable after publication and is swapped only after current readers
have left their bounded permits.

The three historical function-pointer signatures and the accepted 0.10-1.6
provider interval remain exact.  The additive typed provider does not alter
the broker payload consumed by older clients.  Dataset readiness is republished
after asynchronous load and again after cyclone preparation, covering both the
standard WeatherRouting plugin (which reparses `CLIMATOLOGY` messages) and
xWeatherRouting (which additionally resets its preparation guard).

The final GUI acceptance remains deliberately pending until the headless
release gates and isolated Test-OpenCPN installation described later in this
report are complete.  No 1.6.39.0 artefact is installed into the normal
OpenCPN profile or system plugin directories.

## Compatibility-first design

The released `windMM.gz`, `currentMM.gz`, scalar, cyclone and auxiliary files
retain the historical byte layouts.  Existing Weather Routing consumers
therefore receive updated fields through the same function signatures and do
not require a source change.  `wind-extrasMM.gz` is an additive sidecar which
preserves both calm and gale probabilities; historical plugins simply ignore
it.  A manifest and human-readable provenance file are also additive.

The exact declarations copied from xWeatherRouting are frozen with compile-time
type assertions.  Provider signatures now match its `const wxDateTime&`
contract exactly.  No incompatible API or new routing dependency was
introduced.

## Phase 10 headless release gates

The final 1.6.39.0 source and release artefacts passed the following gates on
31 August 2026 before installation or GUI testing:

* a clean GCC Release build and all five plugin tests (math, historical ABI,
  typed provider API, immutable dataset/query/render preparation, and the real
  loader/service) passed;
* a clean Clang RelWithDebInfo build passed the same five tests;
* the Debug AddressSanitizer/UndefinedBehaviorSanitizer build passed all five
  tests, with LeakSanitizer alone disabled because this ptrace sandbox cannot
  host it;
* the Debug ThreadSanitizer build passed the dataset and real loader/service
  concurrency tests, including repeated load cancellation and joining;
* Clang static analysis reported no project-owned finding.  Its two remaining
  reports are both in the installed wxWidgets `wx/buffer.h` system header;
* xWeatherRouting at
  `fb10a975d7dff105aa5c11cf481f7f7c36a4e7f1` built and passed all 175
  headless tests, including its Climatology thread-guard tests;
* standard WeatherRouting at
  `22dd1f099ff067f6c36a953664a54180fdf9d955` built and passed all 78
  headless tests.

The two consumer builds used detached `/tmp` source snapshots, leaving both
consumer repositories untouched.  The current standard WeatherRouting
snapshot required a forced `<cstdint>` include for GCC 16 and two missing
OpenCPN test mocks (`GetWaypointGUIDArray` and `fromDMM_Plugin`) in that
detached test harness.  These are pre-existing consumer build/test-fixture
defects and do not change or adapt the Climatology API.

The release package is
`climatology_pi-1.6.39.0-arch-x86_64-rolling.tar.gz` (11,322,011 bytes,
SHA-256
`bf698f92e29245ed9a70377d3e5360d7473293447ad7236f64eebc9f18771d91`).
The packaged library is 1,262,712 bytes with SHA-256
`7c08de3a2341adbab03883a65bc2a219cf5261226f4dcee0beaaf800b26f2c15`.
It has no embedded RPATH/RUNPATH.  After extracting the package, all 52
manifest-declared data files independently matched both their recorded sizes
and SHA-256 checksums, and the loader/service test passed against those exact
packaged data bytes.

## Historical dataset findings

The released bundle has no common header, schema/version identifier, build
date, checksums or machine-readable provenance.  Its source comments identify
COADS 1950–1979 pressure/cloud, COADS 1960–1979 SST, an NCEP air-temperature
record of unrecorded years, CMAP precipitation of unrecorded years, ICOADS
relative humidity of unrecorded years, OSCAR currents labelled 1993–2012,
WOA98 bathymetric context, and a NASA LIS/OTD 2013 lightning edition.  The
cyclone source is described only as Unisys best tracks and is internally
inconsistent about its start year; its source version, checksums and wind
averaging convention do not survive.  The wind-atlas generator is not a
reproducible specification of the checked-in `0xfefe` payload, so the strict
runtime decoder and exact released files are treated as authoritative.  The
full byte-level findings and surviving provenance are recorded in
`LEGACY_DATA_FORMAT.md`.

## Runtime defects corrected

Focused fixes include:

* bounded dateline and polar interpolation;
* missing-aware scalar and vector interpolation;
* interpolation of current U/V components rather than wrapped directions;
* correct circular direction interpolation through 0/360 degrees;
* exact time-of-day and leap-year-aware month interpolation;
* little-endian, dimension-bounded readers with strict decompressor/EOF checks;
* heap-backed scalar fields and overflow-checked allocations;
* valid-component current averaging rather than NaN propagation;
* strict ENSO parsing and consistent cyclone date wrapping;
* cyclone corruption cleanup without leaking already-created states;
* source-safe, atomic acquisition checkpoints;
* bounded and retried Earthdata catalogue/granule reads, preventing one
  stalled object from holding an otherwise complete monthly batch forever;
* process-lifetime checkpoint ownership, preventing concurrent source writers
  from racing the same otherwise atomic progress file;
* versioned-dataset fail-closed recovery, preventing a damaged modern bundle
  from being silently mixed with downloaded historical files;
* defined function-pointer formatting at the variadic boundary while retaining
  the legacy hexadecimal broker representation.

Authenticated integration with an official OSCAR file additionally exposed
its CF Julian calendar and the distinction between `lat`/`lon` coordinate
names and `latitude`/`longitude` dimension names.  The reader now handles both
explicitly and the regression fixture reproduces the official metadata.

## Modern data products

The target rolling period is 1995-01-01 through 2024-12-31.  Each product
records its actual authoritative coverage when that target is unavailable:

| Product | Source and actual period | Processing |
|---|---|---|
| Wind atlas | ERA5, 1995–2024 | Six-hour instantaneous 10 m U/V classified into eight wind-from sectors; speeds and calm/gale frequencies are sample statistics, never monthly mean vectors. |
| Surface current | OSCAR Final 0.25-degree V2.0, 1995-01-01–2022-08-05 | Every daily total-current U/V average is accumulated before spatial resampling or direction conversion. |
| Pressure, air temperature, RH, precipitation | ERA5 monthly fields, 1995–2024 | Unit-validated monthly climatologies resampled into the exact historical scalar grids. |
| Total cloud | ERA5 NCAR monthly aggregation, 1995–2022 | Monthly mean total-cloud percentage. |
| Sea-surface temperature | NOAA OISST v2.1, 1995–2024 | Monthly SST climatology with global physical-range validation. |
| Tropical cyclones | NOAA IBTrACS v04r01, 1995–2024 | Main-track six-hour positions; homogeneous USA wind/pressure fields; missing intensity remains missing. |
| Bathymetric context | GEBCO 2026 | Median wet elevation per useful legacy cell, then historical 40-bin quantisation; expressly not for navigation. |
| ENSO | NOAA CPC ERSSTv6 ONI | Complete seasonal classifications only. |
| Lightning | NASA LIS/OTD HRMC v2.3.2013, 1995–2013 | Historical observed product retained byte-for-byte; no precipitation/convection proxy substituted. |

Acquisition, validation, statistical aggregation, resampling, legacy encoding,
read-back validation and packaging are separate stages.  ERA5 and OSCAR use
bounded sufficient-statistic checkpoints; complete historical archives are
never retained.  The declared 100 GB ceiling is enforced before source access.

## Provenance and reproducibility

`dataset-manifest.json` records source/product identifiers, actual periods,
units, cadence, grids, resampling, output checksums, generator revision,
software versions and the complete deterministic validation result.
`DATASET_PROVENANCE.md` renders the same release identity for humans.
Cyclone conversion has a separate input/output/count report.  The formal
legacy format, old-source limitations, source audit and decision evidence are
in the companion documents in this directory.

## Automated verification

The suite covers every historical file family, strict malformed/truncated
input rejection, independent encoder round trips, quantisation tolerances,
dateline and polar interpolation, missing coastal cells, vector direction
wrapping, exact date/time interpolation, calm/gale distributions, cyclone
encoding, provenance consistency, storage-budget enforcement, exact parallel
checkpoint merging and the Weather Routing ABI.

The 110 Python source-pipeline/data tests pass under the recorded environment,
as do both C++ tests in normal and Release builds.  Both tests also pass with
AddressSanitizer and UndefinedBehaviorSanitizer.  LeakSanitizer alone is
disabled for that run because it cannot operate in this ptrace-based execution
sandbox, rather than because of a suppressed result.  A Clang `scan-build`
clean rebuild reports no analyser findings.  Dependency warnings emitted by
current xarray/netCDF4 and earthaccess releases are retained as upstream
deprecation diagnostics; they are not hidden to produce a nominally clean run.

The final byte-bound numerical comparison is installed as
`dataset-validation.json` and mirrored in `docs/NUMERICAL_COMPARISON.json`.
Regional checks cover representative wind regimes and the Gulf Stream, North
Atlantic Drift, Canary Current, North Equatorial Current, Equatorial
Countercurrent, Kuroshio, Agulhas and Antarctic Circumpolar Current.  They are
labelled advisory rather than misrepresented as statistical confidence tests.

All 52 manifest outputs independently match their recorded SHA-256 checksums.
The final comparison passes 28/28 regional physical sanity checks and 4/4 hard
source-binding checks.  January and July wind masks, sector frequencies,
sector speeds, calm probability and gale probability reproduce the
source-derived ERA5 checkpoint exactly after wire quantisation.  January and
July OSCAR valid masks are exact and each current component is within the
legacy 0.025 kn half-step tolerance (largest observed error 0.02499999 kn).

For context, the paired global old/new scalar differences are:

| Field | Bias (new - old) | MAE | RMSE |
|---|---:|---:|---:|
| Sea-level pressure | +0.088 hPa | 1.863 hPa | 3.655 hPa |
| Sea-surface temperature | +0.314 C | 0.600 C | 0.870 C |
| Air temperature | -0.628 C | 0.886 C | 1.825 C |
| Cloud cover | +2.150 percentage points | 9.815 | 15.419 |
| Precipitation | +0.299 mm/day | 0.616 mm/day | 1.154 mm/day |
| Relative humidity | -2.098 percentage points | 3.592 | 5.579 |

These differences are expected to include both the much newer period and
source/method changes; they are comparison diagnostics, not an assertion that
the historical field was ground truth.  The full cell counts, paired masks and
regional values remain in the machine-readable report.

The packaged data occupy about 11 MB (60 files); the Release shared library is
977,848 bytes.  The largest observed temporary working set during acquisition
was about 7.4 GB, far below the 100 GB project ceiling.  The completed source
workspace is about 3.7 GB and is disposable after release verification.

## Earlier 1.6.38.0 Test-OpenCPN deployment evidence

The release was built and deployed only through the isolated launcher at
`/home/paul/Test-OpenCPN/bin/launch-test-opencpn`.  Its installed library is:

`/home/paul/Test-OpenCPN/config/plugins/lib/libclimatology_pi.so`

and its data/resource root is:

`/home/paul/Test-OpenCPN/config/plugins/climatology_pi`

The library SHA-256 is
`3bec33d2aad6d516f2e12779a953f4d47deb589846ed7f5ac0d919f45c2e93a9`.
All 52 installed manifest outputs were rehashed successfully after copying.
The rollback backup is
`/home/paul/Test-OpenCPN/backups/climatology-20260818T063319Z`.

OpenCPN 5.14's real plugin loader found four candidates, declared Climatology
compatible, loaded it, selected the expected isolated data directory, loaded
its toolbar/panel resources and translation catalogue, and completed startup.
No Climatology error or warning remained in the second launch.  The only
warning was OpenCPN's unrelated failure to enumerate a network interface.

During the isolated implementation phase, the normal OpenCPN 5.15 installation
and profile were protected by pre/post sentinels and were not installation
targets.  Their library, configuration and data-tree sentinels remained
unchanged throughout that phase.

## Earlier 1.6.38.0 promotion to working OpenCPN

After the isolated validation was complete, the user explicitly requested the
same tested artefacts be promoted to working OpenCPN 5.15.0.  The production
library and complete plugin data/resource tree were transactionally replaced;
the existing configuration was not part of the installation and was restored
byte-for-byte after runtime validation.  The installed locations are:

* `/usr/lib/opencpn/libclimatology_pi.so` (also visible through Arch Linux's
  `/usr/lib64` symlink);
* `/usr/share/opencpn/plugins/climatology_pi`.

The rollback backup is
`/home/paul/OpenCPN-plugin-backups/climatology-working-20260818T065548Z`.
All 52 installed manifest outputs pass their checksums and the production
library has the same
`3bec33d2aad6d516f2e12779a953f4d47deb589846ed7f5ac0d919f45c2e93a9`
SHA-256 as the Test-OpenCPN artefact.

Runtime validation used the actual desktop launcher
`/home/paul/bin/opencpn-gribmerge`, which starts the in-tree OpenCPN 5.15.0
binary with the user's xWeatherRouting/xGRIB search paths.  OpenCPN declared
Climatology compatible, loaded the new library, selected the production data
directory and loaded its resources and translation catalogue without a
Climatology error or warning.

## Remaining limitations

* The legacy function-pointer broker is process-local and ABI-fragile by
  design; replacing it requires a coordinated OpenCPN/consumer API rather than
  a data-only change.
* OSCAR Final V2 ends on 2022-08-05.  Later Interim/NRT data are not mixed into
  a supposedly homogeneous Final climatology.
* Available global monthly cloud coverage ends in 2022.
* No newer documented global observational lightning climatology was selected;
  the known LIS/OTD period remains explicit.
* The historical formats quantise values and omit embedded metadata, which is
  why the external manifest is mandatory for the versioned bundle.
* Climatology describes historical frequency and mean conditions.  It cannot
  replace forecasts, current observations, prudent seamanship or official
  navigation products.
* Automated tests and the real loader establish data, ABI and startup
  behaviour.  A navigator should still make a human visual pass over each
  overlay/control and exercise a representative xWeatherRouting route before
  promotion from Test-OpenCPN to the working installation.
