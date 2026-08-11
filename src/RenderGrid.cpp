#if 0
// RenderGrid modern implementation (disabled during build stabilization)


#include "RenderGrid.h"
#include "ClimatologyColorGrid.h"


// ===========================================
// RenderGrid
// ============================================
//  modern frame buffer

RenderGrid::RenderGrid()
    : rows(0), cols(0)
{
}

void RenderGrid::Allocate(int r, int c)
{
    rows = r;
    cols = c;
    cells.resize(r * c);
}

// Stub to get working.  See below for full version.
void RenderGrid::BuildFromUnifiedGrid(const UnifiedGrid& grid,
                                      const ClimatologyRenderParams& rparams,
                                      const StandardDisplayParams& dparams,
                                      const WindAtlasParams& wparams,
                                      const CycloneParams& cparams,
                                      const CycloneFilterParams& fparams,
                                      float month_float)
{
    if (!grid.IsValid())
        return;

    Allocate(grid.rows, grid.cols);
    // TEMP: do nothing else
}

void RenderGrid::ToColorGrid(ClimatologyColorGrid& out) const
{
    out.rows = rows;
    out.cols = cols;
    out.data.resize(rows * cols);

    for (int i = 0; i < rows * cols; ++i)
    {
        const RenderCell& rc = cells[i];
        ClimatologyColorCell cc;

        cc.rgba      = rc.rgba;
        cc.symbol_id = rc.symbol_id;

        out.data[i] = cc;
    }
}

/*
void RenderGrid::BuildFromUnifiedGrid(const UnifiedGrid& grid,
                                      const ClimatologyRenderParams& rparams,
                                      const StandardDisplayParams& dparams,
                                      const WindAtlasParams& wparams,
                                      const CycloneParams& cparams,
                                      const CycloneFilterParams& fparams,
                                      float month_float)
{
    Allocate(grid.rows, grid.cols);

    InterpolateMonth(grid, month_float);
    ApplyColorMap(rparams);
    ApplyTransparency(rparams);

    ApplyCycloneOverlay(grid, cparams, fparams, month_float);
    ApplyWindAtlasOverlay(grid, wparams);
}
*/


void RenderGrid::InterpolateMonth(const UnifiedGrid& grid,
                                  float month_float)
{
    int m0 = (int)floor(month_float);
    int m1 = (int)ceil(month_float);
    float w = month_float - m0;

    int slice = rows * cols;

    std::vector<float> s0(slice), s1(slice);
    std::vector<float> u0(slice), u1(slice);
    std::vector<float> v0(slice), v1(slice);

    grid.GetMonthSlice(m0, s0, u0, v0);
    grid.GetMonthSlice(m1, s1, u1, v1);

    for (int i = 0; i < slice; i++)
    {
        cells[i].scalar_interp = (1-w)*s0[i] + w*s1[i];
        cells[i].u_interp      = (1-w)*u0[i] + w*u1[i];
        cells[i].v_interp      = (1-w)*v0[i] + w*v1[i];
    }
}


void RenderGrid::ApplyColorMap(const ClimatologyRenderParams& rparams)
{
    // Implement per-layer color mapping here.
}

void RenderGrid::ApplyTransparency(const ClimatologyRenderParams& rparams)
{
    // Implement alpha blending here.
}

void RenderGrid::ApplyCycloneOverlay(const UnifiedGrid& grid,
                                     const CycloneParams& cparams,
                                     const CycloneFilterParams& fparams,
                                     float month_float)
{
    // Implement cyclone overlay here.
}

void RenderGrid::ApplyWindAtlasOverlay(const UnifiedGrid& grid,
                                       const WindAtlasParams& wparams)
{
    // Implement wind atlas overlay here.
}


#endif