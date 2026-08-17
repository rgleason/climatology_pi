# Climatology modernisation decision log

Status: implementation in progress  
Audit date: 2026-08-17  
Working branch: `modernisation/legacy-compatible-2026`

## Safety and repository baseline

The source baseline is Rick Gleason's `master` commit
`0af2303f932f8aead5f029b3ad083e08d4ce51e4` (2026-05-16), imported as local
commit `d41fdf4`. Sean D'Epagnier's comparison baseline is
`1e9f04072a11f227e58fde8c45efc8e1348f6597` (2023-12-24). Exact source archive
checksums are retained in `docs/IMPLEMENTATION_AUDIT.md`.

Only the portable Test-OpenCPN installation may receive this build:

* launcher: `/home/paul/Test-OpenCPN/bin/launch-test-opencpn`
* binary: `/home/paul/Test-OpenCPN/stock-app/bin/opencpn`
* config: `/home/paul/Test-OpenCPN/config`
* plugin library directory: `/home/paul/Test-OpenCPN/config/plugins/lib`

The production locations `/usr/lib/opencpn`, `/usr/share/opencpn`,
`~/.opencpn`, and the normal OpenCPN 5.15.0 environment are explicitly out of
scope. Installation requires a fresh path assertion and a timestamped Test-only
backup.

## Decisions

### D001 — Preserve the consumer interface

Keep the three `CLIMATOLOGY` message function pointers and their argument and
return semantics. Weather Routing and xWeatherRouting consume:

* scalar/vector data: `(setting, date, lat, lon) -> direction, speed`
* eight-sector wind atlas data: frequencies, sector speeds, gale and calm
* cyclone-track crossing count

Dataset metadata is additive in the JSON message. No routing source change is
required.

### D002 — Use an explicit rolling period, not an anonymous “normal”

The candidate target period is 1995-01-01 through 2024-12-31. It is a recent
rolling 30-year navigation climatology, not a WMO standard normal. WMO standard
normals use periods ending in a year ending in zero (currently 1991–2020).

Source coverage does not support one honest common end date: OSCAR Final V2.0
ends on 2022-08-05 and the public ERA5 WeatherBench archive used for bulk
atmospheric processing ends in early 2023. Each product therefore records its
actual inclusive period. Wind may use the current ERA5 ARCO store to extend
through 2024; MSLP, temperature, humidity and precipitation use 1995–2024;
the available NCAR monthly total-cloud aggregation uses 1995–2022; OSCAR uses
1995-01-01–2022-08-05; OISST and IBTrACS use 1995–2024. Static and
coverage-limited products (GEBCO and lightning) likewise declare their own
coverage. The manifest must never imply that a shorter source was silently
extended.

### D003 — Distributional wind, never monthly mean vectors

The legacy atlas represents eight 45-degree meteorological **wind-from**
sectors, each sector's frequency and conditional mean speed, plus calm
(<=3 kn) and gale (>=34 kn) probabilities. ERA5 hourly 10 m U/V samples are
therefore classified sample by sample. Monthly mean U/V is rejected because it
destroys direction, calm, gale, and multimodal distributions.

The default source cadence is six-hourly (00, 06, 12, 18 UTC), matching the
best documented historical generator and limiting acquisition to roughly one
quarter of the hourly volume. A scientifically stronger hourly mode is
available. Six-hourly sampling is stratified across the UTC day but can alias
local diurnal structure; validation must quantify this against hourly samples
in representative regions.

### D004 — Hard 100 GB storage ceiling

The pipeline has a configurable peak-workspace budget whose default and project
maximum are 100 GB. Acquisition is split by variable, month/year, and spatial
tile. A raw chunk is checksummed and validated, folded into compact sufficient
statistics, then deleted. Resume checkpoints contain aggregated counts/sums,
not source archives. Planning aborts before download if estimated peak usage
exceeds the budget.

A naive hourly global 0.25-degree U/V archive for 1995–2024 is about 2.18 TB
uncompressed, so retaining it is explicitly prohibited. Expected permanent
pipeline state and packaged output are well below 1 GB.

### D005 — Explicit little-endian legacy encoding

Released files were written using native C/C++ integer types and are
little-endian. New legacy-compatible encoders use explicit little-endian wire
types, strict dimension/range checks, deterministic gzip timestamps, and exact
payload-length validation. Runtime reading remains backward compatible with
the released little-endian files.

### D006 — Legacy wind loss is a demonstrated format limitation

One legacy marker byte stores either calm percentage (0–99) or gale percentage
(100–200); the other value becomes zero. Exactly 100% calm is also ambiguous.
This is scientifically material because both probabilities are routing inputs.
The builder continues to emit old-format files for compatibility, while an
additive enhanced wind schema will retain both. The plugin may prefer the
enhanced file but must fall back to the historical file and expose the same
function-pointer API.

### D007 — Source choices

* wind, MSLP, 2 m air temperature, total cloud: ERA5 hourly single levels;
* relative humidity: derived from ERA5 2 m temperature/dewpoint, with the
  formula and phase convention recorded;
* precipitation: ERA5 total precipitation accumulated to monthly mean daily
  rate, with accumulation boundaries tested;
* current: OSCAR Final 0.25-degree Version 2.0 daily surface current;
* tropical cyclones: IBTrACS v04r01, 1995–2024, agency-consistent fields;
* SST: NOAA OISST v2.1 daily rather than ERA5 skin temperature;
* bathymetry: GEBCO_2026 resampled to the useful one-degree legacy grid and
  explicitly marked “not for navigation”;
* lightning: retain the observed NASA LIS/OTD climatology until an
  authoritative truly global modern observational replacement exists. It is
  not replaced by precipitation or convective proxies;
* ENSO: NOAA CPC historical ONI table based on ERSSTv6, version/access date in
  provenance. The plugin retains ONI semantics even though CPC now also uses
  RONI operationally.

### D008 — Cyclone winds must not silently mix averaging conventions

IBTrACS `WMO_WIND` is not homogeneous: responsible agencies report 1-, 3-, or
10-minute maximum sustained winds and IBTrACS does not convert them. The first
encoder uses `USA_WIND` only for basins/times where the combined NHC/JTWC
one-minute series is available and otherwise records missing wind rather than
mixing conventions. A future optional WMO-agency mode must carry the agency and
averaging period; it must not masquerade as a homogeneous series.

### D009 — Dataset version differs from plugin version

Plugin versions retain the `1.6.x.y` line because current Weather Routing
accepts major/minor 0.10 through 1.6. Dataset versions use
`ocpn-climatology-YYYY.release` (first candidate:
`ocpn-climatology-2026.1`) in `dataset-manifest.json`. Binary schema versions
are separate (`legacy-wind/fefe`, `legacy-current/1`, etc.).

### D010 — Reject physically corrupt source reads

An OPeNDAP full-grid OISST read returned syntactically valid arrays filled with
zero, although point requests were correct. Structural checks alone would have
encoded this as a tiny, plausible-looking but useless climatology. Source
adapters now enforce variable-specific physical-range checks before encoding;
the production OISST path uses NOAA's official HTTP file endpoint and records
its checksum.

### D011 — Additive wind extension, not an incompatible replacement

Legacy `wind*.gz` remains the authoritative compatibility payload. Optional
`wind-extras*.gz` sidecars retain both calm and gale probabilities, which the old
one-byte marker cannot represent simultaneously. The modern runtime prefers a
valid sidecar but falls back to the legacy file. Old plugins simply ignore the
sidecars, while Weather Routing continues to receive the same function-pointer
interface.

### D012 — Fail closed on incomplete ENSO rows

The released ENSO file ends with a truncated 2014 row containing only three
monthly values. The historical reader indexed it as though twelve values were
present. The runtime now rejects incomplete rows, and the generator emits only
complete, source-attributed years.

### D013 — Use monthly ERA5 transports for monthly scalar products

Reading every six-hour ERA5 sample is necessary for the wind probability
distribution, but wasteful for monthly scalar means. MSLP, 2 m temperature,
dew point and mean precipitation rate use the public Planette monthly archive,
which is derived from NSF-NCAR ERA5 and retains the upstream parameter and DOI
metadata. Total cloud uses NCAR GDEX's public ERA5 monthly aggregation because
it is not present in Planette. Source variables, units, grids and time
continuity are validated, and selected values are compared with the official
ERA5/NCAR transport. Provenance names ERA5 as the scientific source and the
transport/archive separately.

### D014 — Calendar interpolation and cyclone windows are circular

The historical month interpolation asked wxWidgets for February's length
without supplying the observation year, and the cyclone crossing filter used
linear day and month ranges.  The latter lost December/January candidates and
could produce a negative signed separation.  Runtime helpers now use the
actual year (including leap years), fractional UTC days, and explicit circular
day/month arithmetic.  Unit tests cover leap-day and year-boundary cases.

### D015 — Saturate only legacy precipitation overflow

The precipitation byte reserves 255 for missing, so its largest physical
value is 50.8 mm/day.  ERA5's 1995–2024 monthly climatology contains a small
number of genuinely wetter 2.5-degree cells (the observed maximum is recorded
in the product validation report).  Rejecting the whole product or turning
those cells into missing data is less useful than representing them as
"at least 50.8 mm/day".  The encoder therefore explicitly saturates only
finite precipitation overflow at 254; all other scalar products still fail
closed or use their documented missing-data policy.  The manifest records the
clip count and maximum before encoding.

### D016 — Retain the identified 2013 LIS/OTD lightning edition

The old generator identifies its input as `LISOTD_HRMC_V2.3.2013.hdf`.  NASA's
current guide describes the v2.3 product family and the later 2015 edition, but
the original HDF and its checksum were never committed.  The modern bundle
therefore retains the byte-identical observational layer, declares the
conservative 1995-05-04 through 2013-12-31 coverage of that edition, and
records the encoded checksum.  It does not relabel the data as 1995–2024 or
replace lightning with an ERA5 proxy.

### D017 — Bind OSCAR resume checkpoints to their source and period

The current accumulator now embeds a versioned source identifier, requested
start/end dates, and completed-through date in the same atomically replaced
NPZ file as its sums and counts.  A JSON sidecar remains as a human-readable
progress aid but is not the recovery authority.  This prevents an interrupted
or accidentally reused checkpoint from silently duplicating a month, skipping
a month, or combining Final OSCAR with a different quality stream.  Download
results are also filtered to NetCDF granules so checksum/metadata sidecars
cannot be passed to xarray as science data.

## Rejected alternatives

* Monthly mean U/V as a wind atlas — invalid distributional substitution.
* A complete local ERA5 archive — exceeds the storage ceiling and is
  unnecessary for aggregation.
* Full native GEBCO in the plugin — the one-degree renderer/API cannot use its
  15-arc-second detail and the size would be wasteful.
* ERA5 convection/precipitation as “lightning” — not the observed quantity.
* Immediate API redesign — breaks routing consumers without improving the
  first dataset.
* Calling the anonymous historical bundle “dataset v1” — no such upstream
  version convention has been established.

## Open validation gates

1. Strictly decode every released file and freeze representative metadata.
2. Round-trip synthetic files independently within wire quantisation.
3. Validate spatial orientation, the dateline, poles, missing coasts, and
   mid-month temporal interpolation.
4. Build source-specific modern products and compare source grid samples.
5. Exercise the exact Weather Routing pointer semantics in a harness.
6. Build with warnings and sanitizers where supported.
7. install and validate only in Test-OpenCPN, then inspect its isolated log.
