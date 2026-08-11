#pragma once
#include <wx/colour.h>
#include <vector>

struct ClimatologyColorGrid {
    int lon_count = 0;
    int lat_count = 0;
    std::vector<wxColour> data;

    void allocate(int nx, int ny) {
        lon_count = nx;
        lat_count = ny;
        data.assign(nx * ny, wxColour(0, 0, 0));
    }

    wxColour get(int ix, int iy) const {
        return data[iy * lon_count + ix];
    }

    void set(int ix, int iy, const wxColour& c) {
        data[iy * lon_count + ix] = c;
    }
};
