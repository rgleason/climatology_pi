#pragma once

#include <vector>
#include <cmath>
#include <wx/log.h>


// part of the legacy NOAA loader pipeline
// ClimatologyMonthData and ClimatologyCell are used by:
// LoadScalarField()
// LoadVectorField()
// ClimatologyOverlayFa/ctory::m_data[NUM_OVERLAYS][13]
// IsoBarMap
// Legacy interpolation routines
// Legacy rendering paths  NOT part of the Modern UnifiedGrid
// 3. It is used by code you have not migrated yet
//Legacy scalar/vector monthly grid
//Used by IsoBarMap, OverlayMapping, ManifestLoader, etc.
//  These are all valid, real, and used by the legacy pipeline.
// These are still needed until the Modern UnifiedGrid migration is complete.
// If you move or merge this header, you will break:


struct ClimatologyCell
{
    double scalar = NAN;
    double u      = NAN;
    double v      = NAN;
};

struct ClimatologyMonthData
{
    int latitudes  = 0;
    int longitudes = 0;

    double lat0    = 0.0;
    double lon0    = 0.0;

    double latStep = 0.0;
    double lonStep = 0.0;

    std::vector<ClimatologyCell> grid;

    double lookup(double lat, double lon) const;
    double lookupU(double lat, double lon) const;
    double lookupV(double lat, double lon) const;
};
