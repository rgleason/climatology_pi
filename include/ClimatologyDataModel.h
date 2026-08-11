#ifndef CLIMATOLOGY_DATA_MODEL_H
#define CLIMATOLOGY_DATA_MODEL_H

#pragma once

#include <array>
#include <vector>
#include "UnifiedGrid.h"
#include "GridInfo.h"
#include "ClimatologyEnums.h"

#include "defs.h"        // for NUM_OVERLAYS

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
