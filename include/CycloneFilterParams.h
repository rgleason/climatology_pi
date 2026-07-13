#ifndef CYCLONE_FILTER_PARAMS_H
#define CYCLONE_FILTER_PARAMS_H

#pragma once
#include <wx/datetime.h>

// Cyclone filtering rule set.
// UI-facing, non-persistent, consumed by ClimatologyOverlayFactory.
// Determines which cyclone tracks are INCLUDED in the dataset.

struct CycloneFilterParams
{
    // Storm-type mask (tropical, subtropical, etc)
    int statemask = 0;

    // Intensity filters
    int minwindspeed = 0;   // knots
    int maxpressure = 0;    // hPa

    // Date range filters
    wxDateTime startDate;
    wxDateTime endDate;

    // ENSO filters
    bool allowElNino       = true;
    bool allowLaNina       = true;
    bool allowNeutral      = true;
    bool allowNotAvailable = true;

    // Basin filters (point-level)
    bool allowEPA = true;
    bool allowWPA = true;
    bool allowSPA = true;
    bool allowATL = true;
    bool allowNIO = true;
    bool allowSHE = true;
};


#endif // CYCLONE_FILTER_PARAMS_H
