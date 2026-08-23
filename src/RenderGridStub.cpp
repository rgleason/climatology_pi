#include "RenderGrid.h"
#include "UnifiedGrid.h"
#include "ClimatologyColorGrid.h"

// ============================================================================
// Legacy RenderGridStub – absorbs ALL legacy RenderGrid API calls
// ============================================================================
// This stub keeps the build stable while UnifiedGrid is modernized.
// It matches the legacy RenderGrid.h EXACTLY.
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
                                      const ClimatologyRenderParams& rparams,
                                      const StandardDisplayParams& dparams,
                                      const WindAtlasParams& wparams,
                                      const CycloneParams& cparams,
                                      const CycloneFilterParams& fparams,
                                      float month_float)
{
    // no-op
}

void RenderGrid::ToColorGrid(ClimatologyColorGrid& out) const
{
    // no-op
}

void RenderGrid::InterpolateMonth(const UnifiedGrid& grid,
                                  float month_float)
{
    // no-op
}

void RenderGrid::ApplyColorMap(const ClimatologyRenderParams& rparams)
{
    // no-op
}

void RenderGrid::ApplyTransparency(const ClimatologyRenderParams& rparams)
{
    // no-op
}

void RenderGrid::ApplyCycloneOverlay(const UnifiedGrid& grid,
                                     const CycloneParams& cparams,
                                     const CycloneFilterParams& fparams,
                                     float month_float)
{
    // no-op
}

void RenderGrid::ApplyWindAtlasOverlay(const UnifiedGrid& grid,
                                       const WindAtlasParams& wparams)
{
    // no-op
}
