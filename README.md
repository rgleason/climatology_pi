# OpenCPN Climatology — legacy-compatible modernisation branch

This branch keeps the Climatology function-pointer interface used by Weather
Routing while replacing the anonymous historical data-generation process with
strict readers, reproducible Python builders, source provenance and tests.

The plugin remains able to read the released historical files. A generated
modern bundle carries `dataset-manifest.json`; plugin and dataset versions are
reported separately. The optional `wind-extrasMM.gz` sidecars preserve both
calm and gale probability, while old plugins continue to use `windMM.gz`.

This is research/planning climatology, not a forecast and not official
navigational data.

## Build and test the plugin

```sh
cmake -S . -B build -DBUILD_TESTING=ON -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build -j2
ctest --test-dir build --output-on-failure
```

The ABI test freezes the three exact pointer signatures consumed by Weather
Routing. The runtime tests cover dateline/polar handling, missing-data
interpolation and direction interpolation through 0/360 degrees.

## Install and test the data pipeline

Use a virtual environment. Source acquisition dependencies are deliberately
separate from the small runtime package:

```sh
python -m venv .venv-source
. .venv-source/bin/activate
python -m pip install -e './tools/climatology_pipeline[source,planette,test,earthdata]'
cd tools/climatology_pipeline
pytest -q
```

The default acquisition plan enforces a 100 GB decimal peak-workspace ceiling.
ERA5 and OSCAR are folded into bounded checkpoints; complete raw archives are
not retained.

## Rebuild individual products

### ERA5 wind and atmosphere

The six-hour public WeatherBench2 ERA5 copy ends in 2022. The wind probability
distribution requires the instantaneous samples:

```sh
climatology-build-wind \
  --output WORK/dataset --checkpoint WORK/wind-1995-2022.npz \
  --start-year 1995 --end-year 2022 --budget-gb 100
```

The checkpoint may then be extended for wind-only 2023–2024 from the current
public Google ARCO store:

```sh
climatology-build-wind \
  --output WORK/dataset --checkpoint WORK/wind-1995-2022.npz \
  --start-year 1995 --end-year 2024 --budget-gb 100
```

Every instantaneous U/V pair is classified into the legacy eight
meteorological wind-from sectors. Monthly mean vectors are never substituted
for a wind distribution. Calm is <=3 kn and gale is >=34 kn.

Independent, non-overlapping year ranges may be built concurrently and then
combined with `climatology-merge-wind`. The merge adds the unquantised
sufficient statistics, so its result is numerically identical to a serial
pass; it is not an average of already-encoded atlas files. Versioned progress
metadata is mandatory, and the merger rejects overlapping or gapped effective
year ranges.

Monthly scalar atmosphere fields use bounded annual chunks from public ERA5
monthly archives rather than rereading all six-hour samples:

```sh
climatology-build-atmosphere WORK/dataset \
  --start 1995-01-01 --end 2024-12-31

climatology-build-cloud WORK/dataset/cloud.gz \
  --start 1995-01-01 --end 2022-12-31
```

MSLP, temperature, dew point/RH and mean precipitation rate use the public
Planette archive derived from NSF-NCAR ERA5. Total cloud uses NCAR GDEX's
monthly ERA5 aggregation. The manifest records both the scientific source and
transport, including their differing end dates.

### OSCAR Final V2 currents

NASA Earthdata Login is required. Put credentials in a protected `~/.netrc`;
the program does not print or store them. Final V2 presently ends on
2022-08-05, so that exact period is declared rather than filling later years
from a different quality stream.

```sh
climatology-build-current WORK/dataset --earthdata \
  --workspace WORK/oscar-temporary --checkpoint WORK/oscar-final.npz \
  --start 1995-01-01 --end 2022-08-05 --budget-gb 100
```

Daily granules are downloaded and deleted in monthly batches. U/V components
are averaged before any speed/direction conversion.

Independent, contiguous OSCAR date ranges may be acquired concurrently into
separate checkpoints and combined with `climatology-merge-current`. The merger
rejects overlaps, gaps, incomplete ranges, other OSCAR quality streams and
different grids before adding the unquantised U/V sums and sample counts.

### OISST, GEBCO, IBTrACS and ENSO

```sh
climatology-build-sst WORK/dataset/seasurfacetemperature.gz \
  --source WORK/source/sst.mon.mean.nc --start 1995-01-01 --end 2024-12-31

climatology-build-depth WORK/source/GEBCO_2026.nc WORK/dataset/seadepth.gz

climatology-build-cyclones WORK/source/ibtracs.ALL.list.v04r01.csv \
  WORK/dataset

climatology-build-enso WORK/dataset/elnino_years.txt
```

Use NOAA's official OISST HTTP NetCDF file. The NOAA DAP2 endpoint has been
observed returning silent zero-filled full-grid reductions with some netCDF4
clients; the builder's global physical-range check rejects that failure.

GEBCO is streamed one latitude-degree slab at a time. Each output cell is the
median of its wet source elevations and is then quantised to the historical
40-bin display table. It is explicitly not for navigation.

The modern cyclone files use IBTrACS v04r01 main-track positions and
`USA_WIND`/`USA_PRES`; missing intensity remains missing instead of mixing
incompatible agency wind-averaging periods. ENSO uses the NOAA CPC ERSSTv6
ONI table and omits incomplete current-year rows.

Observed NASA LIS/OTD HRMC v2.3.2013 lightning (1995–2013) is retained
byte-for-byte from the historical bundle until a documented, genuinely
observational global monthly successor is selected. The old generator records
the source filename but the original HDF/checksum is unavailable, so that
provenance limitation is explicit.

## Provenance and validation

`climatology-inspect` strictly decodes old or new legacy files. Generate both
machine- and human-readable provenance after all outputs have passed numerical
validation:

```sh
climatology-compare OLD_DATA WORK/dataset \
  --wind-checkpoint WORK/wind-1995-2024.npz \
  --current-checkpoint WORK/oscar-final.npz \
  --output WORK/dataset/dataset-validation.json

climatology-write-provenance REPOSITORY WORK/dataset \
  WORK/dataset/*.gz WORK/dataset/elnino_years.txt \
  WORK/dataset/cyclone-conversion.json \
  --products tools/climatology_pipeline/metadata/products-2026.1.json \
  --sources tools/climatology_pipeline/metadata/sources-2026.1.json \
  --validation WORK/dataset/dataset-validation.json
```

The optional checkpoints bind the packaged bytes back to the unquantised
sufficient statistics accumulated from ERA5 and OSCAR. Wind output must match
the source-derived quantised atlas exactly; current U/V must match within half
of the legacy 0.05 kn encoding step. This is separate from—and stronger than—
the advisory old-versus-new regional checks.

See `docs/IMPLEMENTATION_AUDIT.md`, `docs/LEGACY_DATA_FORMAT.md`,
`docs/SOURCE_DATA_AUDIT.md` and `docs/CLIMATOLOGY_MODERNISATION.md`.

Never copy a development build into a working OpenCPN profile without first
testing it in an isolated installation and keeping a rollback copy.
