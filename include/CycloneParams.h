#ifndef CYCLONE_PARAMS_H
#define CYCLONE_PARAMS_H

// Persistent cyclone appearance preferences.
// Saved in config as part of StandardDisplayParams.
// Used by the cyclone renderer for visual styling ONLY.
// Not used by ClimatologyRenderParams.h

struct CycloneParams
{
    bool enabled = false;

    // Track center and span (visual timeline controls)
    int centerDay = 180;
    int daySpan   = 30;

    // Intensity display thresholds (visual only)
    double minWind     = 0.0;
    double maxPressure = 2000.0;

    // Storm-type mask (visual category coloring)
    int stateMask = 0xFFFFFFFF;

    // ENSO visibility (visual only)
    bool showElNino       = true;
    bool showLaNina       = true;
    bool showNeutral      = true;
    bool showNotAvailable = true;

    // All-times mode (visual)
    bool allTimesCyclones = false;

    // Appearance
    double opacity = 1.0;
    int    colorMode = 0;
};

#endif // CYCLONE_PARAMS_H
