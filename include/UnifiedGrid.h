// ============================================================================
// Full UnifiedGrid.h — Header  - Modern unified climatology model
// ============================================================================

#pragma once

#include <wx/wxprec.h>
#ifndef WX_PRECOMP
#include <wx/wx.h>
#endif

#include <vector>
#include <cmath>

#include "CycloneStructs.h"
#include "WindAtlasData.h"


class ClimatologyDataModel;


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
	void NormalizeRawData();

    // ---- Month slice ----
    const std::vector<float>& GetMonthSliceScalar(int month) const;
    const std::vector<float>& GetMonthSliceU(int month) const;
    const std::vector<float>& GetMonthSliceV(int month) const;

    // ---- ENSO ----
	float enso_index[12];


    // ---- Wind Atlas ----
	std::vector<WindAtlasData::WindPolar> wind_atlas;
    
    // ---- Cyclone Tracks ----
    std::vector<CycloneTrack> cyclone_tracks;
	void LoadFromDataModel(const ClimatologyDataModel& dm);
	int GetStormCategory(float pressure, float wind) const;
	

private:
    bool valid;
};
