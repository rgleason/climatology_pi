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

    // ---- Metadata ----
    void InitGridMetadata(const std::vector<GridInfo>& info);

    // ---- Accessors ----
    bool HasMonth(int setting, int month) const;
    const UnifiedGrid& GetGrid(int setting, int month) const;

    // ---- Loaders ----
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

    // ---- Geometry getters ----
    int GetRows() const { return rows; }
    int GetCols() const { return cols; }

    double GetLat0() const { return lat0; }
    double GetLon0() const { return lon0; }

    double GetLatStep() const { return latStep; }
    double GetLonStep() const { return lonStep; }

    // ---- Raw data fields (UnifiedGrid copies these) ----
    int rows = 0;
    int cols = 0;
    double lat0 = 0.0;
    double lon0 = 0.0;
    double latStep = 0.0;
    double lonStep = 0.0;

    std::vector<std::vector<float>> scalar;
    std::vector<std::vector<float>> u;
    std::vector<std::vector<float>> v;

    std::vector<CycloneTrack> cyclone_tracks;
    std::vector<WindAtlasData::WindPolar> wind_atlas;

    float enso_index[12] = {0};

private:
    // ---- Raw NOAA grids (database) ----
    std::array<std::array<UnifiedGrid, 12>, NUM_OVERLAYS> m_grids;

    // ---- Metadata ----
    std::vector<GridInfo> m_meta;
};

#endif // CLIMATOLOGY_DATA_MODEL_H