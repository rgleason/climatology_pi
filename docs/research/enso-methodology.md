# ENSO methodology and statistical safeguards

Status: research specification, 2026-08-25.  This document defines historical
conditioning. It does **not** define or supply an ENSO forecast.

## Classification used in the experiment

The primary classification is CPC ERSSTv6 ONI: the three-month running mean
Niño-3.4 anomaly. A warm/cold episode requires ONI at or beyond +0.5/−0.5 °C
for at least five consecutive overlapping seasons. Each season is assigned to
its centre month (`DJF -> January`, ..., `NDJ -> December`). Short threshold
excursions remain neutral. A complete qualified episode—not an overlapping
season or calendar year—is the independent resampling unit. See the
[official CPC ONI v6 table and definition](https://www.cpc.ncep.noaa.gov/products/analysis_monitoring/enso/oni/v6/).

The exact reproducible table is
[`results/enso-month-classification.csv`](../../research/climatology_modernisation/results/enso-month-classification.csv).
The parser in `research/climatology_modernisation/enso.py` reads HTML table
cells structurally and is covered by tests.

## ONI versus relative ONI

CPC changed its operational monitoring to relative ONI (RONI) in February
2026. RONI removes the tropical-mean SST anomaly before applying the same
threshold/duration rule. This is scientifically relevant in a warming climate;
it is not a cosmetic rename. [CPC announcement](https://www.cpc.ncep.noaa.gov/products/analysis_monitoring/enso/roni/announcement.php).

For 1950–2025 centre months, the two official indices classified:

| ONI phase | RONI El Niño | RONI La Niña | RONI neutral |
|---|---:|---:|---:|
| El Niño | 216 | 0 | 23 |
| La Niña | 0 | 190 | 48 |
| Neutral | 36 | 47 | 352 |

Thus 154 of 912 months (16.9%) change category. Every scientific result was
therefore repeated with RONI. A signal which disappears under this defensible
alternative is not ready for a user-facing selector.

## Existing packaged classification defect

The current production data-generation parser flattens styled CPC HTML before
applying a numeric regular expression. Markup around the final `NDJ` cell can
be mistaken for the value. The packaged `data/elnino_years.txt` consequently
contains numerous December mismatches—for example official 1950 NDJ −0.8 is
packaged as −0.0, 1951 0.7 as 0.0 and 1957 1.6 as 1.0. It also stores monthly
threshold values without applying CPC's five-season episode rule.

This is a separate correctness defect and should be fixed with a versioned data
rebuild even if no regime selector is ever implemented. The audit is in
[`production-oni-mismatches.csv`](../../research/climatology_modernisation/results/production-oni-mismatches.csv).
Research results do not use the faulty table.

## Sample size and effective sample size

There are 76 calendar samples per month in the 1950–2025 wind screen, but far
fewer independent ENSO events:

| Centre month | El Niño years / independent events | La Niña years / independent events |
|---|---:|---:|
| January | 25 / 17 | 26 / 17 |
| April | 12 / 10 | 14 / 12 |
| May | 16 / 12 | 17 / 13 |
| July | 14 / 14 | 17 / 12 |
| November | 26 / 24 | 24 / 20 |
| December | 26 / 20 | 24 / 18 |

Counts for every month/phase are in
[`enso-sample-counts.csv`](../../research/climatology_modernisation/results/enso-sample-counts.csv).
The OISST/GODAS 1981–2025 experiment is smaller still: some months contain only
7–16 classified years. Splitting by weak/moderate/strong would usually leave a
handful of independent events and is rejected.

## Contemporaneous versus lagged conditioning

The experiment uses contemporaneous centre-month phase because it is explicit
and reproducible. Teleconnections can lag ENSO and the correct lag varies by
field, region and season. A production investigation must therefore:

1. pre-register region/variable/month and physically plausible lags (for
   example 0, 1 and 3 months), before inspecting route outcomes;
2. train lag choice inside each event-block training fold;
3. test it only on the omitted complete event;
4. correct across the tested lags/regions;
5. publish “no supported lag” when appropriate.

Selecting the best lag retrospectively on all events would be overfitting and
is explicitly prohibited.

## Statistical procedure

### Gridded signal

For each month and phase, the scripts compare the phase composite with neutral:

* vector anomaly magnitude for wind/current;
* scalar anomaly for pressure/SST;
* pooled variability and signal-to-noise ratio;
* unequal-variance component tests;
* within-cell Bonferroni correction for U/V followed by global
  Benjamini–Hochberg false-discovery-rate control at 0.05;
* area weighting by cosine(latitude), with ocean validity masks.

The maps are screening evidence, not proof of route value. Reanalysis grid
cells are spatially dependent, so area fractions are more honest than counting
cells as independent observations.

### Out-of-sample event validation

For each qualified event:

1. omit the complete event;
2. build correct-phase, all-years and opposite-phase composites without it;
3. compare the omitted event over a pre-specified route corridor;
4. record wind/current vector RMSE;
5. repeat for all events.

The mean wind-skill uncertainty is a deterministic 5,000-replicate bootstrap
over omitted events. A result is called robust here only when it has positive
mean skill, a 95% bootstrap interval above zero, and the sign survives both ONI
and RONI. This rule was declared after the broad screening and must be
pre-registered before any next-stage confirmation dataset.

### Sensitivity to individual events

Leave-one-event-out results expose influential events directly. Composite-only
route changes which fail this test are reported as overfit demonstrations, not
benefits.

## Nonstationarity

The production target should distinguish:

* **1991–2020 WMO standard normal**—stable comparison and published normal;
* **1996–2025 recent planning normal**—actual recent 30 years, clearly labelled;
* **full record**—research/sensitivity only;
* **regime anomaly**—estimated from the same baseline, never silently mixed
  across different climate periods.

WMO defines 1991–2020 as the current standard normal and supports frequently
updated normals for climate services ([WMO normals](https://wmo.int/wmo-climatological-normals)).
Changes in observing systems and reanalysis quality also make “more years” not
automatically better. No universal linear detrending is applied: marine wind
trends differ among reanalyses and can reflect both physics and observing
systems. Trend-adjusted regime anomalies remain experimental.

## Forecast use

Manual `El Niño` means “show a historical El Niño composite”, not “El Niño is
forecast”. A later seasonal-planning interface could accept authoritative
phase probabilities such as 60/30/10 and mixture-weight the distributions, but
must show issue date, lead time and forecast skill. CPC outlooks are
probabilistic, and boreal-spring ENSO predictability is notably difficult.
Binary auto-selection is rejected.

## Rejected classification choices

* no invented ENSO definition;
* no annual label applied to every month;
* no strength selector at current effective sample sizes;
* no global “ENSO affected” mask based on uncorrected p-values;
* no post-hoc location-specific lag chosen on the validation events;
* no ENSO × IOD or other cross-product categories—the independent sample size
  collapses and multiple-comparison burden explodes.
