# Candidate source matrix for Climatology modernisation

Status: research decision record, 2026-08-25.  “Derived redistribution” is a
technical assessment, not legal advice.  Before publishing any new data pack,
freeze the exact product version and licence text in the build manifest and
repeat the licence review.

## Recommended production combination

| Field | Primary build source | Independent check | Recommended period/product |
|---|---|---|---|
| Wind, pressure and atmospheric fields | ERA5 hourly single levels | JRA-3Q or NCEP R1 for coarse sensitivity checks | 1991–2020 standard normal; optionally a separately labelled recent 1996–2025 normal |
| Surface current | GLORYS12 daily/monthly, conservatively regridded | ORAS5 ensemble and OSCAR; GODAS only for screening | 1993–latest common complete year; report disagreement/uncertainty |
| Waves | WAVERYS three-hourly or ERA5 hourly waves | the other product | 1991–2020 where the selected archive permits; retain Hs, direction, period and exceedances |
| SST | NOAA OISST v2.1 daily/monthly | ERA5/HadISST only as sensitivity checks | 1991–2020 and recent normal |
| Tropical cyclones | IBTrACS v4r01 main best tracks | basin-agency best track where a release test needs it | satellite era, normally 1980 onward; preserve agency/intensity caveats |
| ENSO | CPC ERSSTv6 ONI for historical compatibility study; RONI sensitivity mandatory | MEI.v2/SOI only as sensitivity tests | official table, episode rule and retrieval date frozen in manifest |

## Atmosphere

| Product/provider | Coverage, grid and cadence | Useful variables | Scientific/engineering assessment | Access, licence and redistribution |
|---|---|---|---|---|
| ERA5, ECMWF/Copernicus C3S | 1940–present; native ~31 km; hourly; regular downloads commonly 0.25° atmosphere and 0.5° waves | 10 m U/V, MSLP, temperature/dew point, precipitation, cloud, wave fields | Best single production backbone. Assimilative reanalysis, not observation; observing-system changes and coastal/model biases remain. Hourly input is required for calm/gale and joint wind distributions. | CDS API/ARCO; registration may be required. C3S attribution/licence applies. Archive only derived, quantised monthly statistics, attribution and source DOI—not raw fields. [ERA5 product](https://www.ecmwf.int/en/forecasts/dataset/ecmwf-reanalysis-v5). |
| JRA-3Q, Japan Meteorological Agency | 1947–present; TL479 (~40 km); 6-hour analyses plus monthly products | Surface/pressure atmosphere | Strong independent reanalysis for structural uncertainty; not needed in the small runtime pack. Check near-surface marine wind conventions before numerical comparison. | JMA data portal/registration and terms; derived redistribution must be cleared against the exact JRA-3Q terms before release. |
| NCEP/NCAR Reanalysis 1, NOAA PSL | 1948–present; T62/~1.875° wind, 2.5° many monthly fields; 4/day and monthly | 10 m wind, pressure | Long, open and easy to automate, but old model/assimilation and coarse grid. Used in this study only as a credential-free screening and independent-validation source; **not** the production recommendation. | PSL HTTP/OPeNDAP; NOAA-origin data are generally public-domain/CC0, but inspect collection metadata. [PSL product](https://psl.noaa.gov/data/gridded/data.ncep.reanalysis.html). |
| NOAA 20CRv3 | 1806–2015; 1°; 3-hour; 80-member ensemble | Pressure, circulation and uncertainty | Excellent for long pressure/circulation context and pre-satellite uncertainty; surface-pressure-only assimilation makes it unsuitable as the sole modern marine wind climatology. | NOAA PSL. Large ensemble; subset statistically, never bundle raw members. [20CRv3](https://psl.noaa.gov/data/gridded/data.20thC_ReanV3.html). |
| MERRA-2, NASA GMAO | 1980–present; ~0.5° × 0.625°; hourly/3-hour/monthly | Atmosphere, aerosols, surface wind | Useful independent trend/bias check, not a better compact primary than ERA5. | Earthdata registration; NASA attribution and exact product terms apply. |

Global ERA5 hourly 0.25° inputs are many terabytes if downloaded naively.
Streaming one variable/year/month and accumulating statistics is feasible; a
local raw archive is neither necessary nor desirable.  The repository's
existing block/checkpoint pipeline is the right pattern.

## Ocean currents

| Product/provider | Coverage, grid and cadence | Useful variables | Scientific/engineering assessment | Access, licence and redistribution |
|---|---|---|---|---|
| GLORYS12V1, Copernicus Marine/Mercator Ocean | 1993–present; 1/12°, 50 levels; daily and monthly | surface/subsurface U/V, SSH, temperature, salinity | Eddy-resolving assimilative product and strongest detailed primary candidate. Effective coastal detail does not justify retaining 1/12° in the runtime climatology. Shorter altimetry-era record and model dependence limit regime sample size. | Copernicus Marine account/API. Derived products require Copernicus attribution; verify current licence at release. [Product DOI 10.48670/moi-00021](https://data.marine.copernicus.eu/product/GLOBAL_MULTIYEAR_PHY_001_030/description). |
| ORAS5, ECMWF | 1958–present family; 0.25°, 75 levels; monthly; historical five-member ensemble | ocean U/V, temperature, salinity, SSH, ensemble spread | Valuable longer-period and ensemble structural/initial-condition check. Consolidated and operational streams must not be silently spliced. Known current-system position/bias errors make it unsuitable as “truth”. | ECMWF/C3S access and attribution. [ORAS5 overview](https://www.ecmwf.int/en/research/climate-reanalysis/ocean-reanalysis). |
| OSCAR Final v2, NASA/JPL PO.DAAC | 1993–2022-08-05 final archive; 0.25°; daily; mixed-layer/top-30-m diagnostic velocity | total surface U/V | Appropriate compact broad-scale cross-check and the current plugin source. It is not an eddy-resolving, near-shore or depth-specific current forecast; final/interim/NRT streams must remain distinct. | Earthdata authentication. Product DOI 10.5067/OSCAR-25F20; verify NASA/product citation and derived-redistribution terms. [OSCAR Final v2](https://podaac.jpl.nasa.gov/dataset/OSCAR_L4_OC_FINAL_V2.0). |
| GODAS, NOAA/NCEP | 1980–present; 1° longitude × 1/3° latitude; monthly, 40 levels | 5 m and subsurface current | Public and reproducible screening product used here. Tropical observing emphasis, coarse zonal grid and model/assimilation bias preclude it as the production source. | NOAA PSL OPeNDAP. [GODAS](https://psl.noaa.gov/data/gridded/data.godas.html). |

Current climatology should expose vector mean plus directional persistence,
speed percentiles and product disagreement.  A single “mean current” can hide
reversing or eddy regimes.  Reanalysis precision is not coastal-current truth.

## Waves

| Product/provider | Coverage, grid and cadence | Useful variables | Scientific/engineering assessment | Access, licence and redistribution |
|---|---|---|---|---|
| WAVERYS global wave reanalysis, Copernicus Marine | 1980–present; 0.2°; hourly/monthly products (core source sampling 3-hourly) | significant wave height, mean/peak period, direction, partitions, Stokes drift | Preferred wave candidate. It uses GLORYS currents and assimilates altimeter Hs; directional spectra assimilation improves recent years, which also creates observing-system nonstationarity to assess. | Copernicus Marine API/account and attribution. [GLOBAL_MULTIYEAR_WAV_001_032](https://data.marine.copernicus.eu/product/GLOBAL_MULTIYEAR_WAV_001_032/description). |
| ERA5 wave reanalysis | 1940–present family; regular 0.5° wave grid; hourly/monthly | Hs, direction, period, wind wave and swell partitions | Long, convenient companion to ERA5 atmosphere and required independent check against WAVERYS. Coarser coastal representation and wave-model/forcing biases remain. | CDS/ARCO under C3S terms. |
| NOAA CFSR WAVEWATCH III hindcast | 1979–2009; global hindcast; spectral/point archives and long-term summaries | Hs, direction, period, spectra | The provider describes the 30-year run as homogeneous and suitable for climate. Useful historical sensitivity check, but it ends in 2009. The small public “monthly summary” file contains only Jan–Dec long-term fields, not event years, so it cannot test ENSO conditioning. | NOAA/NCEP archive; check each derived/partner component. [CFSR hindcast](https://polar.ncep.noaa.gov/waves/CFSR_hindcast.shtml). |

The runtime fields with sailing value are Hs median/upper percentiles,
exceedance probabilities, dominant direction and period—not only mean Hs.
There is published evidence for regionally varying ENSO wave-power/direction
signals, but it does not establish global passage-routing skill. See
[Odériz et al. (2020)](https://doi.org/10.1029/2020JC016464).

## SST

| Product/provider | Coverage, grid and cadence | Assessment | Access/licence |
|---|---|---|---|
| NOAA OISST v2.1 | Sep 1981–present; 0.25°; daily and monthly | Preferred modern blended SST climate record: satellites plus in-situ ships/buoys/Argo, with ice treatment. It is still an analysis with changing inputs. The study used a 2° research stride and validated physical range/coverage. | NOAA NCEI/PSL; generally NOAA public-domain/CC0 with citation, but freeze product metadata. [OISST](https://www.ncei.noaa.gov/products/optimum-interpolation-sst). |
| HadISST1, UK Met Office Hadley Centre | 1870–present; 1° monthly | Valuable long-record sensitivity check, especially pre-satellite, but lower resolution and reconstruction/sampling uncertainty. | Download/use terms are not assumed to permit arbitrary redistribution. Clear the exact licence before including derived fields. |
| ERA5 SST/skin temperature | 1940–present | Useful consistency check, not preferred over purpose-built OISST for the runtime SST layer. | C3S terms. |

## Tropical cyclones

IBTrACS v4r01 is the most complete merged global best-track collection, spans
the 1840s–present and is generally three-hourly.  Use `MAIN` tracks, a modern
satellite-era start, and one documented intensity convention.  Pre-satellite
undercount, provisional tracks, inter-agency wind averaging periods and changing
practice prevent naive global trend or intensity comparisons.  The official
metadata explicitly warns that agency wind practices can mask climatological
change. [IBTrACS product and caveats](https://www.ncei.noaa.gov/products/international-best-track-archive).

Derived track density, passage probability and intensity distribution are
useful.  Regime-conditioned cyclone statistics need basin/season-specific
event-block validation; global monthly phase ratios are unstable where counts
are small.

## ENSO and other indices

| Index | Definition/use | Decision |
|---|---|---|
| CPC ONI v6 | ERSSTv6 Niño-3.4 three-month running anomaly; ±0.5 °C for at least five overlapping seasons | Reproducible historical reference for this study. [Official ONI](https://www.cpc.ncep.noaa.gov/products/analysis_monitoring/enso/oni/v6/). |
| CPC RONI | Niño-3.4 anomaly relative to tropical mean; operational CPC standard since Feb 2026; same episode threshold/duration | Mandatory sensitivity analysis because warming changes conventional ONI classifications. [RONI announcement](https://www.cpc.ncep.noaa.gov/products/analysis_monitoring/enso/roni/announcement.php). |
| MEI.v2 | Multivariate tropical Pacific atmosphere/ocean index, 1979–present | Useful sensitivity check but shorter and not a reason to add another UI selector. |
| SOI | Pressure-based Tahiti–Darwin index, long record | Useful dynamical sensitivity check; not interchangeable with SST-defined event labels. |

Seasonal outlooks must retain phase probabilities.  CPC's outlook is
probabilistic and the boreal-spring prediction barrier limits lead skill; a
manual historical composite is not a forecast. [CPC RONI outlook](https://www.cpc.ncep.noaa.gov/products/analysis_monitoring/enso/roni/outlook/).

## Licensing release gate

1. Record product ID, DOI/version, retrieval timestamp, access URL and licence
   hash/text in the provenance manifest.
2. Retain no raw reanalysis in the plugin archive; publish only heavily
   aggregated/quantised statistics and processing source revision.
3. Include exact provider attribution. Copernicus Marine gives separate wording
   for derivatives and redistribution ([citation guidance](https://help.marine.copernicus.eu/en/articles/4444611-how-to-cite-copernicus-marine-products-and-services)).
4. Do not infer that every file hosted by NOAA is CC0: NCEI can hold third-party
   material. Verify collection metadata even though NOAA-origin data are
   generally public domain/CC0 ([NCEI policy](https://www.ncei.noaa.gov/archive)).
5. Treat Earthdata authentication as access control, not automatically as a
   redistribution licence.
6. Make publication contingent on a clean automated provenance/licence report.
