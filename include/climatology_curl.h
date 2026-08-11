#ifndef _UNIFIED_GRID_H_
#define _UNIFIED_GRID_H_

#include <vector>

struct GridCell
{
    double lat;
    double lon;
    double u;
    double v;
    double scalar;
};

class UnifiedGrid
{
public:
    UnifiedGrid();

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
