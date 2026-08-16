#ifndef CYCLONE_PARAMS_H
#define CYCLONE_PARAMS_H

#include <wx/wxprec.h>
#ifndef WX_PRECOMP
#include <wx/wx.h>
#endif


#include <wx/colour.h>

// ============================================================================
// CycloneParams
// Persistent cyclone appearance preferences.
// Saved in config as part of StandardDisplayParams.
// Used by the dialog and renderer for visual styling ONLY.
// NOT used for filtering (CycloneFilterParams handles filtering).
// ============================================================================

struct CycloneParams
{
    // -----------------------------------------------------------------------
    // Enable/disable cyclone overlay
    // -----------------------------------------------------------------------
    bool enabled = false;

    // -----------------------------------------------------------------------
    // Timeline controls (visual-only)
    // -----------------------------------------------------------------------
    int  centerDay        = 180;   // Day-of-year center (1–365)
    int  daySpan          = 30;    // +/- days around center
    int  currentMonth     = 0;     // 0–11
    bool allTimesCyclones = false;

    // -----------------------------------------------------------------------
    // ENSO visibility (visual-only)
    // -----------------------------------------------------------------------
    bool showElNino       = true;
    bool showLaNina       = true;
    bool showNeutral      = true;
    bool showNotAvailable = true;

    // -----------------------------------------------------------------------
    // Basin visibility (visual-only)
    // -----------------------------------------------------------------------
    bool showEPA = true;    // East Pacific
    bool showWPA = true;    // West Pacific
    bool showSPA = true;    // South Pacific
    bool showATL = true;    // Atlantic
    bool showNIO = true;    // North Indian Ocean
    bool showSHE = true;    // South Indian Ocean

    // -----------------------------------------------------------------------
    // Appearance (visual-only)
    // -----------------------------------------------------------------------
    double opacity   = 1.0;   // cyclone track opacity
    int    colorMode = 0;     // 0 = basin, 1 = ENSO, 2 = intensity, 3 = type

    // Track thickness controls
    float baseThickness   = 2.0f;
    float minThickness    = 1.0f;
    float maxThickness    = 6.0f;
    float intensityScale  = 1.0f;   // optional intensity → thickness scaling
    bool  useThicknessScaling = false; // <— needed by renderer

    // -----------------------------------------------------------------------
    // Visual + logical filters (UI-side)
    // -----------------------------------------------------------------------
    bool basinFilter[6] = { true, true, true, true, true, true }; // EPA,WPA,SPA,ATL,NIO,SHE
    int  categoryFilter = 0;     // 0 = all
    int  monthFilter    = -1;    // -1 = all
    int  yearMin        = 0;
    int  yearMax        = 9999;

    // -----------------------------------------------------------------------
    // Color override
    // -----------------------------------------------------------------------
    bool     useFixedColor = false;
    wxColour fixedColor     = wxColour(255, 255, 255);

    // -----------------------------------------------------------------------
    // Intensity thresholds (visual-only)
    // -----------------------------------------------------------------------
    double minWind     = 0.0;     // knots
    double maxPressure = 2000.0;  // hPa
};

#endif // CYCLONE_PARAMS_H

