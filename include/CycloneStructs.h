#pragma once

#include <wx/wxprec.h>
#ifndef WX_PRECOMP
#include <wx/wx.h>
#endif

#include <vector>
#include <map>
#include <wx/datetime.h>
#include "ClimatologyEnums.h"      // CycloneState, CycloneBasin, CycloneENSO

// ============================================================================
// CyclonePoint
// Atomic observation: one storm at one time.
// ============================================================================
struct CyclonePoint
{
    // Identification
    int         stormId    = -1;          // unique storm/track id
    int         pointIndex = -1;          // index within track (optional)

    // Time
    wxDateTime  time;                     // observation time (UTC)

    // Position
    double      lat        = std::numeric_limits<double>::quiet_NaN();
    double      lon        = std::numeric_limits<double>::quiet_NaN();

    // Intensity
    double      wind       = std::numeric_limits<double>::quiet_NaN(); // knots
    double      pressure   = std::numeric_limits<double>::quiet_NaN(); // hPa

    // Classification (per‑point, if available)
    CycloneState      state     = CycloneState::UNKNOWN;
    CycloneBasin      basin     = CycloneBasin::UNKNOWN;
    CycloneENSO      ensoPhase = CycloneENSO::NA;

    bool        valid      = false;
};


// ==========================================================
// Cyclone — raw track from loader, ONLY used during loading 
//  NOT by rendering, filtering, or unified database.
// ==========================================================
struct Cyclone
{
	int id;   
	
    CycloneBasin basin;
	CycloneENSO enso;
	
	double maxWind;      
    double minPressure;  

    std::vector<CyclonePoint> points;
};


// ============================================================================
// CycloneTrack
// Full life history of one storm: ordered CyclonePoints + metadata.
//  precomputed, cached, fast rendering structure ============================================================================
struct CycloneTrack
{
    // Identification
    int       id   = -1;          // storm/track id (matches CyclonePoint::stormId)
    wxString  name;               // optional storm name

    // Track‑level classification
    CycloneBasin  basin     = CycloneBasin::UNKNOWN;
    CycloneENSO   ensoPhase = CycloneENSO::NA;

    // Temporal extent
    wxDateTime  startDate;       // first valid point
    wxDateTime  endDate;         // last valid point
    int         daySpan  = 0;    // (endDate - startDate).GetDays()

    // Precomputed aggregates (for filtering/analysis)
    double      maxWind       = std::numeric_limits<double>::quiet_NaN(); // kt
    double      minPressure   = std::numeric_limits<double>::quiet_NaN(); // hPa
    CycloneState dominantState = CycloneState::UNKNOWN;                   // most frequent state

    // Ordered time series
    std::vector<CyclonePoint> points;

    bool valid = false;
};


// ============================================================================
// CycloneData
// Container for all cyclone tracks + indices for fast querying.
// ============================================================================
struct CycloneData
{
    bool valid = false;

    // All tracks
    std::vector<CycloneTrack> tracks;

    // Indices (optional but recommended)
    // stormId → track index
    std::map<int, int> idToIndex;

    // year → list of track indices
    std::map<int, std::vector<int>> yearToTracks;

    // basin → list of track indices
    std::map<CycloneBasin, std::vector<int>> basinToTracks;

    // ENSO phase → list of track indices
    std::map<CycloneENSO, std::vector<int>> ensoToTracks;
};
