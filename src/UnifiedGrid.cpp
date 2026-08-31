#include "UnifiedGrid.h"
#include "CycloneStructs.h"
#include "ClimatologyDataModel.h"

// ============================================================================
// Full UnifiedGrid.cpp — Modern unified climatology model
// ============================================================================

UnifiedGrid::UnifiedGrid()
    : rows(0)
    , cols(0)
    , lat0(0.0)
    , lon0(0.0)
    , latStep(0.0)
    , lonStep(0.0)
    , valid(false)
{
    validMonths.assign(12, false);
    scalar.resize(12);
    u.resize(12);
    v.resize(12);
}

// ----------------------------------------------------------------------------
// Geometry
// ----------------------------------------------------------------------------

void UnifiedGrid::InitGeometry(int r, int c,
                               double la0, double lo0,
                               double laStep, double loStep)
{
    rows    = r;
    cols    = c;
    lat0    = la0;
    lon0    = lo0;
    latStep = laStep;
    lonStep = loStep;

    valid = true;
}

bool UnifiedGrid::IsValid(int month) const
{
    if (month < 0 || month >= 12)
        return false;
    return valid && validMonths[month];
}

// ----------------------------------------------------------------------------
// Month-major initialization
// ----------------------------------------------------------------------------

void UnifiedGrid::InitScalarMonth(int month)
{
    scalar[month].assign(rows * cols, 0.0f);
    validMonths[month] = true;
}

void UnifiedGrid::InitVectorMonth(int month)
{
    u[month].assign(rows * cols, 0.0f);
    v[month].assign(rows * cols, 0.0f);
    validMonths[month] = true;
}

// ----------------------------------------------------------------------------
// Geometry helpers
// ----------------------------------------------------------------------------

int UnifiedGrid::LatLonToIndex(double lat, double lon) const
{
    int r = int((lat - lat0) / latStep);
    int c = int((lon - lon0) / lonStep);

    if (r < 0 || r >= rows || c < 0 || c >= cols)
        return -1;

    return r * cols + c;
}

void UnifiedGrid::IndexToLatLon(int index, double& lat, double& lon) const
{
    int r = index / cols;
    int c = index % cols;

    lat = lat0 + r * latStep;
    lon = lon0 + c * lonStep;
}

// ----------------------------------------------------------------------------
// Normalization
// ----------------------------------------------------------------------------

void UnifiedGrid::NormalizeRawData()
{
    // Phase 1: no-op
}

// ----------------------------------------------------------------------------
// Month slices
// ----------------------------------------------------------------------------

const std::vector<float>& UnifiedGrid::GetMonthSliceU(int month) const
{
    static const std::vector<float> empty;
    return (month >= 0 && month < u.size()) ? u[month] : empty;
}

const std::vector<float>& UnifiedGrid::GetMonthSliceV(int month) const
{
    static const std::vector<float> empty;
    return (month >= 0 && month < v.size()) ? v[month] : empty;
}



// ------------------------------------------------------------------------
// LoadFromDataModel  Populate UnifiedGrid::cyclone_tracks from ClimatologyDataModel.
// ---------------------------------------------------
void UnifiedGrid::LoadFromDataModel(const ClimatologyDataModel& dm)
{
    // --- Geometry ---
    rows    = dm.GetRows();
    cols    = dm.GetCols();
    lat0    = dm.GetLat0();
    lon0    = dm.GetLon0();
    latStep = dm.GetLatStep();
    lonStep = dm.GetLonStep();

    // --- Scalar + Vector month-major arrays ---
    scalar = dm.scalar;
    u      = dm.u;
    v      = dm.v;

    // --- Cyclone tracks ---
	// Full CycloneTrack objects (id, name, basin, ENSO, points, metadata)
    cyclone_tracks = dm.cyclone_tracks;

    // --- Wind atlas ---
    wind_atlas = dm.wind_atlas;

    // --- ENSO index ---
    for (int i = 0; i < 12; i++)
        enso_index[i] = dm.enso_index[i];

    // --- Normalization hook (empty for Phase 1) ---
    NormalizeRawData();
	
	wxLogMessage("Wind atlas entries: %zu (expected %d)", 
             wind_atlas.size(), rows * cols);

	// --- UnifiedGrid Self-Test ---
	wxLogMessage("UnifiedGrid: Geometry rows=%d cols=%d lat0=%f lon0=%f latStep=%f lonStep=%f",
				 rows, cols, lat0, lon0, latStep, lonStep);

	for (int m = 0; m < 12; m++) {
		wxLogMessage("UnifiedGrid: Month %d scalar size=%zu u size=%zu v size=%zu",
					 m,
					 scalar[m].size(),
					 u[m].size(),
					 v[m].size());
	}

	wxLogMessage("UnifiedGrid: Cyclone tracks loaded: %zu", cyclone_tracks.size());
	wxLogMessage("UnifiedGrid: Wind atlas entries: %zu", wind_atlas.size());
		int ensoCount = 0;
		for (int i = 0; i < 12; i++)
			if (!std::isnan(enso_index[i]))
				ensoCount++;
	wxLogMessage("UnifiedGrid: ENSO index entries: %d", ensoCount);

}

// ------------------------------------------------------------------------
// GetStormCategory   Populate UnifiedGrid from ClimatologyDataModel
// ------------------------------------------------------------------------

int UnifiedGrid::GetStormCategory(float pressure, float wind) const
{
    // Saffir–Simpson hurricane wind scale (knots)
    if (wind >= 137.0f) return 5;   // Cat 5
    if (wind >= 113.0f) return 4;   // Cat 4
    if (wind >= 96.0f)  return 3;   // Cat 3
    if (wind >= 83.0f)  return 2;   // Cat 2
    if (wind >= 64.0f)  return 1;   // Cat 1

    // Tropical storm / depression
    return 0;
}
