# Reconstruction compatibility contract

This branch restructures the plugin without changing the behavior of the
working `pob220` modernisation baseline.  A migration phase is complete only
when the gates below pass.

## Dataset contract

- The default bundle remains the provenance-bound `ocpn-climatology-2026.1`
  dataset.
- The manifest, checksums and product periods must be validated as one unit.
- A complete historical unversioned bundle may be read, but files from
  different generations must never be mixed.
- Missing data remains missing.  It is not converted to zero.
- Each variable retains its native geometry, mask, scale and units.
- Dateline wrapping, polar bounds, month wrapping and component-wise current
  interpolation retain the modernisation fixes.

## OpenCPN and UI contract

The main dialog, configuration notebook, timeline, All/Now modes, cursor
readouts, scalar overlays, current and wind arrows, isobars, numbers, wind
atlas, cyclone filters and GL/DC paths remain available.  Existing settings
keys continue to load and save.

Dialogs may edit state and request a canvas refresh.  They must not load data,
query raw storage or invoke a renderer directly.

## xWeatherRouting contract

The `CLIMATOLOGY` response retains the exact legacy pointer fields and
signatures from `ClimatologyAPI.h`:

- `ClimatologyDataPtr`
- `ClimatologyWindAtlasDataPtr`
- `ClimatologyCycloneTrackCrossingsPtr`

The latest xWeatherRouting performs main-thread preflight and then calls these
services from route workers under its invocation mutex.  Once advertised as
ready, callbacks must therefore read only a complete immutable snapshot and
must not touch dialogs, OpenGL state or other GUI objects.

The same three pointer declarations are used by standard WeatherRouting.
Release gating checks both `rgleason/weather_routing_pi` and
`pob220/xweather_routing_pi`; Climatology remains within their supported
0.10–1.6 version interval.  A second ready publication after the asynchronous
cyclone index completes lets both consumers replace the initially unavailable
cyclone callback state without restarting either plugin.

Typed Provider API V1 is additive.  It advertises all-years data only and
returns explicit status, units and direction conventions.

## Publication contract

Data is decoded into a private builder.  A snapshot is published atomically
only after all required validation has completed.  Until then, typed queries
return `STATUS_NOT_READY`, legacy data queries return `false`, and cyclone
queries return unavailable.  Publication sends a fresh `CLIMATOLOGY` message
so xWeatherRouting resets and repeats preflight.
