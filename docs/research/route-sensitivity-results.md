# Route-sensitivity and out-of-sample results

Status: bounded screening experiment, 2026-08-25. The complete machine-readable
results and code are in `research/climatology_modernisation/`.

## What was tested

The wind experiment used NCEP/NCAR Reanalysis 1 monthly 10 m U/V for
1950–2025 at ~1.875°. This deliberately open, low-resolution product permits a
reproducible independent screen; it is not the proposed production dataset.
Six routes/months were selected before calculating composites:

| Route/month | Purpose |
|---|---|
| Panama–Marquesas, April | tropical Pacific candidate expected to be ENSO-sensitive |
| California–Hawaii, July | northeast-Pacific moderate candidate |
| Brisbane–Fiji, November | southwest-Pacific warm-season candidate |
| Cape Verde–Barbados, January | Atlantic trade-wind control |
| Bermuda–Azores, May | North Atlantic weak-signal control |
| Cape Town–Fremantle, December | remote southern-Indian teleconnection test |

Two separate questions were asked:

1. Does a route through each composite look different? A transparent
   coarse-grid Dijkstra proxy with a documented generic 35-ft cruiser polar
   measured geometry/ETA sensitivity.
2. Does the correct phase predict an unseen event better? Complete ENSO events
   were left out and corridor vector-wind RMSE was compared with all-years and
   opposite-phase composites.

Question 2 is the acceptance test. The proxy is not production Weather Routing,
does not include waves or a vessel-specific polar, and has unrealistic absolute
path lengths near coarse land masks. Only differences are used as a screening
signal. A true headless Weather Routing run using ERA5 distributions is still
required before production acceptance.

## Wind composites: geographically real, globally sparse

Across months/phases, the marine median phase-minus-neutral wind-vector anomaly
was 0.87–1.14 kn (RONI: 0.82–1.20 kn); the 90th percentile was 1.77–2.76 kn.
Only 0–14.0% of marine area survived FDR 0.05 for ONI and 0–11.8% for RONI.
This supports strong regional teleconnections, not a globally superior “El
Niño climatology”.

Representative maps:

* [April El Niño wind anomaly](../../research/climatology_modernisation/results/wind-anomaly-month-04-el_nino.png)
* [July El Niño wind anomaly](../../research/climatology_modernisation/results/wind-anomaly-month-07-el_nino.png)
* [November La Niña wind anomaly](../../research/climatology_modernisation/results/wind-anomaly-month-11-la_nina.png)

## Event-block wind skill

Positive numbers mean the correct phase reduced vector RMSE versus all-years.
Intervals are 5,000-replicate event-block bootstrap 95% intervals for the mean.

| Route/month, phase | ONI mean skill (95% CI) | RONI mean skill (95% CI) | Decision |
|---|---:|---:|---|
| Brisbane–Fiji Nov, El Niño | +6.48% (+0.45, +12.13); median +9.48%, 17/24 events positive | +6.33% (+0.23, +12.33); median +10.67%, 16/23 positive | only robust signal in this screen |
| Brisbane–Fiji Nov, La Niña | +4.15% (−3.67, +11.20) | +3.22% (−3.06, +8.76) | suggestive, not conclusive |
| Panama–Marquesas Apr, El Niño | −11.26% (−21.44, −1.61) | −5.12% (−10.97, +1.03) | conditioning worsened prediction |
| California–Hawaii Jul, El Niño | −3.44% (−5.49, −1.51) | −2.52% (−4.06, −1.01) | conditioning worsened prediction |
| Bermuda–Azores May, El Niño | −4.75% (−8.07, −1.79) | −6.05% (−10.88, −1.50) | conditioning worsened prediction |
| Cape Verde–Barbados Jan, La Niña | +3.36% (−2.66, +9.08) | −0.60% (−9.58, +7.97) | index-sensitive/no evidence |
| Cape Town–Fremantle Dec, El Niño | −0.21% (−5.73, +4.80) | −0.60% (−5.05, +3.48) | no evidence |

Other phase rows are in
[`leave-one-event-out-summary.csv`](../../research/climatology_modernisation/results/leave-one-event-out-summary.csv)
and the [RONI repeat](../../research/climatology_modernisation/results-roni/leave-one-event-out-summary.csv).

## Why composite route differences are not sufficient

The proxy produced operationally interesting-looking differences:

| Route/month | ONI composite differences versus all-years |
|---|---|
| Panama–Marquesas Apr | El Niño +23.9 h/+95.6 NM; La Niña −54.5 h/+84.8 NM; 78.4 h El-to-La spread |
| Brisbane–Fiji Nov | El Niño −20.4 h/−173 NM; La Niña +6.7 h/−238 NM |
| California–Hawaii Jul | all phase ETAs within 0.8 h |
| Bermuda–Azores May | El Niño +33.4 h; La Niña +60.2 h |
| Cape Town–Fremantle Dec | El Niño −13.1 h; La Niña −0.2 h |
| Cape Verde–Barbados Jan | no difference in this coarse/polar-capped proxy |

RONI changed several results materially: Brisbane neutral became +40.0 h while
both El Niño and La Niña were about −20 h versus all-years; Bermuda El Niño
became +105 h; Cape Town La Niña changed path by +168 NM. This sensitivity is
itself a warning.

Most importantly, the 78-hour Panama and 60–105-hour Bermuda spreads failed or
reversed out of sample. Visually striking composite routes can be event-specific
overfit. Brisbane–Fiji November is the sole case where the El Niño wind signal,
route change and event-block skill agree under both indices.

## Currents

The independent GODAS 5 m monthly screen (1981–2025, ~1° research stride) found:

* median phase-minus-neutral current-vector anomaly: 0.020–0.027 kn;
* 90th percentile: 0.066–0.108 kn;
* ocean area surviving FDR 0.05: 0–2.51%;
* Brisbane–Fiji La Niña corridor: median LOO improvement +6.46% ONI and
  +8.88% RONI, but this is one model and does not validate route ETA;
* most other corridors had negative or inconsistent mean skill.

These magnitudes can matter over long passages locally, but GODAS is too coarse
and model-dependent to justify a production regime current. Confirmation must
require agreement among GLORYS12, ORAS5 and OSCAR and true Weather Routing
sensitivity. Current conditioning is therefore **NOT ENOUGH EVIDENCE**.

## Pressure and SST

These fields establish physical consistency, not direct route value:

* NCEP R1 MSLP median absolute composite anomaly 0.49–0.92 hPa (RONI
  0.42–0.87); 90th percentile up to ~3.0 hPa; maximum FDR-significant area
  20.1% ONI/16.7% RONI.
* OISST 1981–2025 median absolute anomaly 0.13–0.20 °C (RONI 0.13–0.22);
  90th percentile up to 0.60/0.74 °C; maximum FDR-significant ocean area
  6.87/7.75%.

The SST result is expected because the index is SST-derived; it is not an
independent demonstration of sailing benefit.

## Waves

The bounded NOAA 1980–2009 wave-summary download contains only 12 long-term
calendar-month fields, not individual event years, so it cannot honestly test
ENSO conditioning. No wave-route benefit is claimed. Published ERA5 analysis
finds regional ENSO-related wave-power and direction changes, particularly in
the Pacific, but also event heterogeneity
([Odériz et al., 2020](https://doi.org/10.1029/2020JC016464)). A credentialled
WAVERYS/ERA5 event-block experiment remains a production gate.

## Tropical cyclones

The packaged IBTrACS 1995–2024 tracks were decoded and active-track counts were
cross-validated by complete ENSO event, basin and month. Effective event counts
were commonly only 4–11. Apparent 100% improvements often occurred in months
with zero or near-zero expected tracks; active-season signs varied by basin,
month and phase. This is classic small-count instability.

ENSO–cyclone relationships are physically real and basin-specific, but this
30-year experiment does not support a global phase switch. The useful future
product is a basin/season conditional historical-risk layer with event counts,
uncertainty and intensity conventions—not a deterministic “avoid this route”
field.

## Proposed route sensitivity metric

Do not expose LOW/MODERATE/HIGH yet. The next confirmation study should compute
for a vessel, route corridor and departure month:

1. event-block out-of-sample skill versus all-years;
2. confidence interval and effective event count;
3. spread in production Weather Routing ETA/geometry;
4. changes in calm, gale and wave-exceedance exposure;
5. agreement between ONI/RONI and at least two source products.

Only after a blinded route set establishes calibrated outcome thresholds should
labels be defined. Until then expose the continuous diagnostics and allow
“no demonstrated practical sensitivity”.

## Route-level conclusion

ENSO conditioning can improve one scientifically plausible regional/seasonal
case, but this screen does not demonstrate broad operational skill. A global
four-mode selector would imply evidence which does not exist. The right next
step is a regional experimental data pack and a real headless Weather Routing
qualification focused first on southwest-Pacific austral spring, with Atlantic
and northeast-Pacific controls retained to catch overfitting.
