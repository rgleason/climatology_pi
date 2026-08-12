#include "RenderGrid.h"
#include "ClimatologyColorGrid.h"
#include <wx/colour.h>
#include <cmath>

// ============================================================================
// RenderGrid – Stage‑4 empty renderer (compiling, aligned with UnifiedGrid)
// ============================================================================

RenderGrid::RenderGrid()
    : rows(0), cols(0)
{
}

void RenderGrid::Allocate(int r, int c)
{
    rows = r;
    cols = c;
    cells.assign(r * c, RenderCell());
}

// ----------------------------------------------------------------------------
// BuildFromUnifiedGrid (empty Stage‑4 version)
// ----------------------------------------------------------------------------

void RenderGrid::BuildFromUnifiedGrid(const UnifiedGrid& grid,
                                      const ClimatologyRenderParams& rparams,
                                      const StandardDisplayParams& dparams,
                                      const WindAtlasParams& wparams,
                                      const CycloneParams& cparams,
                                      const CycloneFilterParams& fparams,
                                      float month_float)
{
    int m0 = (int)std::floor(month_float);
    if (!grid.IsValid(m0))
        return;

    Allocate(grid.rows, grid.cols);

    // Stage‑4: do nothing else yet
}

// ----------------------------------------------------------------------------
// ToColorGrid – convert RenderGrid → ClimatologyColorGrid (wxColour)
// ----------------------------------------------------------------------------

void RenderGrid::ToColorGrid(ClimatologyColorGrid& out) const
{
    out.lon_count = cols;
    out.lat_count = rows;
    out.data.resize(rows * cols);

    for (int i = 0; i < rows * cols; ++i)
    {
        const RenderCell& rc = cells[i];

        wxColour c(
            (rc.rgba >> 24) & 0xFF,
            (rc.rgba >> 16) & 0xFF,
            (rc.rgba >> 8)  & 0xFF,
            (rc.rgba)       & 0xFF
        );

        out.data[i] = c;
    }
}

// ----------------------------------------------------------------------------
// InterpolateMonth – modern UnifiedGrid API
// ----------------------------------------------------------------------------

void RenderGrid::InterpolateMonth(const UnifiedGrid& grid,
                                  float month_float)
{
    int m0 = (int)std::floor(month_float);
    int m1 = (int)std::ceil(month_float);
    float w = month_float - m0;

    const auto& s0 = grid.GetMonthSliceScalar(m0);
    const auto& u0 = grid.GetMonthSliceU(m0);
    const auto& v0 = grid.GetMonthSliceV(m0);

    const auto& s1 = grid.GetMonthSliceScalar(m1);
    const auto& u1 = grid.GetMonthSliceU(m1);
    const auto& v1 = grid.GetMonthSliceV(m1);

    int slice = rows * cols;

    for (int i = 0; i < slice; i++)
    {
        cells[i].scalar_interp = (1 - w) * s0[i] + w * s1[i];
        cells[i].u_interp      = (1 - w) * u0[i] + w * u1[i];
        cells[i].v_interp      = (1 - w) * v0[i] + w * v1[i];
    }
}

// ----------------------------------------------------------------------------
// Empty Stage‑4 stubs
// ----------------------------------------------------------------------------

void RenderGrid::ApplyColorMap(const ClimatologyRenderParams& rparams)
{
    // Stage‑4: empty stub
}

void RenderGrid::ApplyTransparency(const ClimatologyRenderParams& rparams)
{
    // Stage‑4: empty stub
}

void RenderGrid::ApplyCycloneOverlay(const UnifiedGrid& grid,
                                     const CycloneParams& cparams,
                                     const CycloneFilterParams& fparams,
                                     float month_float)
{
    // Stage‑4: empty stub
}

void RenderGrid::ApplyWindAtlasOverlay(const UnifiedGrid& grid,
                                       const WindAtlasParams& wparams)
{
    // Stage‑4: empty stub
}
