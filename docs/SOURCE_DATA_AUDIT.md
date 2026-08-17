# Modern source-data audit and provenance plan

## Dynamic climatology period

Target: 1995-01-01 through 2024-12-31 inclusive. This is a rolling 30-year
navigation climatology, not a WMO standard normal. It is not falsely presented
as one common period where source coverage is shorter. Every output records
the exact inclusive period per variable: currently wind, MSLP, temperature,
humidity and precipitation 1995–2024; cloud 1995–2022; OSCAR Final
1995-01-01–2022-08-05; and OISST/IBTrACS 1995–2024. The WMO 1991–2020
standard period remains configurable.

## Source assessment

### Wind and atmosphere — ECMWF/Copernicus ERA5

ERA5 provides hourly global fields on a native ~31 km model grid and regular
0.25-degree distribution grid. Use 10 m U/V, mean sea-level pressure, 2 m
temperature, 2 m dewpoint, total cloud cover, and total precipitation. The
official geo-chunked ARCO Zarr store supports time- and spatially-bounded reads.
For monthly scalar fields the public Planette/NSF-NCAR monthly ERA5 archive is
used as an efficient transport; NCAR GDEX supplies monthly total cloud. Their
metadata and selected samples are cross-checked rather than treated as a new
scientific product.

Wind aggregation is sample-based into the eight historical sectors. Six-hourly
is the default production cadence; hourly is the reference mode. Air, pressure,
humidity and cloud use ERA5 monthly means. Precipitation uses ERA5 mean total
precipitation rate and reports mm/day, avoiding accumulation-boundary ambiguity.

Risks: ERA5 is a reanalysis, not direct observation; 10 m wind over land/coasts
requires a sea mask; six-hour subsampling may alias the diurnal cycle; total
cloud parameter meaning differs from old COADS observations.

### Ocean currents — NASA/JPL PO.DAAC OSCAR Final V2.0

Use product DOI `10.5067/OSCAR-25F20`: daily, 0.25-degree surface-current U/V.
Final quality is preferred over interim/NRT. It combines sea-surface-height
gradients, vector wind, and SST in a diagnostic model. Monthly U/V means are
formed over the available 1995-01-01–2022-08-05 period and resampled as vector
components before speed/direction.

Risks: OSCAR represents mixed-layer surface current and has coastal/missing
cells. Never interpolate direction directly; interpolate U/V. The source's
exact final-data end date is checked at acquisition and a shortfall aborts or
changes the declared period—never silently fills with interim data.

### Tropical cyclones — NOAA/NCEI IBTrACS v04r01

Use DOI `10.25921/82ty-9e16`, best tracks in 1995–2024, excluding provisional
spurs. Preserve six-hour observations. The primary one-minute wind series is
`USA_WIND` (NHC/JTWC combination); where unavailable, wind is missing rather
than silently mixing WMO agencies' 1-, 3-, and 10-minute winds. Position and
pressure selection rules, basin mapping, and rejected rows are counted in the
manifest.

Risks: no single agency intensity series is complete globally. Track positions
can still use WMO/main-track records while homogeneous wind remains missing.
The UI must not imply cyclone tracks are deterministic risk contours.

### Sea-surface temperature — NOAA OISST v2.1

Use NOAA Optimum Interpolation SST v2.1 daily 0.25-degree data, 1995–2024,
monthly climatological means. It is chosen over ERA5 skin temperature because
SST is its primary analysed product. Resample with missing-aware bilinear
interpolation of the physical field, retaining coastal missingness.

### Bathymetry — GEBCO_2026

Use the current 15-arc-second global GEBCO_2026 grid but stream/downsample it to
the one-degree legacy output. Encode the nearest legacy depth-bin index. The
manifest records aggregation and wet-cell policy. This layer is contextual and
must be labelled not for navigation.

### Lightning — NASA LIS/OTD

The existing observed `LISOTD_HRMC_V2.3.2013` High Resolution Monthly
Climatology is retained.  The historical generator names that exact upstream
HDF file and converts its twelve 0.5-degree flash-rate grids to the plugin's
one-degree nonlinear byte encoding.  NASA's v2.3 product documentation gives
the family coverage as beginning 1995-05-04; the retained 2013 edition predates
the later v2.3.2015 extension through 2014, so its conservative declared period
is 1995-05-04 through 2013-12-31.  The packaged encoded file has SHA-256
`7dcb59a00d88cfbf40f2a95a08827c1920fc2eecd60df71e223e09944602e243`.

The upstream HDF object was not distributed with the historical repository,
so its original object checksum cannot be reconstructed from the encoded
payload.  This limitation is recorded rather than inventing provenance.
Modern ERA5 variables are not accepted as a lightning proxy. A newer product
can replace it only if it is observational, global enough for marine use,
monthly, and its coverage limitations are explicit.

### ENSO — NOAA CPC historical ONI (ERSSTv6)

Use the versioned/access-dated historical Oceanic Niño Index table based on
ERSSTv6. ENSO is auxiliary to cyclone filtering and is not forced to the
dynamic climatology period. Retain the plugin's ONI interpretation; do not
silently substitute CPC's newer operational Relative ONI.

## Provenance manifest

`dataset-manifest.json` is installed beside the binaries and contains:

* dataset and schema versions;
* UTC generation time;
* generator Git commit and dirty flag;
* per-variable inclusive period;
* source title, DOI/product ID, version and access URL/date;
* source variables and units;
* sampling cadence and temporal aggregation;
* source and target grids, masks and resampling;
* encoding scale, missing sentinel and quantisation bounds;
* acquired-object and final-file SHA-256 checksums;
* validation results and software dependency versions.

The same core information is rendered into a human-readable
`DATASET_PROVENANCE.md`. Plugin messaging adds dataset version/period fields
without changing existing pointers.
