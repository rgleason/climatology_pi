#ifndef CYCLONE_FILTER_PARAMS_H
#define CYCLONE_FILTER_PARAMS_H

#include <wx/datetime.h>

// ============================================================================
// CycloneFilterParams
// Data filtering rules for cyclone tracks.
// NOT used for rendering (CycloneParams handles appearance).
// Used by the cyclone renderer for visual styling ONLY.
// Not used by ClimatologyRenderParams.h
// Used by the cyclone loader and filter engine.
// ============================================================================
struct CycloneFilterParams
{
    // -----------------------------------------------------------------------
    // Date range filtering (refactored model)
    // -----------------------------------------------------------------------
    wxDateTime startDate;     // inclusive
    wxDateTime endDate;       // inclusive

    // -----------------------------------------------------------------------
    // Intensity filtering
    // -----------------------------------------------------------------------
    double minWind     = 0.0;     // knots
    double maxPressure = 2000.0;  // hPa

    // -----------------------------------------------------------------------
    // Storm-type mask (CycloneState)
    // -----------------------------------------------------------------------
    int stateMask = 0xFFFFFFFF;

    // -----------------------------------------------------------------------
    // ENSO filtering (track-level)
    // -----------------------------------------------------------------------
    bool allowElNino       = true;
    bool allowLaNina       = true;
    bool allowNeutral      = true;
    bool allowNotAvailable = true;

    // -----------------------------------------------------------------------
    // Basin filtering (track-level)
    // -----------------------------------------------------------------------
    bool allowEPA = true;
    bool allowWPA = true;
    bool allowSPA = true;
    bool allowATL = true;
    bool allowNIO = true;
    bool allowSHE = true;
};

#endif // CYCLONE_FILTER_PARAMS_H
