# OpenCPN Climatology dataset ocpn-climatology-2026.1

Generated: 2026-08-18T04:50:24.720927+00:00
Target period: 1995-01-01 through 2024-12-31 (multi-product-target)
Target rolling period; authoritative per-product periods below take precedence where source coverage is shorter
Generator commit: `486ffba6b55d70daa437ce357b86a32f93ab6429`

## Products

* **wind atlas** (1995-01-01 through 2024-12-31) — Six-hourly ERA5 10 m U/V samples classified into eight 45-degree meteorological wind-from sectors; frequency, conditional speed, calm and gale probability
* **ocean surface current** (1995-01-01 through 2022-08-05) — OSCAR Final V2 daily total-current U/V monthly climatology
* **mean sea-level pressure** (1995-01-01 through 2024-12-31) — ERA5 monthly mean sea-level pressure climatology
* **air temperature** (1995-01-01 through 2024-12-31) — ERA5 monthly 2 m air-temperature climatology
* **relative humidity** (1995-01-01 through 2024-12-31) — Relative humidity derived from ERA5 monthly 2 m temperature and dew point
* **precipitation** (1995-01-01 through 2024-12-31) — ERA5 monthly mean total-precipitation rate converted to mean mm/day
* **total cloud cover** (1995-01-01 through 2022-12-31) — ERA5 monthly total-cloud-cover climatology
* **sea-surface temperature** (1995-01-01 through 2024-12-31) — NOAA OISST v2.1 monthly sea-surface-temperature climatology
* **tropical cyclone tracks** (1995-01-01 through 2024-12-31) — IBTrACS v04r01 main-track positions at six-hour synoptic times
* **bathymetric context** (2026-04-23 through 2026-04-23) — GEBCO_2026 wet-cell median depth, contextual and explicitly not for navigation
* **lightning** (1995-05-04 through 2013-12-31) — Retained observational NASA LIS/OTD HRMC v2.3.2013 monthly flash climatology
* **ENSO classification** (1950-01-01 through 2025-12-31) — NOAA CPC historical three-month Oceanic Nino Index based on ERSSTv6

## Sources

* **ECMWF ERA5 hourly data on single levels** — 10.24381/cds.adbb2d47
* **Planette ERA5 monthly archive** — planette-era5/month/0p25latx0p25lon
* **NSF NCAR GDEX ERA5 monthly single levels** — ds633.0/d633001/2/TP
* **OSCAR Surface Currents Final 0.25 Degree Version 2.0** — 10.5067/OSCAR-25F20
* **NOAA Optimum Interpolation SST Version 2.1** — 10.25921/RE9P-PT57
* **International Best Track Archive for Climate Stewardship Version 4r01** — 10.25921/82ty-9e16
* **GEBCO_2026 Grid** — 10.5285/4f68d5c7-45eb-f999-e063-7086abc036fa
* **NASA LIS/OTD High Resolution Monthly Climatology v2.3.2013** — LISOTD_HRMC_V2.3.2013
* **NOAA CPC Oceanic Nino Index based on ERSSTv6** — CPC-ONI-v6

## Validation

The machine-readable manifest embeds the complete deterministic before/after report.
Advisory regional checks passed: 28/28.
Hard source-to-package checks passed: 4/4.

## Output checksums

* `airtemperature.gz` — 101788 bytes — SHA-256 `de918c195a71855706ce28bcfa9829c9403a05f379dacaed02f53775d3743bbd`
* `cloud.gz` — 138640 bytes — SHA-256 `cb48fffea280bce37871e891e7dd1833686780e87794f225abd7e6b520dd33e1`
* `current01.gz` — 174925 bytes — SHA-256 `d6cd2f6e01f05490801e619c08dbbb56990009dfbbfec11020782f207a7d4c09`
* `current02.gz` — 173700 bytes — SHA-256 `b74e8b3141c66fb3b9717385511b4deb41a6042256901403b0ad5967b688a9f1`
* `current03.gz` — 171510 bytes — SHA-256 `c52a2ceba2ddd13cc4f22d5a7b847ce372f639461db2839e747059127a3380f3`
* `current04.gz` — 170182 bytes — SHA-256 `12cd851649d96dc18576a50b8f0436733757870a12c7c1e17ba30f1363bd4fd8`
* `current05.gz` — 172292 bytes — SHA-256 `bd0eaeb75816e806b37e72b28e374fa483003e75712d0f4d4bf4899b103c439b`
* `current06.gz` — 175568 bytes — SHA-256 `b65dca4418489460a7fce164ee34027d1f5bf9f1092dab4f36c131abcab5b1aa`
* `current07.gz` — 177201 bytes — SHA-256 `c4ab978a28767cc479f157aad5bf704b7a9c9f649bb01ae999a2555118e05b62`
* `current08.gz` — 176895 bytes — SHA-256 `5326de9d021a6796ccfaf7439eb754358dc69b7c3645aa442a67f51e3a4e897b`
* `current09.gz` — 176210 bytes — SHA-256 `c13c0926ed8a48e8fecbe4f2fdacd35f1fe0ac73ebc5d38fceffb9ab26e9c178`
* `current10.gz` — 175626 bytes — SHA-256 `d6a87f3fa4234e03fe1a134ae4ff03aaf695e2538d03da57157e10929e3d6a5a`
* `current11.gz` — 176214 bytes — SHA-256 `c97fe0ccffedb6e95ca3e3d3fb9a2b5d2c0b9171e46ac3d68f4668d1554ac23f`
* `current12.gz` — 176589 bytes — SHA-256 `60cea3f125471e7fd981b78fdbfa660c4c2166507598b4ab51ed538a26105408`
* `cyclone-atl.gz` — 105939 bytes — SHA-256 `203c897fff1a99de32401bf41e71b16876a9623c035c3a50b6cf6b69296a398d`
* `cyclone-conversion.json` — 2396 bytes — SHA-256 `12d7dc9d7a9d002c4bbb3ccfc1e391d617254958b7d9b39f533981bced85d7cc`
* `cyclone-epa.gz` — 99512 bytes — SHA-256 `7ddffe6bf03ab8100cc70721392d8e11da0d233d62ec561fcf41912eab913bd5`
* `cyclone-nio.gz` — 32332 bytes — SHA-256 `748636e5b150a333419c3cb88bac2dee4f49a7903e2cd294a07d5043862d7397`
* `cyclone-she.gz` — 124650 bytes — SHA-256 `6329d2d821c79b00b7fffd2040e7943c34eeef9375014e4280246f1a27db4a9c`
* `cyclone-spa.gz` — 58868 bytes — SHA-256 `a448dfb91146f7a19ddbc4ff1e9e47d3a1bc757e51cec20deeb8f5c13ac433ef`
* `cyclone-wpa.gz` — 189266 bytes — SHA-256 `3f215411d1e7b91df98535947c85824b20ec5ed246c021c8a6ab79748747d81c`
* `elnino_years.txt` — 4529 bytes — SHA-256 `dcf4a0b26a4b281708d71430d76764df3f25e6cdeb76908d27a9d3e1afbac603`
* `lightning.gz` — 463626 bytes — SHA-256 `7dcb59a00d88cfbf40f2a95a08827c1920fc2eecd60df71e223e09944602e243`
* `precipitation.gz` — 54411 bytes — SHA-256 `2cb2594ff163e7f1b86032acceb7c69ffe5c93f5004771051f5e1b9ba0c4bdc5`
* `relativehumidity.gz` — 358724 bytes — SHA-256 `06004c7ab3818d7031482a79809c108bf586d12368f6eb5acab03c2d2e5e70fa`
* `seadepth.gz` — 16968 bytes — SHA-256 `5f8c9a8f88cdd0c938c4f00ccdc33db6a0a670fe97cced9f998de9f3461ae2e1`
* `sealevelpressure.gz` — 296416 bytes — SHA-256 `9bcd70469d4da2ef4b4d4e47b54c1b08ec54043140d15412e91dfd4de436683f`
* `seasurfacetemperature.gz` — 215642 bytes — SHA-256 `89f0a9908d51f6edb012e60bbfe32e7b094dcc6f1907fa9c42b7f5bf074130a1`
* `wind-extras01.gz` — 42783 bytes — SHA-256 `189f7b91566a77e3539c48ccccb243e336a53256994141ae65f0645fdaaa7a51`
* `wind-extras02.gz` — 44336 bytes — SHA-256 `c682f3abdb4b038ac7480d936089aa9ef0ca7962135099736ecb90236999ec04`
* `wind-extras03.gz` — 44554 bytes — SHA-256 `e3c778539aac8cd0eecf0854a0c05cf9fb984cfe0b1b9518ed4bd64731916bb9`
* `wind-extras04.gz` — 45280 bytes — SHA-256 `b20cbc911fe1f20d628666c58f7e9780f5912cbe8b7fe5b1b4bb435e49e0b6b5`
* `wind-extras05.gz` — 44375 bytes — SHA-256 `93e2329ec43e6d47ce78d7d326f2da6f1b6931cc24f98767f002f341e1045713`
* `wind-extras06.gz` — 43319 bytes — SHA-256 `af75c6aa9e5ef26e9ffb81807b953a078f2554aebaadd32779af09ef9b16e843`
* `wind-extras07.gz` — 42585 bytes — SHA-256 `414676d66521b6a4e92a4f34ba8e5beb96a36903e435f1425c2256346dc660f2`
* `wind-extras08.gz` — 43563 bytes — SHA-256 `0b73570f34f4732538286104d11cac2e902cd86435198305021f82449b01d837`
* `wind-extras09.gz` — 44753 bytes — SHA-256 `4fc92a5e00110ab613acb25bd13bfe702eb91386b1af3f4c79235f73138df745`
* `wind-extras10.gz` — 43761 bytes — SHA-256 `da9e08e6f040a9df813405192838156ac801860161319ffdda40f28bf9078ab1`
* `wind-extras11.gz` — 43065 bytes — SHA-256 `20f85ce90c745ef36f5a64d3148d180f608e39ceb49b26bf8009662f7c7e360c`
* `wind-extras12.gz` — 42176 bytes — SHA-256 `248b93ec8dcc7c182a92ed30a686920fc7eeed576be02c89923110519dc1ec72`
* `wind01.gz` — 499404 bytes — SHA-256 `0514f218b4b0aef16aefe78dfbe6f1029231c1be09497cfe2b5af5b0a8e41807`
* `wind02.gz` — 509236 bytes — SHA-256 `71ef7797a98fac5153764f9a5be7add3f7a2e34e796d4cd5c510f27fa1da5e36`
* `wind03.gz` — 502317 bytes — SHA-256 `2ac48ea2b7981ffa32c4b1107f48b79882827c65d41bf1aef568cbbfb9e8dfb2`
* `wind04.gz` — 501138 bytes — SHA-256 `a9bc67cf9ef24489dfec37591daae0f5cda427064140acffa56ded93f321b7b7`
* `wind05.gz` — 489799 bytes — SHA-256 `a7efa9d8953dfbe6ea291c5215719eb8c4720d8634a4fbc55e9c74d8b296f9cb`
* `wind06.gz` — 483975 bytes — SHA-256 `0e129747c688f388e33b0c3eb07ee6a62d330706e920bb1121294f082fd17c47`
* `wind07.gz` — 481398 bytes — SHA-256 `83a734c65d83aea64566fe09e9d547f52976f57958d2ffb1d257ad065045d18b`
* `wind08.gz` — 482342 bytes — SHA-256 `aef069281e845738a865adcb9a30de7968d3ec618148cead7c585660283d75de`
* `wind09.gz` — 493158 bytes — SHA-256 `bf352ddf72df01d8765dc856138f0b8f24cc3c12fd7de3b33a168ed95168b38d`
* `wind10.gz` — 498003 bytes — SHA-256 `3dfe7883c12047d6d7eeaa67e6fb11ee498926752a6149120cc104e63eb8e97b`
* `wind11.gz` — 498063 bytes — SHA-256 `eec990f110af5679fde62256c25abe94700eedf37e2a6a559e04b29c415939b6`
* `wind12.gz` — 493282 bytes — SHA-256 `05a6fdee12ea35846de103a7bc6c5a87483595ae9fefd3d222a3258db59f2306`

This climatology is for planning and routing context; it is not a substitute for current forecasts or official navigational data.
