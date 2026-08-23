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
    // no state
}

RenderGrid::~RenderGrid()
{
    // no state
}

void RenderGrid::BuildFromUnifiedGrid(const UnifiedGrid& grid)
{
    // no-op
}

void RenderGrid::InterpolateMonth(int month)
{
    // no-op
}

void RenderGrid::ApplyColorMap()
{
    // no-op
}

void RenderGrid::ApplyTransparency(float alpha)
{
    // no-op
}

void RenderGrid::ApplyCycloneOverlay()
{
    // no-op
}

void RenderGrid::ApplyWindAtlasOverlay()
{
    // no-op
}

bool RenderGrid::IsValid() const
{
    return true;    // always valid for stub
}
