#ifndef STANDARD_DISPLAY_PARAMS_H
#define STANDARD_DISPLAY_PARAMS_H

#include <wx/colour.h>
#include <vector>

#include "ClimatologyEnums.h"
#include "CycloneParams.h"               // Unified cyclone params
#include "WindAtlasParams.h"



// ============================================================================
// StandardDisplayParams
// Persistent user preferences for all overlays.
// The dialog writes into this struct.
// The factory reads this struct and produces ClimatologyRenderParams.
// ============================================================================

struct StandardDisplayParams
{
    // -----------------------------------------------------------------------
    // Per overlay units, colors, toggles
    // -----------------------------------------------------------------------
 
	int units[NUM_OVERLAYS]        = {0};

    // Overlay enabled/visible
    bool showOverlayType[NUM_OVERLAYS] = {false};

	// Colors per overlay
    wxColour color[NUM_OVERLAYS];
	
	// Transparency (0.0 = fully transparent, 1.0 = opaque)
    double alpha[NUM_OVERLAYS] = {1.0};

    // Isobars
    bool showIsoBars[NUM_OVERLAYS] = {false};
    int  isoSpacing[NUM_OVERLAYS]  = {0};
    int  isoStep[NUM_OVERLAYS]     = {0};
	
	// Numeric labels
    bool   showNumbers[NUM_OVERLAYS] = {false};
    double numberSize[NUM_OVERLAYS]  = {0.0};   // replaces numberSpacing

    // Direction arrows
    bool showArrows[NUM_OVERLAYS]   = {false};
    int  arrowWidth[NUM_OVERLAYS]   = {0};
    int  arrowSpacing[NUM_OVERLAYS] = {0};
    int  arrowSize[NUM_OVERLAYS]    = {0};
    int  arrowMode[NUM_OVERLAYS]    = {0};
	
	// Unified-grid blend mode
    DisplayMode mode = DisplayMode::SingleScaled;

    // -----------------------------------------------------------------------
    // Global settings
    // -----------------------------------------------------------------------
    bool   smooth       = true;      // global smoothing

    // Legacy fields (kept for backward compatibility)
    double opacity        = 100.0;   // global overlay opacity (0–100)
    int    numberSpacing  = 10;      // legacy numeric spacing

    double calibrationFactor[NUM_OVERLAYS] = {0.0};
    double calibrationOffset[NUM_OVERLAYS] = {0.0};
	
    // -----------------------------------------------------------------------
    // Attached parameter blocks (unified model)
    // -----------------------------------------------------------------------

	// Wind Atlas UI Params (same as CRP)
	WindAtlasParams windAtlas;

    // Cyclone UI parameters (unified CycloneParams)
    CycloneParams cyclone;
};

#endif // STANDARD_DISPLAY_PARAMS_H
