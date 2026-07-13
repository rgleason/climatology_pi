# ⭐ HEADER‑ONLY VERSION OF `ClimatologyUnits`  
*(If you prefer inline functions for speed and simplicity.)*

### **ClimatologyUnits.h (header‑only)**

#pragma once
#include <wx/string.h>
#include <vector>
#include "ClimatologyDefs.h"

namespace climatology::units {

// ---- Unit Name ----
inline wxString UnitName(int unit) {
    switch(unit) {
    case 0: return _("knots");
    case 1: return _("m/s");
    case 2: return _("km/h");
    case 3: return _("°C");
    case 4: return _("°F");
    case 5: return _("hPa");
    case 6: return _("mb");
    case 7: return _("inHg");
    case 8: return _("mm");
    case 9: return _("in");
    default: return _("?");
    }
}

// ---- Conversions ----
inline double ConvertSpeed(double v, int unit) {
    switch(unit) {
    case 0: return v;
    case 1: return v * 0.514444;
    case 2: return v * 1.852;
    default: return v;
    }
}

inline double ConvertPressure(double v, int unit) {
    switch(unit) {
    case 5: return v;
    case 6: return v;
    case 7: return v * 0.029529983;
    default: return v;
    }
}

inline double ConvertTemperature(double v, int unit) {
    switch(unit) {
    case 3: return v;
    case 4: return (v * 9.0/5.0) + 32.0;
    default: return v;
    }
}

inline double ConvertPrecip(double v, int unit) {
    switch(unit) {
    case 8: return v;
    case 9: return v * 0.0393701;
    default: return v;
    }
}

inline double ConvertDepth(double v, int unit) {
    return v;
}

// ---- Formatting ----
inline wxString FormatSpeed(double v, int unit) {
    return wxString::Format("%.1f %s", ConvertSpeed(v, unit), UnitName(unit));
}

inline wxString FormatPressure(double v, int unit) {
    double cv = ConvertPressure(v, unit);
    return (unit == 7)
        ? wxString::Format("%.3f %s", cv, UnitName(unit))
        : wxString::Format("%.1f %s", cv, UnitName(unit));
}

inline wxString FormatTemp(double v, int unit) {
    return wxString::Format("%.1f%s", ConvertTemperature(v, unit), UnitName(unit));
}

inline wxString FormatPrecip(double v, int unit) {
    return wxString::Format("%.1f %s", ConvertPrecip(v, unit), UnitName(unit));
}

inline wxString FormatDepth(double v, int unit) {
    return wxString::Format("%.1f %s", ConvertDepth(v, unit), UnitName(unit));
}

// ---- Dispatchers ----
inline double ConvertValue(OverlayType t, double v, int unit) {
    switch(t) {
    case OVERLAY_WIND:
    case OVERLAY_CURRENT: return ConvertSpeed(v, unit);
    case OVERLAY_PRESSURE: return ConvertPressure(v, unit);
    case OVERLAY_SEA_TEMP:
    case OVERLAY_AIR_TEMP: return ConvertTemperature(v, unit);
    case OVERLAY_PRECIP: return ConvertPrecip(v, unit);
    case OVERLAY_SEA_DEPTH: return ConvertDepth(v, unit);
    default: return v;
    }
}

inline wxString FormatValue(OverlayType t, double v, int unit) {
    switch(t) {
    case OVERLAY_WIND:
    case OVERLAY_CURRENT: return FormatSpeed(v, unit);
    case OVERLAY_PRESSURE: return FormatPressure(v, unit);
    case OVERLAY_SEA_TEMP:
    case OVERLAY_AIR_TEMP: return FormatTemp(v, unit);
    case OVERLAY_PRECIP: return FormatPrecip(v, unit);
    case OVERLAY_SEA_DEPTH: return FormatDepth(v, unit);
    case OVERLAY_CLOUD:
    case OVERLAY_RH: return wxString::Format("%.0f%%", v);
    case OVERLAY_LIGHTNING: return wxString::Format("%.0f", v);
    default: return wxString::Format("%.1f", v);
    }
}

// ---- Valid Units ----
inline void InitValidUnits(std::vector<int> validUnits[]) {
    validUnits[OVERLAY_WIND]       = { 0, 1, 2 };
    validUnits[OVERLAY_CURRENT]    = { 0, 1, 2 };
    validUnits[OVERLAY_PRESSURE]   = { 5, 6, 7 };
    validUnits[OVERLAY_SEA_TEMP]   = { 3, 4 };
    validUnits[OVERLAY_AIR_TEMP]   = { 3, 4 };
    validUnits[OVERLAY_PRECIP]     = { 8, 9 };
    validUnits[OVERLAY_SEA_DEPTH]  = { };
    validUnits[OVERLAY_CLOUD]      = { };
    validUnits[OVERLAY_RH]         = { };
    validUnits[OVERLAY_LIGHTNING]  = { };
    validUnits[OVERLAY_WIND_ATLAS] = { };
    validUnits[OVERLAY_CYCLONES]   = { };
}

}  // emd of namespace