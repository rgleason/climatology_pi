# Climatology modernisation research

This directory contains reproducible research code.  It is deliberately not
linked into the plugin build and does not alter runtime behaviour.

The first experiment uses NOAA/NCEP Reanalysis 1 as a credential-free,
low-resolution *screening and independent-validation* dataset.  It is not the
recommended production source.  The scripts quantify monthly ENSO composites,
event-block bootstrap uncertainty, false-discovery-rate controlled map area,
leave-one-event-out skill, recent-baseline drift and sailing-route proxies.

## Reproduce

Create an isolated environment and install `requirements.txt`, then run:

```sh
python fetch_inputs.py --cache cache
python run_study.py --cache cache --output results
python fetch_ocean_samples.py --cache cache
python analyse_ocean_pressure.py --cache cache --output results
PYTHONPATH=.:../../tools/climatology_pipeline \
  python analyse_cyclones.py --data ../../data --oni cache/oni-v6.html \
  --output results
python compare_indices.py --oni cache/oni-v6.html --roni cache/roni-v6.html \
  --output results/enso-index-comparison.csv
python storage_model.py --output results/storage-estimates.csv
```

`fetch_inputs.py` downloads about 130 MB.  It records URL, retrieval time,
size and SHA-256 in `cache/source-manifest.json`.  Raw source files and the
local virtual environment are git-ignored.  Derived CSV, JSON and compact PNG
evidence in `results/` is intended to be versioned.

The ENSO parser reads table cells, not flattened HTML.  It maps each CPC
three-month season to its centre month and applies the official requirement of
at least five consecutive overlapping seasons at |ONI| >= 0.5 deg C.  This is
important: the plugin's current production parser flattens styled HTML and is
not used by this research.

## Scope and limitations

- Monthly wind vectors cannot recover sub-monthly wind distributions.  Global
  results from these files therefore test regime signal and route-scale vector
  sensitivity, not the complete future wind-atlas product.
- The route calculation is a transparent climatological sailing proxy using a
  documented polar and great-circle/corridor alternatives.  It mirrors Weather
  Routing's probability-weighted use of climatology where the input permits,
  but is not a claim that a production OpenCPN route was run.
- ERA5, GLORYS/ORAS5 and WAVERYS are assessed as production candidates in the
  research report.  Their large archives are not downloaded by this bounded
  experiment.
