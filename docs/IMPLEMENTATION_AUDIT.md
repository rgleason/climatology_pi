# Sean and Rick implementation audit

## Compared states

| Repository | Branch/commit | Plugin/API | Observation |
|---|---|---|---|
| `seandepagnier/climatology_pi` | `master` at `1e9f04072a11f227e58fde8c45efc8e1348f6597` (2023-12-24) | 1.6.30.5 / API 1.17 | Upstream origin; last commit merges Rick's then-current frontend work. |
| `rgleason/climatology_pi` | `master` at `0af2303f932f8aead5f029b3ad083e08d4ce51e4` (2026-05-16) | 1.6.37.0 / API 1.18 | Maintained frontend/build fork and chosen baseline. |

Downloaded source archive SHA-256 values:

* Rick: `a27fe4db3387aace59a2696abae510d196f2a3c78c03eada79a66da35989fc53`
* Sean: `7cc80982a4ed7a8e4167da18cad387196e7aa7a312c362ee19f91174ce8b11db`
* exact legacy data commit `006120320bde2c1ad8da10a911cdf2b0f3bffe0d` archive:
  `02b441adb6fcd450007c2966b775b96ee093b0b4b3ca53761bb8f4f1f27f79d1`

The local baseline is an archive import, not a claim to reproduce upstream Git
history. Exact upstream identities above are authoritative.

## Material differences

Rick's fork contains the modern plugin build/CircleCI frontend, OpenCPN 5.8+
catalog configuration, API 1.18, wxWidgets 3.2.8 settings, expanded manual, and
current packaging fixes. Compared with Sean's selected commit, runtime changes
are small:

* current arrays changed from unchecked raw `new[]` to `std::vector` with
  basic dimension/allocation guards;
* displayed version includes all four components;
* data lookup was changed to `GetPluginDataDir("climatology_pi")/data`;
* icon path construction was cleaned up;
* API base changed from `opencpn_plugin_117` to `_118`.

The data generators are effectively the same old toolchain in both. Rick's
fork is the better OpenCPN build baseline, but it is not a scientific dataset
modernisation.

## Exported interface

On `CLIMATOLOGY_REQUEST`, the plugin sends a `CLIMATOLOGY` JSON message holding
major/minor version and three process-local function pointers formatted with
`%p`:

```text
ClimatologyDataPtr
  bool(int setting, wxDateTime, lat, lon, direction, speed)

ClimatologyWindAtlasDataPtr
  bool(wxDateTime, lat, lon, count=8, directions[8], speeds[8], gale, calm)

ClimatologyCycloneTrackCrossingsPtr
  int(lat1, lon1, lat2, lon2, wxDateTime, dayrange)
```

Weather Routing accepts Climatology major/minor 0.10 through 1.6 and parses the
pointers directly. Its declarations use `const wxDateTime&`; the provider uses
non-const reference. This is an ABI-fragile signature mismatch even where the
calling convention happens to be identical. The modernised provider will make
the signatures exactly match without changing the message contract.

The pointer design is process-local and cannot be made thread-safe merely by
changing the dataset. xWeatherRouting already guards preparation/use on the
OpenCPN main thread. A future typed broker API may be desirable, but is outside
this compatibility-first release.

## Confirmed or strongly indicated defects

### Readers and memory safety

* scalar interpolation reads outside arrays at polar boundary inputs;
* wind interpolation can index outside latitude bounds and asserts when a
  valid cell's sector total is zero;
* current interpolation uses an unexplained hard-coded ±80-degree mapping,
  fails to guard all longitude/latitude indices, and can access row 481 at the
  southern boundary;
* current monthly averaging adds NaNs and divides them instead of averaging
  only valid components;
* most file dimensions and multipliers are insufficiently bounded; wind
  allocation multiplication is unchecked;
* scalar readers put arrays up to 777,600 bytes on the stack;
* native-endian integers make files non-portable;
* readers generally accept trailing data and do not distinguish decompressor
  errors from clean EOF;
* cyclone corruption cleanup deletes the track object but leaks states already
  allocated into it;
* `ReadElNinoYears` uses unchecked `strtok`, so malformed/short rows can be
  misparsed;
* download/path lookup tries the same path twice in Rick's fork, losing Sean's
  separate user-data fallback;
* the failed-download modal is shown twice;
* downloaded files use a GitHub `blob` URL rather than a versioned immutable
  asset and have no checksum/provenance validation.

### Numerical semantics

* vector direction interpolation handles 0/360 wrapping, but scalar NaNs are
  propagated even if one neighbour is usable; missing-boundary policy is
  undocumented and inconsistent with the wind-atlas fallback;
* time interpolation is anchored at calendar-month midpoints but ignores time
  of day and uses only integer day; this creates small discontinuities and is
  not documented;
* annual wind speeds are averaged per month without frequency weighting;
* the current generator zeros every component whose absolute value is below
  0.2 m/s, destroying weak currents before vector calculation;
* current year-to-month assignment uses floating `modf(years[y])*12`, vulnerable
  to boundary rounding;
* legacy wind storage discards either calm or gale probability in every cell;
* rare wind directions <=2.5% are deleted and remaining probabilities are
  renormalised, hiding tail risk;
* wind generation uses large stack arrays, unchecked reads, a const buffer
  modified by `strncat`, references a nonexistent struct member, and contains a
  stray semicolon in extended statistics. The checked-in generator cannot be
  treated as a reproducible build of the packaged `0xfefe` files.

### Cyclone inconsistency

`gencyclonedata.cpp` declares a 1968 cutoff, but the released Atlantic file
starts in 1851. The runtime historically clamps years below 1972 only under
MSVC, so the same data behaves differently by platform. The old pressure parser
also duplicates one source character (`s[15]`) and the binary format does not
record wind averaging convention or source agency.

## Open issue review

Rick's tracker still records requests to improve cyclone load speed, data
download UX, DPI/OpenGL handling, and data management. Closed issues document
past current-direction regression, fresh-install warnings, crashes, and data
path failures. Sean's tracker contains historical date-change crashes,
download corruption, data packaging, and Weather Routing integration issues.
These reports corroborate the build/path/lifecycle fragility but do not provide
a scientific provenance for the packaged data.

## Synthesis choice

Use Rick's API-1.18/build baseline; retain Sean's stable three-pointer consumer
contract; replace neither with a blind merge. Add strict codecs, tests,
provenance, modern generation, and focused runtime fixes. Keep scientific/data
commits separable from UI/build maintenance.
