#pragma once

// ==========================================================
// CYCLONE ENUMS
// ==========================================================

enum class CycloneState {
    TROPICAL,
    SUBTROPICAL,
    EXTRATROPICAL,
    WAVE,
    REMANENT,
    UNKNOWN
};

enum class ENSOPeriod {
    EL_NINO,
    LA_NINA,
    NEUTRAL,
    NA
};

enum CycloneENSO {
    ElNino,
    LaNina,
    Neutral
};

enum CycloneBasin {
    EPA,
    WPA,
    SPA,
    ATL,
    NIO,
    SHE
};

// ==========================================================
// NOAA RAW DATA OVERLAY TYPES
// These correspond directly to NOAA dataset categories.
// ==========================================================

enum NoaaOverlayType {
    NOAA_SLP = 0,      // Sea Level Pressure
    NOAA_SST,          // Sea Surface Temperature
    NOAA_AT,           // Air Temperature
    NOAA_CLD,          // Cloud Cover
    NOAA_PCP,          // Precipitation
    NOAA_RH,           // Relative Humidity
    NOAA_LGT,          // Lightning
    NOAA_WIND,         // Wind Vectors
    NOAA_CURRENT,      // Ocean Currents
    NOAA_DEPTH,        // Bathymetry
    NOAA_NUM_OVERLAYS
};

// ==========================================================
// UI + RENDERING OVERLAY TYPES
// These are used by the dialog, factory, GL renderer, etc.
// ==========================================================

enum OverlayType {
    OVERLAY_INVALID = -1,

    OVERLAY_WIND = 0,
    OVERLAY_WIND_ATLAS,
    OVERLAY_CYCLONES,
    OVERLAY_CURRENT,
    OVERLAY_PRESSURE,
    OVERLAY_SEA_TEMP,
    OVERLAY_AIR_TEMP,
    OVERLAY_CLOUD,
    OVERLAY_PRECIP,
    OVERLAY_RH,
    OVERLAY_LIGHTNING,
    OVERLAY_SEA_DEPTH,

    NUM_OVERLAYS
};
