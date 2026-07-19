#include "OverlayMapping.h"
#include <map>

static const std::map<OverlayType, NoaaOverlayType> UI_TO_NOAA = {
    { OVERLAY_PRESSURE, NOAA_SLP },
    { OVERLAY_SEA_TEMP, NOAA_SST },
    { OVERLAY_AIR_TEMP, NOAA_AT },
    { OVERLAY_CLOUD,    NOAA_CLD },
    { OVERLAY_PRECIP,   NOAA_PCP },
    { OVERLAY_RH,       NOAA_RH },
    { OVERLAY_LIGHTNING,NOAA_LGT },
    { OVERLAY_WIND,     NOAA_WIND },
    { OVERLAY_WIND_ATLAS, NOAA_WIND },
    { OVERLAY_CURRENT,  NOAA_CURRENT },
    { OVERLAY_SEA_DEPTH,NOAA_DEPTH },
};

static const std::map<NoaaOverlayType, OverlayType> NOAA_TO_UI = {
    { NOAA_SLP,     OVERLAY_PRESSURE },
    { NOAA_SST,     OVERLAY_SEA_TEMP },
    { NOAA_AT,      OVERLAY_AIR_TEMP },
    { NOAA_CLD,     OVERLAY_CLOUD },
    { NOAA_PCP,     OVERLAY_PRECIP },
    { NOAA_RH,      OVERLAY_RH },
    { NOAA_LGT,     OVERLAY_LIGHTNING },
    { NOAA_WIND,    OVERLAY_WIND },
    { NOAA_CURRENT, OVERLAY_CURRENT },
    { NOAA_DEPTH,   OVERLAY_SEA_DEPTH }
};

NoaaOverlayType ToNoaa(OverlayType ui)
{
    auto it = UI_TO_NOAA.find(ui);
    return (it != UI_TO_NOAA.end()) ? it->second : NOAA_NUM_OVERLAYS;
}

OverlayType ToUI(NoaaOverlayType nt)
{
    auto it = NOAA_TO_UI.find(nt);
    return (it != NOAA_TO_UI.end()) ? it->second : OVERLAY_INVALID;
}
