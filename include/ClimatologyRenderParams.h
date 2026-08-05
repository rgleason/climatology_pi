#ifndef __CLIMATOLOGY_RENDER_PARAMS_H__
#define __CLIMATOLOGY_RENDER_PARAMS_H__

#pragma once
#include "piDC.h"

#include "ocpn_plugin.h"    // ensures PlugIn_ViewPort is fully known

#include <wx/colour.h>

#include "DisplayMode.h"
#include "ClimatologyCoord.h"
#include "ClimatologyEnums.h"          // defines OVERLAY_SEA_TEMP, etc.
#include "WindAtlasParams.h"
#include "CycloneParams.h" // Cyclone Parameters
#include "CycloneFilterParams.h"
#include "WindData.h"
#include "CurrentData.h"

// ============================================================================
// ClimatologyRenderParams
// Unified rendering parameters for all overlays.
// This struct is the authoritative parameter block passed to the renderer.
// ============================================================================
struct ClimatologyRenderParams
{
    // -----------------------------------------------------------------------
    // Rendering context
    // -----------------------------------------------------------------------
    PlugIn_ViewPort* vp = nullptr;
    piDC* dc = nullptr;

    bool useGL      = true;
    bool useShaders = true;

    bool dcContextValid = false;
    bool glContextValid = false;

    // -----------------------------------------------------------------------
    // Overlay selection
    // -----------------------------------------------------------------------
    int  overlayType    = OVERLAY_WIND;
    bool overlayEnabled = false;
    bool overlayMap     = true;
	bool overlayInterpolation = true; 
	
	// -----------------------------------------------------------------------
	// Unified‑grid display mode (scalar compositing)
	// -----------------------------------------------------------------------
	DisplayMode mode = DisplayMode::SingleScaled;

    // -----------------------------------------------------------------------
    // Cursor position (optional)
    // -----------------------------------------------------------------------
    double cursorLat = NAN;
    double cursorLon = NAN;

	// -----------------------------------------------------------------------
    // Month interpolation (used by getCurValue)
    // -----------------------------------------------------------------------
    int    currentMonth      = 1;     // 0–11
    int    nextMonth         = 2;     // 0–11
    double monthInterpolation = 0.0;  // 0.0–1.0
	
	// -----------------------------------------------------------------------
    // All-times modes
	// -----------------------------------------------------------------------	
    bool allTimesWind     = false;
    bool allTimesCurrent  = false;

    // -----------------------------------------------------------------------
    // Per-overlay parameters (from StandardDisplayParams)
    // -----------------------------------------------------------------------
    int     units        = 0;    // units[overlayType]

    bool    showIsoBars  = false;
    int     isoBarSpacing = 0;
    int     isoBarStep    = 0;

    bool   showNumbers = false;
    double numberSize  = 0.0;

    bool showArrows   = false;
    int  arrowWidth   = 0;
    int  arrowSpacing = 0;
    int  arrowSize    = 0;
    int  arrowMode    = 0;

    wxColour color;       // color[overlayType]
	
	double alpha = 1.0;  // Transparency for single overlay being rendered
	
    // -----------------------------------------------------------------------
    // Wind Atlas (unified nested params)
    // -----------------------------------------------------------------------
    bool showWindAtlas = false;
    WindAtlasParams windAtlas;	

	// -----------------------------------------------------------------------
    // Cyclone parameters (unified)
    // -----------------------------------------------------------------------
 	CycloneParams cycloneParams;
	CycloneFilterParams cycloneFilter;

	bool showCyclones = false;
	
	bool showEPA = false;
	bool showWPA = false;
	bool showSPA = false;
	bool showATL = false;
	bool showNIO = false;
	bool showSHE = false;

	bool showElNino = false;
	bool showLaNina = false;
	bool showNeutral = false;

	// -----------------------------------------------------------------------
    // Native NOAA grid spacing per overlay (unified model)
    // -----------------------------------------------------------------------
    double latStep[NUM_OVERLAYS] = {0.0};
    double lonStep[NUM_OVERLAYS] = {0.0};

    // -----------------------------------------------------------------------
    // Global smoothing
    // -----------------------------------------------------------------------
    bool smoothing = true;
};

#endif // __CLIMATOLOGY_RENDER_PARAMS_H__
