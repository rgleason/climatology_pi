#ifndef WINDATLAS_PARAMS_H
#define WINDATLAS_PARAMS_H

//ClimatologyOverlayFactory uses only StandardDisplayParam.h so this struct will be included.

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

    double opacity = 1.0;
    bool allTimesWind = false;
    bool allTimesCurrent = false;
	
	 // legacy compatibility
    wxColour color;        // required by ClimatologyDialog.cpp

	// N, NE, E, SE, S, SW, W, NW replaces resolution
    int directionBins = 8;  
};

#endif
