#include "UnifiedGridLegacy.h"
#include <cmath>
#include <algorithm>


// ============================================================================
// UnifiedGridLegacy  implementation
// ============================================================================
// the single-layer, single-month grid
// the second-generation version:
// Represents one layer, one month
// Has grid geometry
// Has scalar or vector fields
// Has basic lat/lon lookup
// Has helper functions (valueAt, uAt, vAt)
//  It is legacy, but still more structured than UnifiedRawCells.


UnifiedGrid::UnifiedGridLegacy()
    : rows(0), cols(0),
      lat0(0), lon0(0),
      latStep(0), lonStep(0),
      loaded(false),
      isVector(false)
{
}

void UnifiedGrid::InitScalar(int r, int c,
                             double la0, double lo0,
                             double laStep, double loStep)
{
    rows = r;
    cols = c;
    lat0 = la0;
    lon0 = lo0;
    latStep = laStep;
    lonStep = loStep;

    isVector = false;
    loaded = true;

    scalar.resize(rows * cols, NAN);
}

void UnifiedGrid::InitVector(int r, int c,
                             double la0, double lo0,
                             double laStep, double loStep)
{
    rows = r;
    cols = c;
    lat0 = la0;
    lon0 = lo0;
    latStep = laStep;
    lonStep = loStep;

    isVector = true;
    loaded = true;

    u.resize(rows * cols, NAN);
    v.resize(rows * cols, NAN);
}

bool UnifiedGrid::isValid() const
{
    return loaded;
}

double UnifiedGrid::valueAt(double lat, double lon) const
{
    if (!loaded || isVector)
        return NAN;

    int yi = (int)((lat - lat0) / latStep);
    int xi = (int)((lon - lon0) / lonStep);

    if (yi < 0 || yi >= rows ||
        xi < 0 || xi >= cols)
        return NAN;

    return scalar[yi * cols + xi];
}

double UnifiedGrid::uAt(double lat, double lon) const
{
    if (!loaded || !isVector)
        return NAN;

    int yi = (int)((lat - lat0) / latStep);
    int xi = (int)((lon - lon0) / lonStep);

    if (yi < 0 || yi >= rows ||
        xi < 0 || xi >= cols)
        return NAN;

    return u[yi * cols + xi];
}

double UnifiedGrid::vAt(double lat, double lon) const
{
    if (!loaded || !isVector)
        return NAN;

    int yi = (int)((lat - lat0) / latStep);
    int xi = (int)((lon - lon0) / lonStep);

    if (yi < 0 || yi >= rows ||
        xi < 0 || xi >= cols)
        return NAN;

    return v[yi * cols + xi];
}
