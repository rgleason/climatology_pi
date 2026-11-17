# Climatology 2.0.3 — historical cyclone query correction

Publish the filtered cyclone search index as immutable, value-owned data.
Queries can safely continue using the previous index while the UI builds and
publishes a replacement. Rendering uses its own UI-owned map and does not
insert empty cells during lookups. Filters for storm class, wind, pressure,
date range and ENSO remain in effect.

Correct intersections of separated parallel lines and degenerate points;
retain true intersections, endpoint touches and collinear overlaps. Normalize
longitude before spatial binning and use floor for negative coordinates.
Invalid coordinates, seasonal windows and unavailable data have explicit
failure results. A valid empty filtered index means no matching tracks.

Advertise safe background cyclone queries and revision notifications for
Weather Routing 1.36.6 / xWeatherRouting 2.3.4 optional report assessments.
Existing callback pointer signatures are unchanged.

Regression tests cover geometry, dateline/negative cells, year wrap,
read-only cache behavior and concurrent snapshot publication. Native Windows
and macOS builds remain to be qualified before publication.
