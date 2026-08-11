#pragma once

#include <vector>
#include <cmath>

#include "CycloneStructs.h"

// ============================================================================
// Full UnifiedGrid.h — Header  - Modern unified climatology model
// ============================================================================

// Forward declarations

/*
struct CyclonePoint {
    double lat;
    double lon;
    double pressure;
    double wind;
};

struct CycloneTrack {
    std::vector<CyclonePoint> points;
    int month;     // 0–11
    int basin;     // enum CycloneBasin
};
*/




struct WindAtlasCell {
    float speed;
    float direction;
};

struct WindAtlasData {
    int rows = 0;
    int cols = 0;
    std::vector<WindAtlasCell> cells;
};

class UnifiedGrid
{
public:
    UnifiedGrid();

    // ---- Geometry ----
    int    rows;
    int    cols;
    double lat0;
    double lon0;
    double latStep;
    double lonStep;

    // ---- Month-major scalar/vector fields ----
    std::vector<std::vector<float>> scalar;
    std::vector<std::vector<float>> u;
    std::vector<std::vector<float>> v;

    // ---- Validity ----
    std::vector<bool> validMonths;

    bool IsValid(int month) const;

    // ---- Initialization ----
    void InitGeometry(int r, int c,
                      double la0, double lo0,
                      double laStep, double loStep);

    void InitScalarMonth(int month);
    void InitVectorMonth(int month);

    // ---- Geometry helpers ----
    int  LatLonToIndex(double lat, double lon) const;
    void IndexToLatLon(int index, double& lat, double& lon) const;

    // ---- Normalization ----
    float NormalizeRawData(float value) const;

    // ---- Month slice ----
    const std::vector<float>& GetMonthSliceScalar(int month) const;
    const std::vector<float>& GetMonthSliceU(int month) const;
    const std::vector<float>& GetMonthSliceV(int month) const;

    // ---- ENSO ----
    std::vector<float> enso_index;

    // ---- Wind Atlas ----
    WindAtlasData wind_atlas;

    // ---- Cyclone Tracks ----
    std::vector<CycloneTrack> cyclone_tracks;

private:
    bool valid;
};
