#ifndef CYCLONE_FILTER_PARAMS_H
#define CYCLONE_FILTER_PARAMS_H


#include <wx/wxprec.h>
#ifndef WX_PRECOMP
#include <wx/wx.h>
#endif

#include <set>
#include <wx/datetime.h>
#include "ClimatologyEnums.h"   // CycloneState, CycloneBasin, CycloneENSO

// ============================================================================
// CycloneFilterParams
// Logical filtering parameters for cyclone tracks/points.
// NOT used for rendering (CycloneParams handles appearance).
// ============================================================================
struct CycloneFilterParams
{
    // -----------------------------------------------------------------------
    // Basin filter
    // -----------------------------------------------------------------------
    std::set<CycloneBasin> basinFilter;   // EPA, WPA, SPA, ATL, NIO, SHE

    // -----------------------------------------------------------------------
    // ENSO filter
    // -----------------------------------------------------------------------
    std::set<CycloneENSO> ensoFilter;     // EL_NINO, LA_NINA, NEUTRAL, NA

    // -----------------------------------------------------------------------
    // Storm-type filter
    // -----------------------------------------------------------------------
    std::set<CycloneState> stateFilter;   // TROPICAL, SUBTROPICAL, etc.

    // -----------------------------------------------------------------------
    // Month filter (1–12). Empty = all months.
    // -----------------------------------------------------------------------
    std::set<int> monthFilter;

    // -----------------------------------------------------------------------
    // Year range filter
    // -----------------------------------------------------------------------
    int yearMin = 0;
    int yearMax = 9999;

    // -----------------------------------------------------------------------
    // Intensity filters
    // -----------------------------------------------------------------------
    double windMin     = 0.0;     // knots
    double windMax     = 999.0;   // knots

    double pressureMin = 0.0;     // hPa
    double pressureMax = 2000.0;  // hPa

    // -----------------------------------------------------------------------
    // Date range filter (optional, track-level)
    // -----------------------------------------------------------------------
    wxDateTime startDate;         // inclusive
    wxDateTime endDate;           // inclusive

    // -----------------------------------------------------------------------
    // Timeline window (used by CycloneTimelineInterpolation)
    // -----------------------------------------------------------------------
    int daySpan = 30;             // +/- days around center
};

#endif // CYCLONE_FILTER_PARAMS_H
