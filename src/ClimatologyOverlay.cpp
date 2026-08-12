#include "ClimatologyOverlayFactory.h"
#include <cstring>

// ============================================================================
// ClimatologyOverlay – storage for raw NOAA scalar fields
// ============================================================================

ClimatologyOverlay::~ClimatologyOverlay()
{
    if (m_data)
        delete [] m_data;
    m_data = nullptr;
}

double ClimatologyOverlay::value(int ix, int iy) const
{
    if (!m_valid)
        return 0.0;

    if (ix < 0 || ix >= lon_count ||
        iy < 0 || iy >= lat_count)
        return 0.0;

    int idx = iy * lon_count + ix;
    return static_cast<double>(m_data[idx]);
}

void ClimatologyOverlay::setValue(int ix, int iy, double v)
{
    if (!m_valid)
        return;

    if (ix < 0 || ix >= lon_count ||
        iy < 0 || iy >= lat_count)
        return;

    int idx = iy * lon_count + ix;
    m_data[idx] = static_cast<unsigned char>(v);
}
