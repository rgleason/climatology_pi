# Climatology typed provider API V1

## Purpose

`ClimatologyProviderAPI.h` is an additive, versioned query boundary for
Climatology consumers. It supplies explicit variables, units, vector direction
conventions, validity status and room for sample/uncertainty metadata without
exposing wxWidgets, JSON, STL containers or implementation objects.

The legacy functions in `ClimatologyAPI.h` remain unchanged. Current Weather
Routing releases continue to consume those three pointers, so installing this
version does not change their data path or numerical results.

## Discovery

The existing `CLIMATOLOGY` plugin message now includes:

```json
{
  "ClimatologyQueryAPIVersion": 1,
  "ClimatologyQueryV1Ptr": "0x...",
  "ClimatologyQueryCapabilities": {
    "scenarios": ["all-years"],
    "variables": [
      "wind-10m-distribution",
      "surface-current-vector",
      "sea-level-pressure",
      "sea-surface-temperature",
      "air-temperature",
      "cloud-cover",
      "precipitation",
      "relative-humidity",
      "lightning",
      "sea-depth"
    ]
  }
}
```

The pointer is process-local, exactly like the legacy pointers. A consumer must
discard it when Climatology advertises an invalid/zero pointer or unloads.

## Contract

Consumers initialize requests and results with `MakeQueryV1()` and
`MakeResultV1()`. `struct_size` permits compatible extension, while
`api_version` prevents accidental interpretation of a different layout.

All coordinates are WGS84 decimal degrees. Time is UTC Unix seconds; the
current monthly dataset uses the UTC month/day only. Currents are eastward and
northward components in knots and a true-north **to** direction. Wind is the
native eight-sector distribution, with true-north meteorological **from**
directions, conditional sector speeds in knots, and calm/gale probabilities.
Scalar units are returned explicitly.

Unknown or masked data returns `STATUS_UNKNOWN`; it is never converted to
zero. Unsupported variables, versions and scenarios have distinct statuses.
The callback can return `STATUS_NOT_READY` while the runtime database loads.

## Scenario policy

The wire enum reserves Neutral, El Niño and La Niña identifiers so future
research packs need not invent a second API. The production V1 provider
advertises and accepts **only `SCENARIO_ALL_YEARS`**. Every ENSO scenario query
returns `STATUS_UNSUPPORTED_SCENARIO`.

This is deliberate. The 2026 route-value study found one promising
southwest-Pacific seasonal signal, but not enough globally robust,
out-of-sample route skill to justify production regime selection. Adding enum
values is not a claim that conditioned data exists and does not change the UI.

## Compatibility and threading

V1 is read-only. It adapts the already-loaded classic monthly fields and does
not change global Climatology settings. Calls must follow the same OpenCPN main
thread assumptions as the legacy provider until a later snapshot/cache layer
documents otherwise. No consumer should retain internal pointers or infer
capability from the plugin version alone.

Before a future enhanced pack is enabled, it must add sample counts,
independent-event counts, uncertainty and masked-state metadata; preserve the
all-years result; and pass blinded, complete-event Weather Routing tests.
