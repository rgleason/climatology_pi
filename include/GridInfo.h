#ifndef GRID_INFO_H_GUARD
#define GRID_INFO_H_GUARD

struct GridInfo
{
    int rows    = 0;     // number of latitude rows
    int cols    = 0;     // number of longitude columns

    double lat0 = 0.0;   // starting latitude (southmost)
    double lon0 = 0.0;   // starting longitude (westmost)

    double latStep = 0.0;  // delta latitude between rows
    double lonStep = 0.0;  // delta longitude between columns
};

#endif
