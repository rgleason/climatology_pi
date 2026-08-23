#pragma once

#include <vector>
#include <cstdint>
#include "UnifiedGrid.h"

// ============================================================================
// RenderCell – per-cell interpolated render data
// ============================================================================

struct RenderCell
{
    float    scalar = 0.0f;     // interpolated scalar value
    float    u      = 0.0f;     // interpolated U-component
    float    v      = 0.0f;     // interpolated V-component
    uint32_t rgba   = 0;        // final color (renderer fills this)
    uint8_t  symbol = 0;        // overlay symbol (renderer fills this)
};

// ============================================================================
// RenderGrid – modern unified render buffer
// ============================================================================
// Responsibilities:
//   • Hold interpolated scalar/u/v values for a given month
//   • Hold per-cell RGBA values (assigned by renderer)
//   • Hold per-cell overlay symbols (assigned by renderer)
//   • Provide geometry (rows/cols)
//   • Provide access to cells
//
// NOT responsible for:
//   • color maps
//   • transparency
//   • cyclone overlays
//   • wind atlas overlays
//   • wxWidgets
//   • rendering
//   • UI params
//
// RenderGrid is a pure data buffer consumed by the renderer.
//

class RenderGrid
{
public:
    RenderGrid();

    void Allocate(int r, int c);

    // Build interpolated scalar/u/v for a given fractional month
    void BuildFromUnifiedGrid(const UnifiedGrid& grid,
                              float month_float);

    // Accessors
    int Rows() const { return rows; }
    int Cols() const { return cols; }

    const std::vector<RenderCell>& Cells() const { return cells; }
    std::vector<RenderCell>& Cells() { return cells; }

private:
    int rows = 0;
    int cols = 0;

    std::vector<RenderCell> cells;

    void InterpolateMonth(const UnifiedGrid& grid,
                          float month_float);
};
