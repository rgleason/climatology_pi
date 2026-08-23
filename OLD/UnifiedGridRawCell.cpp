#include "UnifiedGridRawCells.h"
#include <cmath>
#include <algorithm>

//  UnifiedGridRawCells
//  primitive cell-based grid
// This is the oldest, most primitive version:
// Represents one grid at one time
// Has no month indexing
// Has no multi-layer support
// Has no metadata
// Has no cyclone/wind atlas/ENSO
// Has no normalization logic
// Has no helper functions beyond basic cell access
// It is purely RAW, and should remain minimal aand untouched
//  kept around temporarily for reference and comparison.



UnifiedGrid::UnifiedGridRawCells()
{
    m_width  = 0;
    m_height = 0;
}

bool UnifiedGrid::Allocate(int w, int h)
{
    if (w <= 0 || h <= 0)
        return false;

    m_width  = w;
    m_height = h;
    m_cells.resize(w * h);

    return true;
}

GridCell& UnifiedGrid::At(int x, int y)
{
    return m_cells[y * m_width + x];
}

const GridCell& UnifiedGrid::At(int x, int y) const
{
    return m_cells[y * m_width + x];
}

void UnifiedGrid::Interpolate(const UnifiedGrid& a,
                              const UnifiedGrid& b,
                              double t)
{
    if (a.m_width != b.m_width ||
        a.m_height != b.m_height)
        return;

    Allocate(a.m_width, a.m_height);

    for (int y = 0; y < m_height; y++)
    {
        for (int x = 0; x < m_width; x++)
        {
            const GridCell& ca = a.At(x, y);
            const GridCell& cb = b.At(x, y);
            GridCell&       cc = At(x, y);

            cc.lat = ca.lat;
            cc.lon = ca.lon;

            cc.u = ca.u + (cb.u - ca.u) * t;
            cc.v = ca.v + (cb.v - ca.v) * t;
            cc.scalar = ca.scalar + (cb.scalar - ca.scalar) * t;
        }
    }
}
