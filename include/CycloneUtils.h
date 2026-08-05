#pragma once
#include "CycloneStructs.h"
#include <wx/filename.h>

// ------------------------------------------------------------
// Basin → string
// ------------------------------------------------------------
inline const char* BasinToString(CycloneBasin b)
{
    static const char* names[] = {
        "East Pacific",   // EPA
        "West Pacific",   // WPA
        "South Pacific",  // SPA
        "Atlantic",       // ATL
        "North Indian",   // NIO
        "South Indian"    // SHE
    };

    int idx = static_cast<int>(b);
    return (idx >= 0 && idx < 6) ? names[idx] : "Unknown Basin";
}

// ------------------------------------------------------------
// ENSO → string
// ------------------------------------------------------------
inline const char* ENSOToString(CycloneENSO e)
{
    static const char* names[] = {
        "El Niño",   // ElNino
        "La Niña",   // LaNina
        "Neutral",   // Neutral
        "N/A"        // NA
    };

    int idx = static_cast<int>(e);
    return (idx >= 0 && idx < 4) ? names[idx] : "Unknown";
}

// ------------------------------------------------------------
// CycloneState → string
// ------------------------------------------------------------
inline const char* CycloneStateToString(CycloneState s)
{
    static const char* names[] = {
        "Tropical",       // TROPICAL
        "Subtropical",    // SUBTROPICAL
        "Extratropical",  // EXTRATROPICAL
        "Wave",           // WAVE
        "Remanent",       // REMANENT
        "Unknown"         // UNKNOWN
    };

    int idx = static_cast<int>(s);
    return (idx >= 0 && idx < 6) ? names[idx] : "Unknown";
}

// ------------------------------------------------------------
// Numeric code → CycloneState
// ------------------------------------------------------------
inline CycloneState CycloneStateFromCode(int code)
{
    return (code >= 0 && code <= 4)
           ? static_cast<CycloneState>(code)
           : CycloneState::UNKNOWN;
}

// ------------------------------------------------------------
// Filename → CycloneBasin
// ------------------------------------------------------------
inline CycloneBasin BasinFromFilename(const wxString& filename)
{
    wxString name = wxFileName(filename).GetName().Lower();

    if (name.Contains("epa")) return CycloneBasin::EPA;
    if (name.Contains("wpa")) return CycloneBasin::WPA;
    if (name.Contains("spa")) return CycloneBasin::SPA;
    if (name.Contains("atl")) return CycloneBasin::ATL;
    if (name.Contains("nio")) return CycloneBasin::NIO;
    if (name.Contains("she")) return CycloneBasin::SHE;

    return CycloneBasin::EPA;   // safe fallback
}
