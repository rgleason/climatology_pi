#pragma once

#include <wx/wxprec.h>
#ifndef WX_PRECOMP
#include <wx/wx.h>
#endif

#include <vector>
#include <cstdint>
#include "UnifiedGrid.h"
#include "ClimatologyRenderParams.h"
#include "StandardDisplayParams.h"
#include "WindAtlasParams.h"
#include "CycloneParams.h"
#include "CycloneFilterParams.h"
#include "UnifiedGrid.h"

class ClimatologyColorGrid;


struct RenderCell
{
    float    scalar_interp = 0.0f;
    float    u_interp      = 0.0f;
    float    v_interp      = 0.0f;
    uint32_t rgba          = 0;
    uint8_t  symbol_id     = 0;
};

class RenderGrid
{
public:
    int rows;
    int cols;
    std::vector<RenderCell> cells;

    RenderGrid();

    void Allocate(int r, int c);

    void BuildFromUnifiedGrid(const UnifiedGrid& grid,
                              const ClimatologyRenderParams& rparams,
                              const StandardDisplayParams& dparams,
                              const WindAtlasParams& wparams,
                              const CycloneParams& cparams,
                              const CycloneFilterParams& fparams,
                              float month_float);
							  
	void ToColorGrid(ClimatologyColorGrid& out) const;

private:
    void InterpolateMonth(const UnifiedGrid& grid,
                          float month_float);

    void ApplyColorMap(const ClimatologyRenderParams& rparams);
    void ApplyTransparency(const ClimatologyRenderParams& rparams);

    void ApplyCycloneOverlay(const UnifiedGrid& grid,
                             const CycloneParams& cparams,
                             const CycloneFilterParams& fparams,
                             float month_float);

    void ApplyWindAtlasOverlay(const UnifiedGrid& grid,
                               const WindAtlasParams& wparams);
};
