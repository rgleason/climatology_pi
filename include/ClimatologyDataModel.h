#ifndef CLIMATOLOGY_DATA_MODEL_H
#define CLIMATOLOGY_DATA_MODEL_H

#include <vector>
#include <array>
#include <cmath>
#include <wx/log.h>

#include "defs.h"        // for NUM_OVERLAYS
#include "GridInfo.h"    // your metadata struct

// ============================================================================
// UnifiedGrid – one scalar or vector field for one month
// ============================================================================

class UnifiedGrid
{
public:
    UnifiedGrid();

    void InitScalar(int rows, int cols,
                    double lat0, double lon0,
                    double latStep, double lonStep);

    void InitVector(int rows, int cols,
                    double lat0, double lon0,
                    double latStep, double lonStep);

    bool isValid() const;

    double valueAt(double lat, double lon) const;
    double uAt(double lat, double lon) const;
    double vAt(double lat, double lon) const;

private:
    int rows, cols;
    double lat0, lon0;
    double latStep, lonStep;

    bool loaded;
    bool isVector;

    std::vector<float> scalar;
    std::vector<float> u;
    std::vector<float> v;
};


// ============================================================================
// ClimatologyDataModel – unified NOAA model for all overlays
// ============================================================================

class ClimatologyDataModel
{
public:
    ClimatologyDataModel();

    void InitGridMetadata(const std::vector<GridInfo>& info);

    bool HasMonth(int setting, int month) const;
    const UnifiedGrid& GetGrid(int setting, int month) const;

    void LoadScalar(int setting, int month,
                    float* buf,
                    int rows, int cols,
                    double lat0, double lon0,
                    double latStep, double lonStep);

    void LoadVector(int setting, int month,
                    float* uBuf, float* vBuf,
                    int rows, int cols,
                    double lat0, double lon0,
                    double latStep, double lonStep);

private:
    std::array<std::array<UnifiedGrid, 12>, NUM_OVERLAYS> m_grids;
    std::vector<GridInfo> m_meta;
};

#endif // CLIMATOLOGY_DATA_MODEL_H
