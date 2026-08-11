#ifndef _UNIFIED_GRID_RAW_CELLS_H_
#define _UNIFIED_GRID_RAW_CELLS_H_

#include <vector>
#include "GridInfo.h"
#include "UnifiedGrid.h"

struct GridCell
{
    double lat;
    double lon;
    double u;
    double v;
    double scalar;
};

class UnifiedGridRawCell
{
public:
    UnifiedGridRawCell();

    bool Allocate(int w, int h);

    GridCell& At(int x, int y);
    const GridCell& At(int x, int y) const;

    void Interpolate(const UnifiedGrid& a,
                     const UnifiedGrid& b,
                     double t);

private:
    int m_width;
    int m_height;
    std::vector<GridCell> m_cells;
};

#endif

