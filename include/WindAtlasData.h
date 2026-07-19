#pragma once
#include <vector>
#include <cmath>

#include "ClimatologyCoord.h"

struct WindAtlasData
{
    struct WindPolar
    {
        wxUint8 gale;
        wxUint8 calm;
        wxUint8 directions[8];
        wxUint8 speeds[8];

        double Value(enum Coord coord, int dir_cnt);
    };

    WindAtlasData(int lats, int lons, int dirs, float dir_res, float spd_mul)
        : latitudes(lats),
          longitudes(lons),
          dir_cnt(dirs),
          direction_resolution(dir_res),
          speed_multiplier(spd_mul),
          data(new WindPolar[lats * lons])
    {}

    ~WindAtlasData() { delete [] data; }

    double InterpWind(enum Coord coord, double lat, double lon);

    WindPolar* GetPolar(double lat, double lon)
    {
        double latoff = 90.0 / latitudes;
        double lonoff = 180.0 / longitudes;

        int lati = round(latitudes * (.5 + (lat - latoff) / 180.0));
        int loni = round(longitudes * (lon - lonoff) / 360.0);

        if (lati < 0 || lati >= latitudes || loni < 0 || loni >= longitudes)
            return nullptr;

        WindPolar* polar = &data[lati * longitudes + loni];
        if (polar->gale == 255)
            return nullptr;

        return polar;
    }

    int latitudes;
    int longitudes;
    int dir_cnt;
    float direction_resolution;
    float speed_multiplier;

    WindPolar* data;
};
