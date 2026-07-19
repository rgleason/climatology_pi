#pragma once

#include <vector>
#include <cmath>
#include <wx/log.h>


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
