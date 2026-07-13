#ifndef __CLIMATOLOGY_RENDER_PARAMS_H__
#define __CLIMATOLOGY_RENDER_PARAMS_H__

#pragma once
#include <wx/datetime.h>
#include <wx/colour.h>
#include "ocpn_plugin.h"   // PlugIn_ViewPort, piDC
#include "ClimatologyCoord.h"
#include "OverlayTypes.h"  // <-- REQUIRED for NUM_OVERLAYS


struct piDC
{
    wxDC* dc = nullptr;

    wxDC* GetDC() const { return dc; }
};


struct ClimatologyRenderParams
{
    // Rendering context
    PlugIn_ViewPort* vp = nullptr;
    bool useGL = true;
    bool useShaders = true;

	// GL DC context
    piDC* dc = nullptr;
    bool dcContextValid = false;
	bool glContextValid = false;

    // DPI / pixel scaling (optional but recommended)
	// viewport->chart_scale device pixel ratio
    double pixelScale = 1.0;  
	// wxWindow::GetContentScaleFactor()	
    double contentScale = 1.0;     

    // Cursor position (optional but recommended)
    double cursorLat = NAN;
    double cursorLon = NAN;

    // Overlay selection
    int overlayType = 0;
    wxString overlayTypeName;

    // Visibility flags
    bool overlayEnabled = true;
    bool overlayMap = true;
    bool overlayInterpolation = true;
	
	bool showWindAtlas = false;
    bool showCyclones = false;
    bool showElNino = false;
    bool showLaNina = false;
    bool showNeutral = false;
	
    // Timeline (month slider)
    int currentMonth = 0;        // 0–11
    int nextMonth = 0;           // interpolation target
    double monthInterpolation = 0.0;

    // All-times modes
    bool allTimesWind = false;
    bool allTimesCurrent = false;
    bool allTimesCyclones = false;

    // Cyclone parameters
    int cycloneColorMode = 0;
    double cycloneOpacity = 1.0;

    // Basin toggles
    bool showEPA = true;
    bool showWPA = true;
    bool showSPA = true;
    bool showATL = true;
    bool showNIO = true;
    bool showSHE = true;

    // Resolved per-overlay parameters (from SDP)
//    double overlayOpacity = 1.0;   // alpha[overlayType]
    int units = 0;                 // units[overlayType]

    bool showIsoBars = false;      // showIsoBars[overlayType]
    int isoBarSpacing = 0;         // isoSpacing[overlayType]
    int isoBarStep = 0;            // isoStep[overlayType]

    bool showNumbers = false;      // showNumbers[overlayType]
    double numberSize = 0.0;       // numberSize[overlayType]

    bool showArrows = false;       // showArrows[overlayType]
    int arrowWidth = 0;            // arrowWidth[overlayType]
    int arrowSpacing = 0;          // arrowSpacing[overlayType]
    int arrowSize = 0;             // arrowSize[overlayType]
    int arrowMode = 0;             // arrowMode[overlayType]

    wxColour color;                // color[overlayType]

    // Wind atlas
 	struct WindAtlasParams
	{
		bool enabled = false;
		double size = 0.0;
		double spacing = 0.0;
		double opacity = 1.0;

		int numberSize = 0;
		int centerMode = 0;

		bool showCalm = false;
		bool showFreq = false;
		bool autoScale = false;

		wxColour color;
		int resolution = 64;
	};

	WindAtlasParams windAtlas;
	
	// Native NOAA grid spacing per overlay (added for unified model)
	double latStep[NUM_OVERLAYS];
	double lonStep[NUM_OVERLAYS];	
 
    // Global smoothing (applies to all grid-based overlays)
    bool smoothing = true;	
};

#endif // __CLIMATOLOGY_RENDER_PARAMS_H__
