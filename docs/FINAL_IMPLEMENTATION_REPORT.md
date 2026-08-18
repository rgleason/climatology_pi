# Climatology modernisation implementation report

This report is completed with the measured release and deployment evidence at
the end of the implementation run; the design and audit findings below are
already fixed in the repository history.

## Scope and baseline

The implementation starts from Rick Gleason's maintained API-1.18 build at
`0af2303f932f8aead5f029b3ad083e08d4ce51e4`, while retaining the process-local
three-function Climatology interface consumed by Weather Routing.  Sean
D'Epagnier's `master` at `1e9f04072a11f227e58fde8c45efc8e1348f6597` was
audited independently.  Rick's tree is the appropriate OpenCPN build baseline;
neither tree had modernised the scientific data-generation workflow.

The produced plugin is **1.6.38.0**.  The dataset has a separate identity,
**ocpn-climatology-2026.1**, and reports that identity in the About panel and
the additive Climatology JSON response.  This is research/planning
climatology, not a forecast or official navigational data.

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

## Deployment boundary

The release is built and deployed only through the isolated launcher at
`/home/paul/Test-OpenCPN/bin/launch-test-opencpn`, whose binary, config,
plugin-library and plugin-data roots are all below `/home/paul/Test-OpenCPN`.
The normal OpenCPN 5.15 installation and profile are protected by pre/post
checksums and are not installation targets.

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
