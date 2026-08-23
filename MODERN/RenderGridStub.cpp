#include "RenderGrid.h"
#include "UnifiedGrid.h"

// ============================================================================
// RenderGridStub – temporary no-op renderer
// ============================================================================
// This file exists ONLY to keep the build stable while Modern RenderGrid.cpp
// is being developed. All functions intentionally do nothing.
// ============================================================================

RenderGrid::RenderGrid()
{
    rows = 0;
    cols = 0;
}

void RenderGrid::Allocate(int r, int c)
{
    rows = r;
    cols = c;
    cells.resize(r * c);
}

void RenderGrid::BuildFromUnifiedGrid(const UnifiedGrid& grid,
                                      float month_float)
{
    // no-op
}

void RenderGrid::InterpolateMonth(const UnifiedGrid& grid,
                                  float month_float)
{
    // no-op
}
