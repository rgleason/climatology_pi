#include "UnifiedGrid.h"

// ============================================================================
// Simplified UnifiedGrid.cpp
// Matches the simplified UnifiedGrid.h used for structural validation.
// ============================================================================

UnifiedGrid::UnifiedGrid()
    : rows(0)
    , cols(0)
    , lat0(0.0)
    , lon0(0.0)
    , latStep(0.0)
    , lonStep(0.0)
    , valid(false)
{
}

void UnifiedGrid::InitScalar(int r, int c,
                             double la0, double lo0,
                             double laStep, double loStep)
{
    rows    = r;
    cols    = c;
    lat0    = la0;
    lon0    = lo0;
    latStep = laStep;
    lonStep = loStep;

    scalar.assign(r * c, 0.0f);
    u.clear();
    v.clear();

    valid = true;
}

void UnifiedGrid::InitVector(int r, int c,
                             double la0, double lo0,
                             double laStep, double loStep)
{
    rows    = r;
    cols    = c;
    lat0    = la0;
    lon0    = lo0;
    latStep = laStep;
    lonStep = loStep;

    scalar.clear();
    u.assign(r * c, 0.0f);
    v.assign(r * c, 0.0f);

    valid = true;
}
