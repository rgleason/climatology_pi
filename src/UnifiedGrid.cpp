#include "UnifiedGrid.h"

// ============================================================================
// Full UnifiedGrid.cpp — Modern unified climatology model
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
    validMonths.assign(12, false);
    scalar.resize(12);
    u.resize(12);
    v.resize(12);
}

// ----------------------------------------------------------------------------
// Geometry
// ----------------------------------------------------------------------------

void UnifiedGrid::InitGeometry(int r, int c,
                               double la0, double lo0,
                               double laStep, double loStep)
{
    rows    = r;
    cols    = c;
    lat0    = la0;
    lon0    = lo0;
    latStep = laStep;
    lonStep = loStep;

    valid = true;
}

bool UnifiedGrid::IsValid(int month) const
{
    if (month < 0 || month >= 12)
        return false;
    return valid && validMonths[month];
}

// ----------------------------------------------------------------------------
// Month-major initialization
// ----------------------------------------------------------------------------

void UnifiedGrid::InitScalarMonth(int month)
{
    scalar[month].assign(rows * cols, 0.0f);
    validMonths[month] = true;
}

void UnifiedGrid::InitVectorMonth(int month)
{
    u[month].assign(rows * cols, 0.0f);
    v[month].assign(rows * cols, 0.0f);
    validMonths[month] = true;
}

// ----------------------------------------------------------------------------
// Geometry helpers
// ----------------------------------------------------------------------------

int UnifiedGrid::LatLonToIndex(double lat, double lon) const
{
    int r = int((lat - lat0) / latStep);
    int c = int((lon - lon0) / lonStep);

    if (r < 0 || r >= rows || c < 0 || c >= cols)
        return -1;

    return r * cols + c;
}

void UnifiedGrid::IndexToLatLon(int index, double& lat, double& lon) const
{
    int r = index / cols;
    int c = index % cols;

    lat = lat0 + r * latStep;
    lon = lon0 + c * lonStep;
}

// ----------------------------------------------------------------------------
// Normalization
// ----------------------------------------------------------------------------

float UnifiedGrid::NormalizeRawData(float value) const
{
    if (std::isnan(value))
        return 0.0f;

    // Simple linear normalization placeholder
    return value;
}

// ----------------------------------------------------------------------------
// Month slices
// ----------------------------------------------------------------------------

const std::vector<float>& UnifiedGrid::GetMonthSliceScalar(int month) const
{
    return scalar[month];
}

const std::vector<float>& UnifiedGrid::GetMonthSliceU(int month) const
{
    return u[month];
}

const std::vector<float>& UnifiedGrid::GetMonthSliceV(int month) const
{
    return v[month];
}
