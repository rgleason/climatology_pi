#include "ClimatologyDataModel.h"
#include <wx/log.h>

// ============================================================================
// Modern ClimatologyDataModel
// RAW NOAA → UnifiedGrid loader
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


/*
bool ClimatologyDataModel::HasMonth(int setting, int month) const
{
    if (setting < 0 || setting >= NUM_OVERLAYS)
        return false;
    if (month < 0 || month >= 12)
        return false;

    return m_grids[setting][month].IsValid();
}
*/

bool ClimatologyDataModel::HasMonth(int setting, int month) const
{
    if (setting < 0 || setting >= NUM_OVERLAYS)
        return false;
    if (month < 0 || month >= 12)
        return false;

    // Simplified: UnifiedGrid has no month-major validity
    return true;
}


const UnifiedGrid& ClimatologyDataModel::GetGrid(int setting, int month) const
{
    return m_grids[setting][month];
}

// ============================================================================
// LoadScalar — RAW → UnifiedGrid (scalar-only overlay)
// ============================================================================

/*
void ClimatologyDataModel::LoadScalar(int setting, int month,
                                      float* buf,
                                      int rows, int cols,
                                      double lat0, double lon0,
                                      double latStep, double lonStep)
{
    UnifiedGrid& g = m_grids[setting][month];

    // Initialize geometry
    g.InitGeometry(rows, cols, lat0, lon0, latStep, lonStep);

    // Allocate month-major storage
    g.scalar.resize(12);
    g.scalar[month].resize(rows * cols);

    // Copy raw data
    for (int i = 0; i < rows * cols; i++)
        g.scalar[month][i] = buf[i];

    // Mark month valid
    g.validMonths.resize(12);
    g.validMonths[month] = true;
}
*/

void ClimatologyDataModel::LoadScalar(int setting, int month,
                                      float* buf,
                                      int rows, int cols,
                                      double lat0, double lon0,
                                      double latStep, double lonStep)
{
    UnifiedGrid& g = m_grids[setting][month];

    // Use simplified geometry initializer
    g.InitScalar(rows, cols, lat0, lon0, latStep, lonStep);

    // Flat storage (no month-major)
    g.scalar.assign(rows * cols, 0.0f);

    for (int i = 0; i < rows * cols; i++)
        g.scalar[i] = buf[i];
}


// ============================================================================
// LoadVector — RAW → UnifiedGrid (u/v vector overlay)
// ============================================================================

/*
void ClimatologyDataModel::LoadVector(int setting, int month,
                                      float* uBuf, float* vBuf,
                                      int rows, int cols,
                                      double lat0, double lon0,
                                      double latStep, double lonStep)
{
    UnifiedGrid& g = m_grids[setting][month];

    // Initialize geometry
    g.InitGeometry(rows, cols, lat0, lon0, latStep, lonStep);

    // Allocate month-major storage
    g.u.resize(12);
    g.v.resize(12);

    g.u[month].resize(rows * cols);
    g.v[month].resize(rows * cols);

    // Copy raw data
    for (int i = 0; i < rows * cols; i++)
    {
        g.u[month][i] = uBuf[i];
        g.v[month][i] = vBuf[i];
    }

    // Mark month valid
    g.validMonths.resize(12);
    g.validMonths[month] = true;
}
*/

void ClimatologyDataModel::LoadVector(int setting, int month,
                                      float* uBuf, float* vBuf,
                                      int rows, int cols,
                                      double lat0, double lon0,
                                      double latStep, double lonStep)
{
    UnifiedGrid& g = m_grids[setting][month];

    // Use simplified geometry initializer
    g.InitVector(rows, cols, lat0, lon0, latStep, lonStep);

    // Flat storage (no month-major)
    g.u.assign(rows * cols, 0.0f);
    g.v.assign(rows * cols, 0.0f);

    for (int i = 0; i < rows * cols; i++)
    {
        g.u[i] = uBuf[i];
        g.v[i] = vBuf[i];
    }
}
