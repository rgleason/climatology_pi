#ifndef CYCLONE_PARAMS_H
#define CYCLONE_PARAMS_H

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
    int centerDay = 180;     // Day-of-year center (1–365)
    int daySpan   = 30;      // +/- days around center
	int currentMonth = 0;   // 0–11
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
    int    colorMode = 0;     // reserved for future color modes

    // -----------------------------------------------------------------------
    // Intensity thresholds (visual-only)
    // -----------------------------------------------------------------------
    double minWind     = 0.0;     // knots
    double maxPressure = 2000.0;  // hPa

    // -----------------------------------------------------------------------
    // Storm-type mask (visual-only)
    // -----------------------------------------------------------------------
    int stateMask = 0xFFFFFFFF;   // tropical, subtropical, extratropical, etc.
};

#endif // CYCLONE_PARAMS_H
