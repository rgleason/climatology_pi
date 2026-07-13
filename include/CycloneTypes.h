#pragma once

#include <vector>

// ----------------------------------------------------------
// Cyclone state
// ----------------------------------------------------------
enum class CycloneState {
    TROPICAL,
    SUBTROPICAL,
    EXTRATROPICAL,
    WAVE,
    REMANENT,
    UNKNOWN
};

// ----------------------------------------------------------
// ENSO period (track-level)
// ----------------------------------------------------------
enum class ENSOPeriod {
    EL_NINO,
    LA_NINA,
    NEUTRAL,
    NA
};

// ----------------------------------------------------------
// ENSO classification (point-level)
// ----------------------------------------------------------
enum CycloneENSO {
    ElNino,
    LaNina,
    Neutral
};

// ----------------------------------------------------------
// Basin classification (point-level)
// ----------------------------------------------------------
enum CycloneBasin {
    EPA,
    WPA,
    SPA,
    ATL,
    NIO,
    SHE
};

struct CyclonePoint {
    double lat;
    double lon;

    int year;
    int month;      // 1–12
    int day;        // 1–31
    int dayOfYear;  // computed by loader

    CycloneState state;
    CycloneENSO enso;
    CycloneBasin basin;
    double maxWind;

    double pressure;   // hPa
    double windspeed;  // knots
};

struct Cyclone {
    int basinId;                 // assigned by loader
    ENSOPeriod enso;             // computed by loader
    std::vector<CyclonePoint> points;
};
