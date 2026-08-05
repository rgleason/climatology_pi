#pragma once
#include <vector>

struct CurrentData
{
    int latitudes;
    int longitudes;

    std::vector<float> u;   // NOAA U-component grid
    std::vector<float> v;   // NOAA V-component grid

    CurrentData(int lats, int lons)
        : latitudes(lats),
          longitudes(lons),
          u(lats * lons),
          v(lats * lons)
    {}
};
