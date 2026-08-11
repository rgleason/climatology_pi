#pragma once
#include <vector>

// Raw NOAA U/V vector fields
// Used by legacy vector loader.

struct WindData
{
    int latitudes;
    int longitudes;

    std::vector<float> u;   // NOAA U-component grid
    std::vector<float> v;   // NOAA V-component grid

    WindData(int lats, int lons)
        : latitudes(lats),
          longitudes(lons),
          u(lats * lons),
          v(lats * lons)
    {}
};
