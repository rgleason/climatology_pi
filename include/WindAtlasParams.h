#ifndef WINDATLAS_PARAMS_H
#define WINDATLAS_PARAMS_H

#include <wx/colour.h>

// UI-facing Wind Atlas parameters.
// StandardDisplayParams stores this.
// ClimatologyRenderParams stores this.
// ClimatologyOverlayFactory receives this and uses WindAtlasData internally.

struct WindAtlasParams
{
    bool enabled = false;

    int size = 0;
    int spacing = 0;
    int numberSize = 0;
    int centerMode = 0;

    bool showCalm = false;
    bool showFreq = false;
    bool autoScale = false;

 //   double opacity = 1.0;   // comes from ClimatologyRenderParams.alpha
    bool allTimesWind = false;
    bool allTimesCurrent = false;

    wxColour color;        // required by ClimatologyDialog.cpp

    int directionBins = 8; // N, NE, E, SE, S, SW, W, NW
};

#endif
