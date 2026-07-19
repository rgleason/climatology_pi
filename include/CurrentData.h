#pragma once
#include <vector>

struct CurrentData
{
    int latitudes;
    int longitudes;
    std::vector<float> u;
    std::vector<float> v;

    CurrentData(int lats, int lons)
        : latitudes(lats), longitudes(lons),
          u(lats * lons, 0.0f),
          v(lats * lons, 0.0f)
    {}
};
