#ifndef STANDARD_DISPLAY_PARAMS_H
#define STANDARD_DISPLAY_PARAMS_H

#include <vector>
#include <wx/colour.h>
#include "ClimatologyCoord.h"
#include "OverlayTypes.h" // for NUM_OVERLAYS
#include "WindAtlasParams.h"
#include "CycloneParams.h"

// Persistent user preferences for all overlays.
// The factory reads these and builds ClimatologyRenderParams.

// Plugin’s saved settings, usually loaded from config
// and stored as StandardDisplayParams.h  m_params;
// Purpose: Describes what the user wants in general,
// not what is being drawn right now.
// Characteristics:  Persistent — saved across sessions
// Global — applies to all overlays
// Per‑overlay arrays — units, colors, arrow sizes, iso spacing, etc.
// Dialog-driven: dialog modifies this struct when the user changes setting
// Then the factory uses it to:
// populate ClimatologyRenderParams defaults
// determine which overlays are enabled
// determine units for each overlay
// determine arrow/iso settings
// determine color maps
// determine cyclone/wind atlas configuration
// This struct is the “user preferences database” for the plugin.

// Do not forward declare as they become pointers

struct StandardDisplayParams
{
    // Which overlay is selected in the dialog
    int overlayType = OVERLAY_WIND;
	
	// Global Settings
	//-----------------
	double transparency = 100.0;     // legacy name used in COF.cpp
	bool smooth = true;              // global smoothing


	// Settings to be removed later that are not needed
	double opacity = 100.0;          // global overlay opacity (0–100)
	int numberSpacing = 10;          // legacy numeric spacing

	double calibrationFactor[NUM_OVERLAYS];
	double calibrationOffset[NUM_OVERLAYS];


    // Per-overlay display parameters
	// --------------------------------------

    // Transparency (0.0 = fully transparent, 1.0 = opaque)
    double alpha[NUM_OVERLAYS] = {1.0};

    // Units per overlay (wind, pressure, temp, currents, etc.)
    int units[NUM_OVERLAYS] = {0};

    // Overlay enabled/visible
    bool showOverlayType[NUM_OVERLAYS] = {false};

    // Colors per overlay
    wxColour color[NUM_OVERLAYS];

    // Direction arrows
    bool showArrows[NUM_OVERLAYS] = {false};
    int  arrowWidth[NUM_OVERLAYS] = {0};
    int  arrowSpacing[NUM_OVERLAYS] = {0};
    int  arrowSize[NUM_OVERLAYS] = {0};
    int  arrowMode[NUM_OVERLAYS] = {0};

    // Numeric labels
    bool   showNumbers[NUM_OVERLAYS] = {false};
    double numberSize[NUM_OVERLAYS] = {0};   // replaces numberSpacing

    // Isobars
    bool showIsoBars[NUM_OVERLAYS] = {false};
    int  isoSpacing[NUM_OVERLAYS] = {0};
    int  isoStep[NUM_OVERLAYS] = {0};

    // Attached parameter blocks
    WindAtlasParams windAtlas;
    CycloneParams   cyclone;    // Per-overlay parameters
};

#endif
