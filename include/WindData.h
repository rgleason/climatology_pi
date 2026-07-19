#pragma once
#include <vector>


// Raw NOAA Grid (u/v components) 
// used by ReadNOAAFile and vector rendering.

struct WindData
{
    int latitudes;
    int longitudes;
    std::vector<float> u;
    std::vector<float> v;

    WindData(int lats, int lons)
        : latitudes(lats), longitudes(lons),
          u(lats * lons, 0.0f),
          v(lats * lons, 0.0f)
    {}
};
