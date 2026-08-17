# Legacy Climatology dataset specification

This specification describes the released bundle at data repository commit
`0061203...` as consumed by the runtime. Unless stated otherwise, multi-byte
fields in the released files are little-endian only by accident of the machine
which generated them; the old C++ code used native byte order. Every binary is
gzip-compressed for packaging. There is no common container, checksum, schema
identifier, period, or provenance header.

Array ordering is C row-major: the rightmost index changes fastest.

## Wind atlas — `wind01.gz` … `wind12.gz`

The actual packaged schema has a 14-byte header of seven `uint16` values:

```text
magic=0xfefe, latitudes=360, longitudes=720, sectors=8,
direction_resolution=50, speed_numerator=1, speed_denominator=1
```

The grid is 0.5 degrees. Runtime index mathematics places rows south-to-north
at centres -89.75, -89.25, …, +89.75 and columns at +0.25, +0.75, … modulo
360. Sectors are 45 degrees wide and describe meteorological direction **from**
which the wind comes: sector 0 north, then clockwise NE, E, ….

Payload passes:

1. one byte for every cell: 201–255 missing; 0–99 calm percentage; 100–200
   represents gale percentage `byte-100`;
2. eight sector-frequency passes, one byte only for each non-missing cell;
3. eight mean-speed passes, one byte only where that cell's corresponding
   sector frequency is nonzero.

Frequency probability is byte/50. Mean speed is
`byte / (speed_numerator/speed_denominator)` knots. Calm is <=3 kn and gale is
>=34 kn in the checked-in generator. It counts all valid samples in directional
frequency, including calm observations. Directions at or below 2.5% were
deleted and the survivors renormalised. A cell is discarded if its sample count
is below half the maximum count anywhere in that month's input.

Critical loss: only calm **or** gale is retained. Exact 100% calm collides with
the gale discriminator. The generator in the repository writes a different
five-value header (`0xfeff`) and contains compile/logic errors; therefore the
runtime reader and packaged files are authoritative for `0xfefe`.

## Ocean current — `current01.gz` … `current12.gz`

Six-byte header, three `uint16` values:

```text
latitudes=481, longitudes=1080, multiplier=20
```

Then a complete signed-byte U plane followed by a V plane. `-128` is missing;
other values decode to knots as `byte/multiplier`, a 0.05 kn step. U is eastward
and V northward. Rows run from 80°N to 80°S at one-third-degree spacing;
columns run eastward from 0° at one-third-degree spacing and wrap at 360°.

The old OSCAR generator averages data labelled 1993 through 2012, removes the
duplicate longitude column, and rotates source longitudes. It independently
sets components smaller than 0.2 m/s to zero, a serious weak-current bias.

## Scalar fields

These files have no header.

| File | Raw shape/type | Missing | Runtime decode | Documented old source |
|---|---|---:|---|---|
| `sealevelpressure.gz` | 12x90x180 int16 | 32767 | `raw*0.01 + 1000` hPa | COADS 1950–1979 |
| `seasurfacetemperature.gz` | 12x180x360 int8 | -128 | writer expands `raw*200`; query `*0.001 + 15`, hence `15 + raw*0.2` °C | COADS 1960–1979 |
| `airtemperature.gz` | 12x90x180 int8 | -128 | `raw/3` °C | NCEP marine record, exact years not recorded |
| `cloud.gz` | 12x90x180 uint8 | 255 | `raw*0.5/4` oktas equivalent (runtime reports percent-like scale) | COADS 1950–1979 |
| `precipitation.gz` | 12x72x144 uint8 | 255 | `raw*0.2` mm/day | CMAP record, exact years not recorded |
| `relativehumidity.gz` | 12x180x360 uint8 | 255 | `raw/2` percent | ICOADS record, exact years not recorded |
| `lightning.gz` | 12x180x360 uint8 | none | nonlinear encoded index | NASA LIS/OTD HRMC v2.3 (2013 product) |
| `seadepth.gz` | 180x360 int8 | -128 | ordinal index into 40 depth bins from 0 to 10,000 m | WOA98 land/sea depth mask |

Most scalar rows run north-to-south. Longitude offsets are embedded in each
runtime query and are inconsistent (for example SLP 1.5°, air/cloud 0.5°, and
precipitation 2° before division by grid spacing). Tests freeze current
semantics before any correction; new encoders generate coordinates explicitly.

Lightning encoding averages four native half-degree cells into one-degree
cells, flips latitude, rotates longitude 180°, then stores approximately
`380 * value^(1/4)`, clamped to 255. The inverse/physical unit is not implemented
in the runtime, which displays this index rather than flashes/km²/year.

## Tropical cyclone tracks — `cyclone-<basin>.gz`

There is no file header. Each track is:

```text
uint16 initial_year
repeat point:
    int8 state                 '*', S, E, W, L, D or X
    uint8 dayhour              day*4 + UTC_hour/6
    uint8 month                1..12
    int16 latitude_tenths
    int16 longitude_tenths
    uint8 maximum_wind_knots
    uint16 pressure_hPa
int8 -128 terminator
```

Year increments when month decreases. Southern files historically store a
positive latitude magnitude and the reader negates it. Longitude is commonly
west-negative and may span roughly -360..+15. A runtime `CycloneState` is a
segment from one point to the next and associates the first point's wind and
pressure with that segment.

The source is described as Unisys best tracks, but no download version or
checksums survive. The current generator says 1968 onward while the actual
Atlantic bundle begins in 1851. Wind averaging convention is absent.

## ENSO — `elnino_years.txt`

Space-separated text with one header row, year, then 12 monthly floating-point
values or non-numeric missing markers. Runtime thresholds are >=+0.5 El Niño,
<=-0.5 La Niña, otherwise neutral. The file does not carry its index name,
base period, source version, or access date.

## Interpolation semantics

The plugin linearly interpolates between monthly fields, treating each value as
located at its calendar month's midpoint. Before the midpoint it blends the
previous month; after it blends the next. Only integer day is used. Direction
uses shortest-arc angular interpolation across 0/360. Spatial interpolation is
bilinear with longitude wrap; NaN behaviour and coastal fill differ by data
type. The historical implementation has unsafe polar/current boundary cases;
modern tests define the corrected behaviour.

## Executable specification

`tools/climatology_pipeline/climatology_pipeline/legacy.py` is the strict
decoder/encoder. `climatology-inspect` reports dimensions, ranges, counts, and
checksums. Tests decode every released schema and use independently assembled
wire payloads to ensure round trips are not merely code testing itself.
