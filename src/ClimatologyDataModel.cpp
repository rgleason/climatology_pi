#include "ClimatologyDataModel.h"
#include <wx/log.h>


// ============================================================================
// UnifiedGrid implementation
// ============================================================================

UnifiedGrid::UnifiedGrid()
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


// ============================================================================
// ClimatologyDataModel implementation
// ============================================================================

ClimatologyDataModel::ClimatologyDataModel()
{
    for (int s = 0; s < NUM_OVERLAYS; s++)
        for (int m = 0; m < 12; m++)
            m_grids[s][m] = UnifiedGrid();
}

void ClimatologyDataModel::InitGridMetadata(const std::vector<GridInfo>& info)
{
    if ((int)info.size() != NUM_OVERLAYS)
    {
        wxLogError("ClimatologyDataModel: metadata size mismatch");
        return;
    }

    m_meta = info;
}

bool ClimatologyDataModel::HasMonth(int setting, int month) const
{
    if (setting < 0 || setting >= NUM_OVERLAYS)
        return false;
    if (month < 0 || month >= 12)
        return false;

    return m_grids[setting][month].isValid();
}

const UnifiedGrid& ClimatologyDataModel::GetGrid(int setting, int month) const
{
    return m_grids[setting][month];
}

void ClimatologyDataModel::LoadScalar(int setting, int month,
                                      float* buf,
                                      int rows, int cols,
                                      double lat0, double lon0,
                                      double latStep, double lonStep)
{
    UnifiedGrid& g = m_grids[setting][month];
    g.InitScalar(rows, cols, lat0, lon0, latStep, lonStep);

    for (int i = 0; i < rows * cols; i++)
        g.scalar[i] = buf[i];
}

void ClimatologyDataModel::LoadVector(int setting, int month,
                                      float* uBuf, float* vBuf,
                                      int rows, int cols,
                                      double lat0, double lon0,
                                      double latStep, double lonStep)
{
    UnifiedGrid& g = m_grids[setting][month];
    g.InitVector(rows, cols, lat0, lon0, latStep, lonStep);

    for (int i = 0; i < rows * cols; i++)
    {
        g.u[i] = uBuf[i];
        g.v[i] = vBuf[i];
    }
}
