# OpenCPN Climatology modernisation and ENSO route-value study

Status: research/architecture report, 2026-08-25. No production behaviour or
user interface was changed by this study.

## 1. Executive summary

The maintained upstream plugin's classic database is scientifically stale and
poorly provenance-bound. This fork has already corrected most of that problem:
`ocpn-climatology-2026.1` is an 11 MB, reproducible, legacy-compatible pack
using ERA5 (1995–2024), OSCAR Final v2 (1995–2022-08-05), OISST (1995–2024)
and IBTrACS (1995–2024), with explicit checksums and validation. Upstreaming or
publishing that modern all-years baseline is a much larger and better-supported
improvement than adding a global ENSO selector.

ENSO conditioning is physically real, regionally strong and not globally
reliable for passage planning. In the 1950–2025 independent wind screen,
phase-minus-neutral vector anomalies had a marine median of 0.87–1.14 kn and a
90th percentile of 1.77–2.76 kn, but no more than 14% of marine area survived
multiple-testing correction. Of six pre-selected passages, only Brisbane–Fiji
in November during El Niño improved held-out wind prediction with a 95%
event-bootstrap interval above zero under both conventional ONI and the new
relative ONI: about 6.3–6.5% mean vector-RMSE improvement. Several visually
large composite route changes failed out of sample.

Therefore:

* **Do not ship a global All/Neutral/El Niño/La Niña selector now.**
* Continue to offer all-years climatology; modernise/upstream its baseline and
  enrich wind distributions and waves.
* Treat ENSO as an optional regional research pack, first for southwest-Pacific
  austral spring, pending ERA5/WAVERYS/GLORYS confirmation and real headless
  Weather Routing tests.
* Do not expose ENSO strength classes.
* Fix the current CPC table parser independently; it corrupts some December
  values and does not apply the official episode-duration rule.

Detailed sources are in [the source matrix](dataset-source-matrix.md), the
classification and statistics in [the ENSO methodology](enso-methodology.md),
and quantitative route evidence in [route results](route-sensitivity-results.md).

## 2. Current plugin and data architecture

### Repository/history finding

The original `seandepagnier/climatology_pi` line and Rick Gleason's maintained
fork share substantially the same scientific dataset/generators. Rick's fork
adds the current build frontend, packaging, API 1.18 and maintenance fixes; it
did not by itself modernise the raw climatology. The local `pob220` line adds a
large, tested modern-data pipeline and hardened runtime. Relevant audited
identities are recorded in `docs/IMPLEMENTATION_AUDIT.md`.

### Complete data path

```text
ERA5 / OSCAR / OISST / IBTrACS / retained legacy products
  -> bounded Python sufficient-statistic accumulators
  -> explicit regridding, masks, unit/range tests and quantisation
  -> legacy gzip binaries + WEX1 wind extras + manifest/provenance
  -> strict C++ readers and monthly/spatial interpolation
  -> Climatology overlays
  -> three process-local function pointers in a CLIMATOLOGY message
  -> Weather Routing wind/current/cyclone consumers
```

The modern pipeline never needs a full multi-terabyte raw archive. Checkpoints
hold sufficient statistics and are source/period bound; outputs are decoded and
numerically compared before packaging.

### Runtime representations

* Wind: twelve 0.5° gzip files; eight 45° meteorological wind-from sector
  frequencies and conditional sector speeds. `WEX1` sidecars preserve separate
  calm (<=3 kn) and gale (>=34 kn) probabilities. The legacy payload otherwise
  stores only one of calm/gale and suppresses sectors <=2.5%.
* Current: twelve 1/3° files from 80°N–80°S, signed quantised U/V, 0.05 kn step;
  the modern pack uses monthly OSCAR component means without the old weak-flow
  zeroing.
* Scalars: separate fixed-grid, fixed-quantisation fields for MSLP, SST, air
  temperature, cloud, precipitation, RH, lightning and bathymetric context.
* Cyclones: six theatre files of six-hour best-track segments, position,
  state, wind and pressure.
* Time: linear interpolation around month midpoints; space: bilinear/wrapped
  interpolation with field-specific missing/coastal behaviour.
* Historical mode: existing ONI values filter cyclone display/avoidance. They
  do not select different wind/current climatologies.

The formal byte layouts and old-source periods are in
`docs/LEGACY_DATA_FORMAT.md`.

### Weather Routing contract

Weather Routing receives three ABI-fragile process-local pointers:

```text
scalar/vector(date, setting, lat, lon) -> direction, speed
wind_atlas(date, lat, lon) -> 8 frequencies, 8 conditional speeds, calm, gale
cyclone_crossings(segment, date, range) -> crossing count
```

It supports disabled, currents-only, cumulative atlas,
cumulative-minus-calms, most-likely and average modes. Cumulative routing
calculates polar speed for each sector and probability-weights boat speed;
minus-calms scales by `1-calm`. GRIB is preferred when configured and
climatology can supply fallback/long-range data. Current is a monthly mean
vector. Cyclone history can reject crossing segments. It has no wave input,
regime dimension, sample count, uncertainty, percentile or provenance in the
call contract.

This representation is better than a simple vector mean—the common-but-opposite
wind regimes remain separate—but it lacks within-sector speed distributions
and tail joint probabilities. A global mutable “selected regime” would make the
existing pointer API unsafe for concurrent routes and must not be added.

## 3. Existing dataset assessment

### Classic upstream data

The classic pack combines COADS 1950–1979 SLP/cloud and 1960–1979 SST, an
uncertain NCEP marine-temperature period, uncertain CMAP/ICOADS periods,
old OSCAR and heterogeneous cyclone sources without a common schema, checksum,
period or provenance. Its current generator zeroes component magnitudes below
0.2 m/s; the wind wire format loses tail information. It is materially
outdated and non-reproducible by modern standards.

### This fork's 2026.1 data

The local modern pack is not stale in the same sense. It already uses:

| Field | Actual source/period |
|---|---|
| Wind atlas | ERA5 six-hourly 10 m U/V, 1995–2024 |
| MSLP, T2m, RH, precipitation | ERA5, 1995–2024 |
| Cloud | ERA5 aggregation, 1995–2022 |
| Current | OSCAR Final v2 daily, 1995-01-01–2022-08-05 |
| SST | OISST v2.1, 1995–2024 |
| Cyclones | IBTrACS v4r01 main tracks, 1995–2024 |

Its old-versus-modern global comparisons are material: MSLP RMSE 3.66 hPa,
SST RMSE 0.87 °C, air-temperature RMSE 1.83 °C, cloud RMSE 15.4 percentage
points, precipitation RMSE 1.15 mm/day and RH RMSE 5.58 points. These are not
all “climate change”—source, processing and old-format defects contribute—but
they show why baseline replacement matters operationally.

The next baseline issue is convention rather than staleness: 1995–2024 is a
recent rolling 30-year interval, whereas 1991–2020 is the WMO standard normal.
Both are defensible if labelled; neither should silently replace the other.

## 4. Candidate reanalysis sources

The recommended combination is ERA5 atmosphere, GLORYS12 primary currents
cross-checked with ORAS5/OSCAR, WAVERYS waves cross-checked with ERA5, OISST
SST and IBTrACS cyclones. NCEP R1/GODAS are screening data only. Exact periods,
resolutions, access and licences are in the [source matrix](dataset-source-matrix.md).

Higher resolution is not automatically more accurate climatology. For currents
and waves, agreement across differently constructed reanalyses is more valuable
than packaging 1/12° precision. A 1° offshore runtime field plus explicit
coastal/unknown masks is more honest and compact.

## 5. ENSO classification methodology

The primary experiment follows CPC ERSSTv6 ONI ±0.5 °C and five consecutive
overlapping seasons. Complete episodes are independent samples. The full study
is repeated with CPC RONI, the operational standard since February 2026.
ONI and RONI change 154/912 monthly labels (16.9%) over 1950–2025, demonstrating
material classification uncertainty in a warming climate. See
[ENSO methodology](enso-methodology.md).

No annual labels, locally invented thresholds, strength subdivisions or
post-hoc lag selection are used.

## 6. Statistical methodology

* monthly phase-minus-neutral vector/scalar anomalies;
* pooled signal-to-noise ratios;
* unequal-variance tests;
* component Bonferroni plus global Benjamini–Hochberg FDR 0.05;
* cosine-latitude ocean-area weighting;
* complete-event leave-one-out validation;
* 5,000-replicate event bootstrap intervals for route-corridor skill;
* pre-selected high/moderate/control routes;
* full ONI/RONI sensitivity repeat;
* recent 30-year versus standard-normal/full-record comparison.

The experiment uses monthly NCEP R1 wind because it is bounded and publicly
accessible. It tests composite vector signal, not the final ERA5 hourly speed/
direction distribution. The sailing-path calculation is a transparent proxy,
not a production Weather Routing qualification.

## 7. Sample-size findings

Even 76 years produce only 10–24 independent warm/cold events for the sampled
months; April has only 10 El Niño and 12 La Niña events. In the 1981–2025
ocean/SST period some categories contain 7–16 years. Event strength classes
would be statistically fragile. Gridded high resolution does not increase the
number of independent climate events.

The bootstrap intervals are correspondingly wide. Only one route/phase's wind
skill interval is positive under both ONI and RONI. Cyclone basin/month counts
often have just 4–11 omitted events and yield unstable apparent improvements.

## 8. Wind results

ENSO adds regionally useful wind information. Global marine median vector
anomalies are around 1 kn and the upper decile 1.8–2.8 kn, but corrected area is
small. Southwest-Pacific November El Niño is the one robust route-corridor
result: mean held-out RMSE improvement 6.48% (95% CI +0.45 to +12.13) with ONI
and 6.33% (+0.23 to +12.33) with RONI. California–Hawaii, Bermuda–Azores and
Panama–Marquesas El Niño conditioning were worse than all-years.

A future product should retain direction frequency, conditional speed
distribution, calm/gale/exceedance probabilities and percentiles. Vector mean
alone is explicitly rejected.

## 9. Current results

GODAS phase anomalies are small globally: median 0.020–0.027 kn, 90th
percentile 0.066–0.108 kn and at most 2.51% FDR-significant area. A
Brisbane–Fiji La Niña corridor signal is suggestive, but other routes are weak
or negative and only one coarse ocean reanalysis was tested. ENSO-conditioned
currents are not justified for routing yet. Modern all-years currents,
cross-reanalysis uncertainty and directional persistence are recommended.

## 10. Wave results

Wave climatology is a strong general enhancement because sail comfort/safety
depends on upper tails, direction and period. The bounded NOAA long-term wave
summary lacks event years and cannot validate ENSO. Published ERA5 work shows
regional Pacific ENSO wave-power/direction effects, not global route skill.
WAVERYS/ERA5 event-block and route tests remain mandatory before conditioning.

## 11. Cyclone results

Updated IBTrACS all-years density/intensity is recommended. ENSO-conditioned
cyclone risk is scientifically plausible and basin-specific, but the packaged
30-year screen is too count-limited and inconsistent for a global switch.
Any future layer must show basin/season, track count, event count, uncertainty,
intensity convention and historical—not forecast—status.

## 12. Geographic and seasonal ENSO influence

Evidence supports:

* **strongest candidate:** tropical/southwest Pacific, especially selected
  austral-spring wind fields;
* **possible regional benefit:** other tropical/subtropical Pacific seasons,
  but not assumed from anomaly magnitude;
* **weak/unreliable in this route screen:** Atlantic trade-wind and North
  Atlantic passages, northeast-Pacific July and southern-Indian December;
* **SST/pressure:** physically coherent regional signals, not direct passage
  benefits;
* **currents/waves/cyclones:** localized candidates requiring stronger tests.

The checked-in maps show magnitude; corrected area and event validation prevent
them being interpreted as global superiority.

## 13. Out-of-sample validation

Composite routing alone would have suggested a 78-hour Panama El-to-La spread
and up to a 105-hour Bermuda difference. Both failed event-block validation.
This is the central negative result: large composite differences can be less
predictive than all-years climatology. Full figures are in
[route-sensitivity-results.md](route-sensitivity-results.md).

## 14. Route-planning experiments

Brisbane–Fiji November composites changed proxy ETA by roughly 20–40 hours and
geometry by about 175–243 NM relative to all-years/neutral depending index, and
the El Niño corridor wind skill survived validation. Absolute route metrics are
not publishable operational results because the proxy is coarse and omits
hourly distributions, waves and vessel-specific settings. The demonstrated
benefit is a credible candidate effect, not a production performance claim.

The required next gate is a blinded set of real headless Weather Routing runs
using ERA5 hourly conditional distributions and identical polars/constraints.

## 15. ENSO sensitivity analysis

Do not expose LOW/MODERATE/HIGH until thresholds are calibrated on independent
routes. Retain continuous diagnostics: effective events, confidence interval,
correct-phase skill, route/ETA spread, calm/gale/wave-exceedance changes,
index/source agreement. “No demonstrated practical sensitivity” is a first-
class result.

## 16. Climate change and nonstationarity

NCEP screening differences relative to 1991–2020 were:

* 1996–2025: monthly marine median wind-vector difference 0.33–0.46 kn,
  90th percentile 0.68–0.88 kn;
* 1981–2010: median 0.48–0.58 kn, 90th 0.96–1.20 kn;
* 1950–2025: median 0.56–0.67 kn, 90th 1.20–1.75 kn.

These shifts are smaller than many ENSO regional composites but widespread and
persistent. They combine true variability/trend and reanalysis observing-
system changes. Baseline modernization is therefore more generally valuable,
but it must offer a named period rather than imply a timeless “normal”.

Recommendation: publish 1991–2020 as the standard normal and optionally a
1996–2025 recent planning normal; do not manufacture future years or blindly
detrend every variable.

## 17. Licensing and distribution

Derived quantised climatologies are technically feasible to redistribute, but
each release needs a product-specific licence gate. Copernicus products require
attribution and distinguish derivatives from redistribution; Earthdata access
does not itself prove redistribution rights; NOAA archives can include
third-party material even though NOAA-origin data are generally CC0/public
domain. The exact release checklist is in the [source matrix](dataset-source-matrix.md).

## 18. Storage and performance

The existing mixed-resolution pack is ~11 MB. For a deliberately rich set of
29 quantised 16-bit statistics across 12 months, estimated compressed sizes are:

| Grid | One regime | Four complete regimes |
|---|---:|---:|
| 2° | 3–6 MB | 11–26 MB |
| 1° | 11–26 MB | 43–103 MB |
| 0.5° | 43–103 MB | 172–413 MB |
| 0.25° | 172–413 MB | 688–1,652 MB |

These are engineering ranges, not measured final archives. Four independent
files duplicate baseline information. A 1° baseline plus sparse/quantised
phase anomalies, confidence masks and metadata should plausibly target
~30–80 MB as an optional enhanced/regime pack, but must be measured with the
real encoder. The classic 11 MB pack remains available. Adaptive coastal
resolution is not worthwhile until coastal reanalysis skill, rather than grid
spacing, is demonstrated. Android should memory-map/lazily decompress tiles.

## 19. Recommended enhanced data model

Use a versioned richer logical model with baseline-plus-anomaly physical
encoding:

```text
dataset / variable / month / grid / regime / statistic
  value + units + quantisation
  valid/masked/unknown
  sample_years + independent_events
  uncertainty + confidence/reliability mask
  source/product/version/period
  classification/index/lag
```

Wind statistics: eight-sector frequency; speed quantiles/histogram per sector;
calm, strong-wind and gale probabilities; vector/scalar means. Current:
component mean, speed percentiles and directional persistence. Waves: Hs
quantiles/exceedances, dominant direction and period. Pressure/SST: mean,
variability and anomaly.

Keep legacy readers and data untouched. Add an optional enhanced pack and a
typed provider API whose request includes dataset/scenario and whose result
includes statistics, validity, counts and uncertainty. The old three-pointer
API adapts only the all-years compatible view. Weather Routing opts into the
typed API; no process-global selection state.

## 20. UI recommendation

No ENSO selector in the production UI yet. An experimental build may show an
analysis panel, disabled outside validated regions/months:

```text
Historical climate scenario: All years (default)
Experimental: El Niño / La Niña
Effective events: N   Out-of-sample skill: X% [CI]
Influence: not calibrated / no demonstrated sensitivity
Historical composite — not a forecast
```

Neutral is scientifically useful for analysis but not automatically a better
“normal”; all-years remains the safe default. Strength controls are rejected.

## 21. Recommended implementation roadmap

1. **Baseline release/correctness:** fix the structural CPC parser, regenerate
   versioned data, publish the modern 1991–2020 and recent-normal all-years
   products, and propose the validated modern pack upstream.
2. **Enhanced statistics/API:** define a portable typed water/climate provider
   and enhanced format; add within-sector wind distributions and wave
   percentiles while preserving the classic 11 MB pack and legacy pointers.
3. **Blinded ENSO qualification:** build bounded ERA5, WAVERYS and
   GLORYS/ORAS/OSCAR event composites; pre-register regions/lags/routes; run
   actual headless Weather Routing with held-out complete events and
   vessel-specific polars. Only then decide on a regional optional selector.

## 22. Features rejected or deferred

### Decision matrix

| Improvement | Rating | Evidence |
|---|---|---|
| Modernized all-years wind climatology | **STRONGLY RECOMMEND** | Existing ERA5 build removes undocumented old data and preserves wind regimes; broad baseline differences are material. |
| Modernized current climatology | **RECOMMEND** | OSCAR build fixes severe old weak-current bias; next version should use/cross-check GLORYS/ORAS and expose distribution/uncertainty. |
| Wave climatology | **RECOMMEND** | Direct sailing relevance; current interface has none. Needs WAVERYS/ERA5 validation and a typed interface. |
| Updated cyclone climatology | **RECOMMEND** | IBTrACS improves provenance/global consistency; retain basin/agency caveats. |
| Global Neutral/El Niño/La Niña selector | **NOT ENOUGH EVIDENCE** | Only one of six route/phase cases has robust skill under both indices; large false positives occurred. |
| Regional ENSO research pack | **EXPERIMENTAL / OPTIONAL** | Southwest-Pacific November El Niño candidate survived event-block validation. |
| ENSO strength selector | **DO NOT IMPLEMENT** | Effective event counts collapse below defensible levels. |
| ENSO sensitivity indicator | **EXPERIMENTAL / OPTIONAL** | Useful concept, but thresholds require blinded calibration and real routing. |
| NAO conditioning | **NOT ENOUGH EVIDENCE** | Strong North Atlantic winter mode, but limited long-lead persistence; test probabilistic seasonal input, not a manual static selector. |
| IOD conditioning | **EXPERIMENTAL / OPTIONAL** | Potential Indian-Ocean seasonal value; test separately. Do not combine with ENSO until sample size supports it. |
| SAM conditioning | **NOT ENOUGH EVIDENCE** | Southern-Ocean relevance, but short persistence and difficult routing validation. |
| PDO conditioning | **DO NOT IMPLEMENT** | Few independent decadal regimes, ENSO overlap and warming confounding. |
| MJO conditioning | **DO NOT IMPLEMENT** | Intraseasonal weather/subseasonal mode, not weeks-to-months climatology category. |
| AMV conditioning | **DO NOT IMPLEMENT** | Few cycles and forced-trend confounding. |
| Probabilistic seasonal-regime input | **EXPERIMENTAL / OPTIONAL** | Scientifically correct future direction; preserve phase probabilities/lead skill after historical composites are validated. |

No raw reanalysis distribution, arbitrary UI mode combinations, ENSO × IOD
cross-products, binary forecast auto-selection, hidden route activation or
claims that composites are forecasts should be implemented.

## 23. Reproducibility and provenance appendix

Research code lives outside production in
`research/climatology_modernisation/`. It downloads bounded NCEP R1 inputs,
OISST via validated annual OPeNDAP blocks and GODAS surface-current subsets;
records source URLs/checksums; decodes the packaged IBTrACS product; and emits
CSV/JSON/PNG results. The bad large OISST DAP response encountered during the
study was rejected by physical-range/coverage validation and replaced with
small blocks.

Principal evidence:

* [`experiment-metadata.json`](../../research/climatology_modernisation/results/experiment-metadata.json)
* [`global-wind-signal.csv`](../../research/climatology_modernisation/results/global-wind-signal.csv)
* [`global-current-signal.csv`](../../research/climatology_modernisation/results/global-current-signal.csv)
* [`global-sst-signal.csv`](../../research/climatology_modernisation/results/global-sst-signal.csv)
* [`global-pressure-signal.csv`](../../research/climatology_modernisation/results/global-pressure-signal.csv)
* [`route-proxy-results.csv`](../../research/climatology_modernisation/results/route-proxy-results.csv)
* [`leave-one-event-out-detail.csv`](../../research/climatology_modernisation/results/leave-one-event-out-detail.csv)
* [RONI sensitivity outputs](../../research/climatology_modernisation/results-roni/)
* [`storage-estimates.csv`](../../research/climatology_modernisation/results/storage-estimates.csv)

### Explicit answers to the final scientific questions

1. **Outdated?** Classic upstream: yes. This fork's 2026.1 pack: substantially
   modern already, though period conventions and current/wave fields can improve.
2. **Would reanalysis improve planning?** Yes—especially reliable wind
   distributions, corrected currents and waves; the existing old/new differences
   are operationally nontrivial.
3. **Sources?** ERA5 wind/pressure; GLORYS12 + ORAS5/OSCAR current checks;
   WAVERYS + ERA5 waves; OISST SST; IBTrACS cyclones.
4. **ENSO selector?** Not globally at present. Regional experiment only.
5. **Where/when?** Best demonstrated candidate: southwest Pacific, November,
   El Niño wind. Other regions/seasons need confirmation.
6. **Wind improvement?** One candidate corridor improved held-out vector RMSE
   ~6.3–6.5% mean; most tested corridors did not.
7. **Current improvement?** Typical anomalies are tiny and corrected area
   sparse; not robust enough for routing yet.
8. **Wave improvement?** Physically/literature plausible regionally, but no
   route-level out-of-sample result in this bounded study.
9. **Cyclones?** Useful only as basin/season conditional historical risk with
   uncertainty; current sample is too small for a global selector.
10. **Operational route differences?** Composite differences can be tens of
    hours/hundreds of NM, but most failed held-out validation. Only one candidate
    combines route sensitivity with robust wind skill.
11. **Three-state model?** Sufficient for a first experiment, not proven for
    production; retain all-years separately.
12. **Strength?** No.
13. **Other mode first?** No. IOD deserves a regional experiment; none earns a
    production selector before ENSO validation.
14. **Baseline vs regime?** Baseline modernization is more valuable.
15. **Architecture?** Versioned rich logical regime/statistic dimension,
    encoded as baseline + sparse anomalies/confidence, with typed provider API.
16. **Size?** Classic ~11 MB; rich 1° four-full-regime estimate 43–103 MB;
    target optional baseline/anomaly pack ~30–80 MB pending measurement.
17. **User confidence/provenance?** Source/version/period, phase index/lag,
    effective event count, uncertainty, reliability mask, method and clear
    historical-climatology label.
18. **Do not implement?** Global selector now, strength modes, mode
    combinations, binary forecast selection, raw reanalysis, or unsupported
    confidence labels.
