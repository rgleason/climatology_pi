// ============================================================================
// ClimatologyOverlayFactory.cpp  (Model A — unified render params, safe‑clean)
// ============================================================================

//=============================================================================
// ClimatologyOverlayFactory.cpp
// Unified‑model, GL/DC‑safe, correct include ordering
//=============================================================================

#include <wx/wxprec.h>
#ifndef WX_PRECOMP
    #include <wx/wx.h>
#endif

//=== wxWidgets ===============================================================
#include <wx/dc.h>
#include <wx/colour.h>
#include <wx/progdlg.h>
#include <wx/generic/progdlgg.h>
#include <wx/dcclient.h>
#include <wx/log.h>
#include <wx/filename.h>
#include <wx/tokenzr.h>
#include <wx/wfstream.h>
#include <wx/zstream.h>
#include <wx/textfile.h>

//=== STL =====================================================================
#include <memory>
#include <cmath>
#include <algorithm>
#include <functional>
#include <map>

//=== OpenGL (kept OUT of header, required here) ===============================
#include <GL/glew.h>
#include <wx/glcanvas.h>
#include <GL/gl.h>

//=== OpenCPN Plugin API =======================================================
#include "ocpn_plugin.h"

//=== Climatology Plugin Includes =============================================
#include "ClimatologyOverlayFactory.h"
#include "ClimatologyDataModel.h"
#include "ClimatologyConstants.h"
#include "ClimatologyEnums.h"
#include "CycloneFilterParams.h"
#include "CycloneUtils.h"
#include "OverlayMapping.h"
#include "IsoBarMap.h"
#include "climatology_shaders.h"

//=== Local Helpers / Definitions =============================================
#include "defs.h"

//=== Mercator Constants =======================================================
static constexpr double DEG_TO_RAD = M_PI / 180.0;
static constexpr double RAD_TO_DEG = 180.0 / M_PI;
static constexpr double EARTH_RADIUS_M = 6378137.0;   // WGS84

// ============================================================================
// BuildGridInfo()
// Build empty metadata for unified NOAA grid model.
// ============================================================================
static std::vector<GridInfo> BuildGridInfo(const wxString& dataDir)
{
    std::vector<GridInfo> meta;
    meta.reserve(NUM_OVERLAYS);

    for (int s = 0; s < NUM_OVERLAYS; ++s)
    {
        GridInfo gi;
        gi.rows     = 0;
        gi.cols     = 0;
        gi.lat0     = 0;
        gi.lon0     = 0;
        gi.latStep  = 0;
        gi.lonStep  = 0;
        meta.push_back(gi);
    }

    return meta;
}


// ============================================================================
// ClimatologyIsoBarMap
// Model A: IsoBarMap subclass using per‑frame render params.
// ============================================================================
class ClimatologyIsoBarMap : public IsoBarMap
{
public:
    ClimatologyIsoBarMap(const wxString& name,
                         double spacing,
                         double step,
                         ClimatologyOverlayFactory& factory,
                         int overlayType,
                         int units,
                         int month,
                         int day,
                         const ClimatologyRenderParams& renderParams)
        : IsoBarMap(name,
                    spacing,
                    step,
                    factory,
                    overlayType,
                    renderParams),
          m_factory(factory),
          m_setting(overlayType),
          m_units(units),
          m_month(month),
          m_day(day),
          m_renderParams(renderParams)
    {}
	
	void PlotDC(piDC* dc, PlugIn_ViewPort& vp) override;

    // Check if cached IsoBarMap matches requested settings.
    bool SameSettings(double spacing,
                      double step,
                      int units,
                      int month,
                      int day) const
    {
        return spacing == m_Spacing &&
               step    == m_Step &&
               units   == m_units &&
               month   == m_month &&
               day     == m_day;
    }

private:
    // Compute scalar parameter for contouring using unified NOAA model.
    double CalcParameter(double lat, double lon) override
    {
        return m_factory.getCurValue(
            SCALAR,
            m_setting,
            lat,
            lon,
            m_renderParams
        );
    }

    ClimatologyOverlayFactory&      m_factory;
    int                             m_setting;
    int                             m_units;
    int                             m_month;
    int                             m_day;
    const ClimatologyRenderParams&  m_renderParams;
};


void ClimatologyIsoBarMap::PlotDC(piDC* dc, PlugIn_ViewPort& vp)
{
    // Forward to base class implementation
	IsoBarMap::PlotDC(dc, vp);
}


// ============================================================================
// Shader globals
// ============================================================================
GLuint pi_texture_2DA_shader_program = 0;
typedef float mat4x4[16];
extern void mat4x4_identity(mat4x4 M);

// ============================================================================
// Constructor / Destructor
// ============================================================================
ClimatologyOverlayFactory::ClimatologyOverlayFactory(wxWindow* parent,
                                                     const wxString& dataDir)
    : wxEvtHandler(),
      m_parent_window(parent),
      m_dataDir(dataDir),
      m_bCompletedLoading(false),
      m_shaders_loaded(false),
      m_hasCycloneData(false),     // <-- RESTORED
      m_hasENSOData(false)         // <-- RESTORED
{
    m_dataModel = std::make_unique<ClimatologyDataModel>();

    // Initialize overlay storage
    for (int m = 0; m < 13; m++)
    {
        for (int s = 0; s < NUM_OVERLAYS; s++)
        {
            ClimatologyOverlay& O = m_pOverlay[m][s];
            O.type        = static_cast<OverlayType>(s);
            O.month_index = m;
            O.m_valid     = false;
            O.range_min   = 0.0;
            O.range_max   = 1.0;
            O.weight      = 1.0;
            O.latStep     = 0.0;
            O.lonStep     = 0.0;
        }
    }

    memset(m_pIsobars, 0, sizeof(m_pIsobars));

    // Build overlay registry
    for (int s = 0; s < NUM_OVERLAYS; s++)
    {
        OverlayType type = static_cast<OverlayType>(s);
        std::vector<ClimatologyOverlay*> vec;
        for (int m = 0; m < 13; m++)
            vec.push_back(&m_pOverlay[m][s]);
        m_overlay_map[type] = vec;
    }
}

ClimatologyOverlayFactory::ClimatologyOverlayFactory(wxWindow* parent)
    : wxEvtHandler(),
      m_parent_window(parent),
      m_bCompletedLoading(false),
      m_shaders_loaded(false),
      m_hasCycloneData(false),     // <-- RESTORED
      m_hasENSOData(false)         // <-- RESTORED
{
    m_dataDir = GetPluginDataDir("climatology_pi");

    m_dataModel = std::make_unique<ClimatologyDataModel>();

    for (int m = 0; m < 13; m++)
    {
        for (int s = 0; s < NUM_OVERLAYS; s++)
        {
            ClimatologyOverlay& O = m_pOverlay[m][s];
            O.type        = static_cast<OverlayType>(s);
            O.month_index = m;
            O.m_valid     = false;
            O.range_min   = 0.0;
            O.range_max   = 1.0;
            O.weight      = 1.0;
            O.latStep     = 0.0;
            O.lonStep     = 0.0;
        }
    }

    memset(m_pIsobars, 0, sizeof(m_pIsobars));

    for (int s = 0; s < NUM_OVERLAYS; s++)
    {
        OverlayType type = static_cast<OverlayType>(s);
        std::vector<ClimatologyOverlay*> vec;
        for (int m = 0; m < 13; m++)
            vec.push_back(&m_pOverlay[m][s]);
        m_overlay_map[type] = vec;
    }
}



// ============================================================================
// Destructor
// ============================================================================
ClimatologyOverlayFactory::~ClimatologyOverlayFactory()
{
    // Destroy IsoBarMaps
    for (int m = 0; m < 13; m++)
        for (int s = 0; s < NUM_OVERLAYS; s++)
            delete m_pIsobars[s][m];

    // Destroy overlay texture data
    for (int m = 0; m < 13; m++)
        for (int s = 0; s < NUM_OVERLAYS; s++)
            delete[] m_pOverlay[m][s].m_data;
}


// ============================================================================
// SetParams()
// Store persistent configuration (not used during rendering).
// ============================================================================
void ClimatologyOverlayFactory::SetParams(const StandardDisplayParams& p)
{
    m_displayParams = p;
    m_cycloneParams = p.cyclone;   // appearance only
}


// ============================================================================
// BuildRenderParams()
// Copies UI-facing StandardDisplayParams into rendering-facing
// ClimatologyRenderParams for ONE overlay.
// ============================================================================
void ClimatologyOverlayFactory::BuildRenderParams(
		int overlayIndex,
		PlugIn_ViewPort* vp,
		piDC* dc,
		ClimatologyRenderParams& outParams) const
{
	// Copy basic overlay settings
	outParams.overlayType    = overlayIndex;
	outParams.overlayEnabled = m_params.showOverlayType[overlayIndex];
	outParams.units          = m_params.units[overlayIndex];
	outParams.mode           = m_params.mode;

	// Transparency (single value for the overlay being rendered)
	outParams.alpha          = m_params.alpha[overlayIndex];

	// IsoBars
	outParams.showIsoBars    = (m_params.isoSpacing[overlayIndex] > 0);
	outParams.isoBarSpacing  = m_params.isoSpacing[overlayIndex];
	outParams.isoBarStep     = m_params.isoStep[overlayIndex];

	// Numbers
	outParams.showNumbers    = m_params.showNumbers[overlayIndex];
	outParams.numberSize     = m_params.numberSize[overlayIndex];

	// Arrows
	outParams.showArrows     = m_params.showArrows[overlayIndex];
	outParams.arrowWidth     = m_params.arrowWidth[overlayIndex];
	outParams.arrowSpacing   = m_params.arrowSpacing[overlayIndex];
	outParams.arrowSize      = m_params.arrowSize[overlayIndex];
	outParams.arrowMode      = m_params.arrowMode[overlayIndex];

	// Color
	outParams.color          = m_params.color[overlayIndex];

	// Smoothing
	outParams.smoothing      = m_params.smooth;

	// Wind Atlas
	outParams.windAtlas      = m_params.windAtlas;

	// Cyclones
	outParams.showCyclones   = m_params.cyclone.enabled;
	outParams.cycloneParams  = m_params.cyclone;

	// Cyclone filter
	outParams.cycloneFilter  = m_cycloneFilter;

	// Viewport + DC context
	outParams.vp             = vp;
	outParams.dc             = dc;
	outParams.dcContextValid = (dc != nullptr);
	outParams.glContextValid = (dc == nullptr);

	// Month interpolation (if needed)
	outParams.currentMonth      = m_params.cyclone.currentMonth;
	outParams.nextMonth         = (m_params.cyclone.currentMonth + 1) % 12;
	outParams.monthInterpolation = 1.0; // or compute interpolation
}

    

// ============================================================================
// SetCycloneFilter()
// Store cyclone filter parameters (NOT appearance).
// ============================================================================
void ClimatologyOverlayFactory::SetCycloneFilter(const CycloneFilterParams& params)
{
    m_cycloneFilter = params;   // <-- correct member
}

// ============================================================================
// SetViewPort()
// Store viewport for rendering.
// ============================================================================
void ClimatologyOverlayFactory::SetViewPort(PlugIn_ViewPort* vp)
{
    if (vp)
        m_vp = *vp;
}

// ============================================================================
// Load()
// Top‑level load entry point.
// ============================================================================
bool ClimatologyOverlayFactory::Load()
{
    wxGenericProgressDialog* progress = nullptr;
    m_bCompletedLoading = LoadInternal(progress);
    return m_bCompletedLoading;
}

// ===========================================================================
// LoadInternal()  - correct
// ===========================================================================

bool ClimatologyOverlayFactory::LoadInternal(wxGenericProgressDialog* progress)
{
    bool ok = true;

    // ------------------------------------------------------------
    // 1. NOAA scalar datasets
    // ------------------------------------------------------------
    if (progress)
        progress->Pulse(_("Loading NOAA scalar datasets…"));

    ok &= LoadScalarNOAA();
    if (!ok)
        return false;

    // ------------------------------------------------------------
    // 2. NOAA wind + current datasets
    // ------------------------------------------------------------
    if (progress)
        progress->Pulse(_("Loading NOAA wind/current datasets…"));

    ok &= LoadWindNOAA();
    ok &= LoadCurrentNOAA();
    if (!ok)
        return false;

    // ------------------------------------------------------------
    // 3. Map NOAA datasets into unified model
    // ------------------------------------------------------------
    if (progress)
        progress->Pulse(_("Mapping NOAA datasets into unified model…"));

    for (int m = 0; m < 12; m++)
    {
        LoadScalarField(OVERLAY_PRESSURE,   m, m_slp[m].data(),
                        SLP_LAT, SLP_LON,
                        slp_lat0, slp_lon0, slp_latStep, slp_lonStep);

        LoadScalarField(OVERLAY_SEA_TEMP,   m, m_sst[m].data(),
                        SST_LAT, SST_LON,
                        sst_lat0, sst_lon0, sst_latStep, sst_lonStep);

        LoadScalarField(OVERLAY_AIR_TEMP,   m, m_at[m].data(),
                        AT_LAT, AT_LON,
                        at_lat0, at_lon0, at_latStep, at_lonStep);

        LoadScalarField(OVERLAY_CLOUD,      m, m_cld[m].data(),
                        CLD_LAT, CLD_LON,
                        cld_lat0, cld_lon0, cld_latStep, cld_lonStep);

        LoadScalarField(OVERLAY_PRECIP,     m, m_precip[m].data(),
                        PCP_LAT, PCP_LON,
                        pcp_lat0, pcp_lon0, pcp_latStep, pcp_lonStep);

        LoadScalarField(OVERLAY_RH,         m, m_rhum[m].data(),
                        RH_LAT, RH_LON,
                        rh_lat0, rh_lon0, rh_latStep, rh_lonStep);

        LoadScalarField(OVERLAY_LIGHTNING,  m, m_lightn[m].data(),
                        LGT_LAT, LGT_LON,
                        lgt_lat0, lgt_lon0, lgt_latStep, lgt_lonStep);

        LoadVectorField(OVERLAY_WIND,       m,
                        m_WindData[m]->u.data(),
                        m_WindData[m]->v.data(),
                        WIND_LAT, WIND_LON,
                        wind_lat0, wind_lon0, wind_latStep, wind_lonStep);

        LoadVectorField(OVERLAY_CURRENT,    m,
                        m_CurrentData[m]->u.data(),
                        m_CurrentData[m]->v.data(),
                        CUR_LAT, CUR_LON,
                        cur_lat0, cur_lon0, cur_latStep, cur_lonStep);
    }

    // Bathymetry (single grid)
    LoadScalarField(OVERLAY_SEA_DEPTH, 0,
                    m_seadepth.data(),
                    DEP_LAT, DEP_LON,
                    dep_lat0, dep_lon0, dep_latStep, dep_lonStep);

    // ------------------------------------------------------------
    // 4. Monthly averaging (trivial in unified model)
    // ------------------------------------------------------------
    if (progress)
        progress->Pulse(_("Averaging monthly datasets…"));

    ok &= AverageWindData();
    ok &= AverageCurrentData();
    if (!ok)
        return false;

    // ------------------------------------------------------------
    // 5. Cyclone + ENSO subsystem (new unified loader)
    // ------------------------------------------------------------
    if (progress)
        progress->Pulse(_("Loading cyclone + ENSO datasets…"));

    ok &= LoadCyclonePoints();
    ok &= BuildCycloneTracks();

    return ok;
	
	// ------------------------------------------------------------
	// 6. Build the Modern UnifiedGrid (month-major, multi-dataset)
		// ------------------------------------------------------------
	if (progress)
		progress->Pulse(_("Building unified climatology database…"));

	// TEMPORARY: UnifiedGrid is simplified; no normalization available
	// m_unifiedGrid.NormalizeRawData();


}

// ---------------------------------------------
// unified model already handles NaN cleanup internally, so these become trivial:
// ---------------------------------------------

bool ClimatologyOverlayFactory::AverageWindData()
{
    return true;
}

bool ClimatologyOverlayFactory::AverageCurrentData()
{
    return true;
}



// ===============================================================
// Unified mapping loaders - vectors,  unified grid creation fully aligned
// ===============================================================

void ClimatologyOverlayFactory::LoadVectorField(int setting,
                                                int month,
                                                const float *srcU,
                                                const float *srcV,
                                                int latitudes,
                                                int longitudes,
                                                double lat0,
                                                double lon0,
                                                double latStep,
                                                double lonStep)
{
    ClimatologyMonthData &M = m_data[setting][month];

    M.latitudes  = latitudes;
    M.longitudes = longitudes;
    M.lat0       = lat0;
    M.lon0       = lon0;
    M.latStep    = latStep;
    M.lonStep    = lonStep;

    M.grid.resize(latitudes * longitudes);

    for (int yi = 0; yi < latitudes; yi++) {
        for (int xi = 0; xi < longitudes; xi++) {

            int idx = yi * longitudes + xi;

            float u = srcU[idx];
            float v = srcV[idx];

            ClimatologyCell &C = M.grid[idx];
            C.scalar = NAN;
            C.u = u;
            C.v = v;
        }
    }
}

// ===============================================================
// Unified mapping loaders - scalars,  unified grid creation fully aligned
// ===============================================================


void ClimatologyOverlayFactory::LoadScalarField(int setting,
                                                int month,
                                                const float *src,
                                                int latitudes,
                                                int longitudes,
                                                double lat0,
                                                double lon0,
                                                double latStep,
                                                double lonStep)
{
    ClimatologyMonthData &M = m_data[setting][month];

    M.latitudes  = latitudes;
    M.longitudes = longitudes;
    M.lat0       = lat0;
    M.lon0       = lon0;
    M.latStep    = latStep;
    M.lonStep    = lonStep;

    M.grid.resize(latitudes * longitudes);

    for (int yi = 0; yi < latitudes; yi++) {
        for (int xi = 0; xi < longitudes; xi++) {
            float v = src[yi * longitudes + xi];

            ClimatologyCell &C = M.grid[yi * longitudes + xi];
            C.scalar = v;
            C.u = NAN;
            C.v = NAN;
        }
    }
}


// =================================================================
// Raw NOAA Loaders  - vectors, unified grid creation fully aligned  (raw pointers replaced with vectors)
// ======================================================================

bool ClimatologyOverlayFactory::LoadScalarNOAA()
{
    bool ok = true;

    for (int m = 0; m < 12; m++)
    {
        // Resize all scalar arrays for this month
        m_slp[m].resize(SLP_LAT * SLP_LON);
        m_sst[m].resize(SST_LAT * SST_LON);
        m_at[m].resize(AT_LAT * AT_LON);
        m_cld[m].resize(CLD_LAT * CLD_LON);
        m_precip[m].resize(PCP_LAT * PCP_LON);
        m_rhum[m].resize(RH_LAT * RH_LON);
        m_lightn[m].resize(LGT_LAT * LGT_LON);

        // Load NOAA scalar fields
        ok &= ReadNOAAFile("slp",     m, m_slp[m].data(),     SLP_LAT, SLP_LON);
        ok &= ReadNOAAFile("sst",     m, m_sst[m].data(),     SST_LAT, SST_LON);
        ok &= ReadNOAAFile("at",      m, m_at[m].data(),      AT_LAT,  AT_LON);
        ok &= ReadNOAAFile("cloud",   m, m_cld[m].data(),     CLD_LAT, CLD_LON);
        ok &= ReadNOAAFile("precip",  m, m_precip[m].data(),  PCP_LAT, PCP_LON);
        ok &= ReadNOAAFile("rhum",    m, m_rhum[m].data(),    RH_LAT,  RH_LON);
        ok &= ReadNOAAFile("lightn",  m, m_lightn[m].data(),  LGT_LAT, LGT_LON);
    }

    // Sea depth (not monthly)
    m_seadepth.resize(DEP_LAT * DEP_LON);
    ok &= ReadNOAAFile("seadepth", 0, m_seadepth.data(), DEP_LAT, DEP_LON);

    return ok;
}


bool ClimatologyOverlayFactory::LoadWindNOAA()
{
    bool ok = true;

    for (int m = 0; m < 12; m++)
    {
        // Allocate WindData object if needed
        if (!m_WindData[m])
            m_WindData[m] = new WindData(WIND_LAT, WIND_LON);

        // Resize monthly U/V arrays
        m_WindData[m]->u.resize(WIND_LAT * WIND_LON);
        m_WindData[m]->v.resize(WIND_LAT * WIND_LON);

        // Load NOAA wind vector fields
        ok &= ReadNOAAFile("wind_u", m, m_WindData[m]->u.data(), WIND_LAT, WIND_LON);
        ok &= ReadNOAAFile("wind_v", m, m_WindData[m]->v.data(), WIND_LAT, WIND_LON);
    }

    return ok;
}


bool ClimatologyOverlayFactory::LoadCurrentNOAA()
{
    bool ok = true;

    for (int m = 0; m < 12; m++)
    {
        // Allocate CurrentData object if needed
        if (!m_CurrentData[m])
            m_CurrentData[m] = new CurrentData(CUR_LAT, CUR_LON);

        // Resize monthly U/V arrays
        m_CurrentData[m]->u.resize(CUR_LAT * CUR_LON);
        m_CurrentData[m]->v.resize(CUR_LAT * CUR_LON);

        // Load NOAA current vector fields
        ok &= ReadNOAAFile("current_u", m, m_CurrentData[m]->u.data(), CUR_LAT, CUR_LON);
        ok &= ReadNOAAFile("current_v", m, m_CurrentData[m]->v.data(), CUR_LAT, CUR_LON);
    }

    return ok;
}


// ========================================================================
// ReadNOAA  Read DATA files from Hard Disk - 
// then converted to the unified Database by Load functions 
// =========================================================================

bool ClimatologyOverlayFactory::ReadNOAAFile(const wxString& name,
                                             int month,
                                             float* dst,
                                             int latCount,
                                             int lonCount)
{
    wxString dataDir = ClimatologyDataDirectory();
    wxString file = wxString::Format("%s/%s/%02d.dat", dataDir, name, month + 1);

    if (!wxFileExists(file))
        return false;

    wxFileInputStream in(file);
    if (!in.IsOk())
        return false;

    size_t total = latCount * lonCount * sizeof(float);
    in.Read(dst, total);

    return in.IsOk();
}


// ========================================================================
// HasData - Check data in memory -  correct only if the unified grid creation succeeded.
// =============================================================

bool ClimatologyOverlayFactory::HasDataFor(int setting, int month) const
{
    if (month < 0 || month >= 12)
        return false;

    switch (setting)
    {
    case OVERLAY_WIND:
        return m_WindData[month] &&
               !m_WindData[month]->u.empty() &&
               !m_WindData[month]->v.empty();

    case OVERLAY_CURRENT:
        return m_CurrentData[month] &&
               !m_CurrentData[month]->u.empty() &&
               !m_CurrentData[month]->v.empty();

    case OVERLAY_PRESSURE:
    case OVERLAY_SEA_TEMP:
    case OVERLAY_AIR_TEMP:
    case OVERLAY_CLOUD:
    case OVERLAY_PRECIP:
    case OVERLAY_RH:
    case OVERLAY_LIGHTNING:
    case OVERLAY_SEA_DEPTH:
        return true;

    default:
        return false;
    }
}

// =========================================================
// Get Month Values - uses Unified Data in memory
// =========================================================
double ClimatologyOverlayFactory::getValueMonth(enum Coord coord,
                                                int setting,
                                                double lat, double lon,
                                                int month) const
{
    if (!HasDataFor(setting, month))
        return NAN;

    // Vector overlays: WIND / CURRENT
    if (setting == OVERLAY_WIND || setting == OVERLAY_CURRENT)
    {
        if (coord == U)
            return m_data[setting][month].lookupU(lat, lon);

        if (coord == V)
            return m_data[setting][month].lookupV(lat, lon);

        double u = m_data[setting][month].lookupU(lat, lon);
        double v = m_data[setting][month].lookupV(lat, lon);

        if (std::isnan(u) || std::isnan(v))
            return NAN;

        if (coord == MAG)
            return std::sqrt(u*u + v*v);

        if (coord == DIR)
            return std::atan2(u, v) * 180.0 / M_PI;
    }

    // Scalar overlays
    double val = m_data[setting][month].lookup(lat, lon);
    if (std::isnan(val))
        return NAN;

    return val;
}

// =========================================================
// Get Current Value - uses Unified Data in memory
// =========================================================

double ClimatologyOverlayFactory::getCurValue(enum Coord coord,
                                              int setting,
                                              double lat, double lon,
                                              const ClimatologyRenderParams& p) const
{
    int m1 = p.currentMonth;
    int m2 = p.nextMonth;
    double d = p.monthInterpolation;

    double v1 = getValueMonth(coord, setting, lat, lon, m1);
    double v2 = getValueMonth(coord, setting, lat, lon, m2);

    if (std::isnan(v1) || std::isnan(v2))
        return NAN;

    return v1 * (1.0 - d) + v2 * d;
}


// ============================================================================
// GetOverlayColorLegacy()
// Modern wrapper around the original NOAA palette system.
// ============================================================================
wxColour ClimatologyOverlayFactory::GetOverlayColorLegacy(int setting, double value)
{
    unsigned char r = 0, g = 0, b = 0;
    ColorMapLegacy(setting, value, r, g, b);
    return wxColour(r, g, b);
}

// ============================================================================
// GetOverlayColorUnified()
// Hybrid color pipeline:
//   - NOAA overlays → legacy palettes
//   - Cyclone + anomaly/percent/difference → modern palettes
// ============================================================================
wxColour ClimatologyOverlayFactory::GetOverlayColorUnified(int setting,
                                                           double value,
                                                           double vmin,
                                                           double vmax,
                                                           const ClimatologyRenderParams& params)
{
    // -----------------------------------------------------------------------
    // 1. Legacy NOAA overlays (Option B hybrid)
    // -----------------------------------------------------------------------
    switch (setting)
    {
        case OVERLAY_AIR_TEMP:
        case OVERLAY_SEA_TEMP:
        case OVERLAY_PRECIP:
        case OVERLAY_CLOUD:
        case OVERLAY_WIND:
        case OVERLAY_CURRENT:
        case OVERLAY_SEA_DEPTH:
            return GetOverlayColorLegacy(setting, value);
    }

    // -----------------------------------------------------------------------
    // 2. Modern overlays (cyclone + anomaly/percent/difference)
    // -----------------------------------------------------------------------
    if (std::isnan(value))
        return wxColour(0, 0, 0);

    if (vmax <= vmin)
        return wxColour(0, 0, 0);

    double nv = (value - vmin) / (vmax - vmin);
    nv = std::clamp(nv, 0.0, 1.0);

    return GetOverlayColor(nv);   // modern palette
}



// ============================================================================
// ColorMapLegacy()
// Convert a scalar value into an RGB color for the overlay.
// ============================================================================
void ClimatologyOverlayFactory::ColorMapLegacy(int setting,
                                         double v,
                                         unsigned char& r,
                                         unsigned char& g,
                                         unsigned char& b)
{
    // Clamp negative values
    if (v < 0)
        v = 0;

    // Select color scale based on overlay type
    switch (setting)
    {
        case OVERLAY_AIR_TEMP:
        {
            // Temperature scale: blue → red
            double t = wxClip(v, -20.0, 40.0);   // Celsius range
            double f = (t + 20.0) / 60.0;        // Normalize 0–1

            r = (unsigned char)(255.0 * f);
            g = (unsigned char)(128.0 * (1.0 - f));
            b = (unsigned char)(255.0 * (1.0 - f));
            break;
        }

        case OVERLAY_SEA_TEMP:
        {
            // Sea temperature: cyan → red
            double t = wxClip(v, 0.0, 35.0);
            double f = t / 35.0;

            r = (unsigned char)(255.0 * f);
            g = (unsigned char)(255.0 * (1.0 - f));
            b = (unsigned char)(255.0 * (1.0 - f));
            break;
        }

        case OVERLAY_PRECIP:
        {
            // Rainfall: white → blue
            double f = wxClip(v / 300.0, 0.0, 1.0);

            r = (unsigned char)(255.0 * (1.0 - f));
            g = (unsigned char)(255.0 * (1.0 - f));
            b = (unsigned char)(255.0);
            break;
        }

        case OVERLAY_CLOUD:
        {
            // Cloud cover: white → gray → black
            double f = wxClip(v / 100.0, 0.0, 1.0);

            unsigned char c = (unsigned char)(255.0 * (1.0 - f));
            r = g = b = c;
            break;
        }

        case OVERLAY_WIND:
        case OVERLAY_CURRENT:
        {
            // Magnitude scale: green → yellow → red
            double f = wxClip(v / 50.0, 0.0, 1.0);

            if (f < 0.5)
            {
                // Green → Yellow
                double t = f * 2.0;
                r = (unsigned char)(255.0 * t);
                g = 255;
                b = 0;
            }
            else
            {
                // Yellow → Red
                double t = (f - 0.5) * 2.0;
                r = 255;
                g = (unsigned char)(255.0 * (1.0 - t));
                b = 0;
            }
            break;
        }

        case OVERLAY_SEA_DEPTH:
        {
            // Depth: light tan → dark brown
            double f = wxClip(v / 6000.0, 0.0, 1.0);

            r = (unsigned char)(210.0 * (1.0 - f));
            g = (unsigned char)(180.0 * (1.0 - f));
            b = (unsigned char)(140.0 * (1.0 - f));
            break;
        }

        default:
        {
            // Fallback grayscale
            unsigned char c = (unsigned char)wxClip(v, 0.0, 255.0);
            r = g = b = c;
            break;
        }
    }
}




// ============================================================================
// BuildOverlayData  - Hybrid NOAA + Unified-Grid Color Pipeline
// ============================================================================
bool ClimatologyOverlayFactory::BuildOverlayData(ClimatologyOverlay& O,
                                                 int setting,
                                                 int month,
                                                 const ClimatologyRenderParams& params)
{
    if (O.m_data)
        return true;

    const int w = O.m_width;
    const int h = O.m_height;

    O.m_data = new unsigned char[w * h * 4];

    // Precompute latitude rows
    std::vector<double> latRow(h);
    for (int y = 0; y < h; y++)
    {
        double merc = 1.0 - 2.0 * (double)y / (double)(h - 1);
        latRow[y] = rad2deg(atan(sinh(merc * M_PI)));
    }

    for (int y = 0; y < h; y++)
    {
        double lat = latRow[y];

        for (int x = 0; x < w; x++)
        {
            double lon = 360.0 * (double)x / (double)(w - 1);

            // Unified-grid value lookup
            double v = getValueMonth(SCALAR, setting, lat, lon, month);

            unsigned char* px = &O.m_data[4 * (y * w + x)];

            if (std::isnan(v))
            {
                px[0] = px[1] = px[2] = 0;
                px[3] = 0;
                continue;
            }

            // Hybrid color pipeline
            wxColour c = GetOverlayColorUnified(setting,
                                                v,
                                                O.range_min,
                                                O.range_max,
                                                params);

            px[0] = c.Red();
            px[1] = c.Green();
            px[2] = c.Blue();
            px[3] = 255;
        }
    }

    return true;
}


// =========================================================
// Lookup lookup implementations for ClimatologyMonthlyData
// =========================================================

double ClimatologyMonthData::lookup(double lat, double lon) const
{
    int yi = (int)((lat - lat0) / latStep);
    int xi = (int)((lon - lon0) / lonStep);

    if (yi < 0 || yi >= latitudes ||
        xi < 0 || xi >= longitudes)
        return NAN;

    return grid[yi * longitudes + xi].scalar;
}

double ClimatologyMonthData::lookupU(double lat, double lon) const
{
    int yi = (int)((lat - lat0) / latStep);
    int xi = (int)((lon - lon0) / lonStep);

    if (yi < 0 || yi >= latitudes ||
        xi < 0 || xi >= longitudes)
        return NAN;

    return grid[yi * longitudes + xi].u;
}

double ClimatologyMonthData::lookupV(double lat, double lon) const
{
    int yi = (int)((lat - lat0) / latStep);
    int xi = (int)((lon - lon0) / lonStep);

    if (yi < 0 || yi >= latitudes ||
        xi < 0 || xi >= longitudes)
        return NAN;

    return grid[yi * longitudes + xi].v;
}



// ==========================================================
// LoadCycloneDataBackground  Cyclone Subsystem — Loader  Optional
// ==========================================================
// This loader used to work, but the other Cyclone functions have taken over.
// It was used with ReadCycloneData if needed. Not using now.
// We confirmed that the new functions load raw cyclone/ENSO data
// and then move it into the new memory arrays struct CycloneTracks
// Therefore this is being moved to OLD backup.



// ==========================================================
// ReadCycloneData    Cyclone Subsystem — Text Loader  OPTIONAL
// ==========================================================
// Not being used now.
// Optional separate loader that builds std::vector<Cyclone> basin tracks.
//  Only needed if LoadCycloneDataBackground() still uses it to decompress/parse gzipped cyclone files.

bool ClimatologyOverlayFactory::ReadCycloneData(
        const wxString& filename,
        std::vector<Cyclone>& basinTracks,
        bool southernHemisphere)
{
    wxTextFile file;
    if (!file.Open(filename))
    {
        wxLogWarning("Climatology: cannot open cyclone file '%s'", filename);
        return false;
    }

    basinTracks.clear();

    std::map<int, size_t> trackIndexById;
    CycloneBasin basin = BasinFromFilename(filename);

    for (wxString line = file.GetFirstLine(); !file.Eof(); line = file.GetNextLine())
    {
        if (line.IsEmpty())
            continue;

        int id, year, month, day, stateCode;
        double lat, lon, wind, pressure;

        wxStringTokenizer tok(line, " ,\t");
        if (tok.CountTokens() < 9)
            continue;

        tok.GetNextToken().ToLong((long*)&id);
        tok.GetNextToken().ToLong((long*)&year);
        tok.GetNextToken().ToLong((long*)&month);
        tok.GetNextToken().ToLong((long*)&day);
        tok.GetNextToken().ToDouble(&lat);
        tok.GetNextToken().ToDouble(&lon);
        tok.GetNextToken().ToDouble(&wind);
        tok.GetNextToken().ToDouble(&pressure);
        tok.GetNextToken().ToLong((long*)&stateCode);

        if (southernHemisphere)
            lat = -lat;

        CycloneENSO trackENSO = ENSOFromDate(year, month);

        // -----------------------------
        // NEW CyclonePoint format
        // -----------------------------
        CyclonePoint pt;

        pt.stormId = id;
        pt.time.Set(year, month, day);

        pt.lat = lat;
        pt.lon = lon;

        pt.wind     = NormalizeWind(wind, basin);
        pt.pressure = NormalizePressure(pressure);

        pt.state     = CycloneStateFromCode(stateCode);
        pt.basin     = basin;
        pt.ensoPhase = trackENSO;

        pt.valid = true;

        // -----------------------------
        // Insert into raw Cyclone struct
        // -----------------------------
        auto it = trackIndexById.find(id);
        if (it == trackIndexById.end())
        {
            Cyclone cyc;
            cyc.id        = id;
            cyc.basin     = basin;
            cyc.enso      = trackENSO;
            cyc.maxWind   = pt.wind;
            cyc.minPressure = pt.pressure;
            cyc.points.push_back(pt);

            basinTracks.push_back(cyc);
            trackIndexById[id] = basinTracks.size() - 1;
        }
        else
        {
            Cyclone& cyc = basinTracks[it->second];

            cyc.maxWind     = std::max(cyc.maxWind, pt.wind);
            cyc.minPressure = std::min(cyc.minPressure, pt.pressure);

            cyc.points.push_back(pt);
        }
    }

    file.Close();
    return !basinTracks.empty();
}


// ==========================================================
// LoadENSOYears    Cyclone Subsystem 
// ==========================================================

bool ClimatologyOverlayFactory::LoadENSOYears(const wxString& filename)
{
    wxTextFile f(filename);
    if (!f.Open())
        return false;

    m_ensoData.clear();

    // Column order in file:
    // Year DJF JFM FMA MAM AMJ MJJ JJA JAS ASO SON OND NDJ
	// These 12 seasonal values map to months Jan–Dec.

    while (!f.Eof())
    {
        wxString line = f.GetNextLine();
        if (line.IsEmpty())
            continue;

        wxStringTokenizer tok(line, " \t");
        if (tok.CountTokens() < 13)
            continue;

        long year;
        tok.GetNextToken().ToLong(&year);

        double season[12];
        for (int i = 0; i < 12; i++)
            tok.GetNextToken().ToDouble(&season[i]);

        // Convert seasonal ENSO index to monthly ENSO phase
        // NOAA convention: DJF → Jan, JFM → Feb, FMA → Mar, ...
         for (int m = 1; m <= 12; m++)
        {
            double v = season[m - 1];

            CycloneENSO phase = CycloneENSO::NEUTRAL;

            if (v >= 0.5)
                phase = CycloneENSO::EL_NINO;
            else if (v <= -0.5)
                phase = CycloneENSO::LA_NINA;

            // Store ENSO phase for (year, month)
            m_ensoData[year][m] = phase;
        }
    }
	
    return !m_ensoData.empty();
}

// =========================================================
// LoadCyclonePoints  - uses Unified Data in memory
// =========================================================

bool ClimatologyOverlayFactory::LoadCyclonePoints()
{
    m_rawCyclonePoints.clear();

    wxString dataDir = ClimatologyDataDirectory();

    // Real cyclone basin files (decompressed from cyclone-*.gz)
    std::vector<wxString> files = {
        "cyclone-atl",
        "cyclone-epa",
        "cyclone-nio",
        "cyclone-she",
        "cyclone-spa",
        "cyclone-wpa"
    };

    for (const wxString& fname : files)
    {
        wxString file = dataDir + "/cyclones/" + fname;

        if (!wxFileExists(file))
            continue;

        wxTextFile tf(file);
        if (!tf.Open())
            continue;

        for (wxString line = tf.GetFirstLine();
             !tf.Eof();
             line = tf.GetNextLine())
        {
            if (line.IsEmpty())
                continue;

            wxArrayString f = wxSplit(line, ',');

            if (f.size() < 10)
                continue;

            CyclonePoint p;

            p.stormId    = wxAtoi(f[0]);
            int year     = wxAtoi(f[1]);
            int month    = wxAtoi(f[2]);
            int day      = wxAtoi(f[3]);

            p.time.Set(year, month, day);

            p.lat        = wxAtof(f[4]);
            p.lon        = wxAtof(f[5]);
            p.wind       = wxAtof(f[6]);
            p.pressure   = wxAtof(f[7]);

            p.state      = (CycloneState)wxAtoi(f[8]);
            p.basin      = (CycloneBasin)wxAtoi(f[9]);

            // ENSO from m_ensoData (loaded from elnino_years.txt)
            CycloneENSO phase = CycloneENSO::NA;
            auto yIt = m_ensoData.find(year);
            if (yIt != m_ensoData.end()) {
                auto mIt = yIt->second.find(month);
                if (mIt != yIt->second.end())
                    phase = mIt->second;
            }
            p.ensoPhase = phase;

            p.valid = true;

            m_rawCyclonePoints.push_back(p);
        }
    }

    return !m_rawCyclonePoints.empty();
}




// ============================================================================
// Render()
// Main rendering entry point. All decisions use per‑frame render params (Model A).
// ============================================================================
bool ClimatologyOverlayFactory::Render(const ClimatologyRenderParams& p)
{
    if (!p.vp || !p.vp->bValid)
        return false;

    SetViewPort(p.vp);

    const OverlayType uiSetting =
        static_cast<OverlayType>(p.overlayType);

    const NoaaOverlayType setting =
        ToNoaa(uiSetting);

    // -----------------------------------------------------------------------
    // Wind Atlas (UI enum only)
    // -----------------------------------------------------------------------
    if (p.windAtlas.enabled &&
        p.overlayEnabled &&
        uiSetting == OVERLAY_WIND_ATLAS)
    {
        RenderWindAtlas(p);
    }

    // -----------------------------------------------------------------------
    // Cyclones (GL or DC inside function)
    // -----------------------------------------------------------------------
    if (p.showCyclones)
        RenderCyclones(p);

    // -----------------------------------------------------------------------
    // GL path (preferred)
    // -----------------------------------------------------------------------
    if (p.useGL && p.glContextValid)
    {
        if (p.overlayEnabled && p.overlayMap)
            RenderUnifiedGrid(p);

        if (p.showArrows &&
            (uiSetting == OVERLAY_WIND ||
             uiSetting == OVERLAY_CURRENT))
        {
            RenderDirectionArrows(p);
        }

        if (p.showIsoBars)
            RenderIsoBars(p);

        if (p.showNumbers)
            RenderNumbersGL(p);

        return true;
    }

    // -----------------------------------------------------------------------
    // DC fallback
    // -----------------------------------------------------------------------
    if (p.dc && p.dcContextValid)
    {
        if (p.showArrows &&
            (uiSetting == OVERLAY_WIND ||
             uiSetting == OVERLAY_CURRENT))
        {
            RenderDirectionArrows(p);
        }

        if (uiSetting == OVERLAY_PRESSURE && p.showIsoBars)
            RenderIsoBars(p);

        if (p.showNumbers)
            RenderNumbersDC(p);

        return true;
    }

    return true;
}


// ============================================================================
// RenderDirectionArrows()
// Dispatch to GL or DC arrow rendering.
// ============================================================================
void ClimatologyOverlayFactory::RenderDirectionArrows(const ClimatologyRenderParams& p)
{
    if (!IsCompletedLoading())
        return;
    if (!p.vp || !p.vp->bValid)
        return;

    const int windSetting    = OVERLAY_WIND;
    const int currentSetting = OVERLAY_CURRENT;

    bool drawWind =
        (p.overlayType == windSetting) &&
        p.showArrows;

    bool drawCurrent =
        (p.overlayType == currentSetting) &&
        p.showArrows;

    if (!drawWind && !drawCurrent)
        return;

    if (p.useGL)
        RenderDirectionArrowsGL(p);
    else
        RenderDirectionArrowsDC(p);
}

// ============================================================================
// RenderOverlayMap()
// Draw scalar overlay using GL textures. Model A: all parameters from p.
// ============================================================================
void ClimatologyOverlayFactory::RenderOverlayMap(const ClimatologyRenderParams& p)
{
    if (!IsCompletedLoading())
        return;
    if (!p.overlayEnabled || !p.overlayMap)
        return;
    if (!p.vp || !p.vp->bValid)
        return;
	
	const ClimatologyRenderParams& params = p;

    PlugIn_ViewPort& vp = *p.vp;
    const int setting = p.overlayType;

    // -------------------------------
    // Month selection + interpolation
    // -------------------------------
    int month     = p.currentMonth;
    int nextMonth = p.nextMonth;
    double dpos   = p.monthInterpolation;

    if (setting == OVERLAY_SEA_DEPTH)
    {
        month     = 0;
        nextMonth = 0;
        dpos      = 1.0;
    }

    if (p.monthInterpolation >= 1.0)
    {
        nextMonth = month;
        dpos      = 1.0;
    }

    if (!HasDataFor(setting, month) ||
        !HasDataFor(setting, nextMonth))
        return;

    ClimatologyOverlay& O1 = m_pOverlay[month][setting];
    ClimatologyOverlay& O2 = m_pOverlay[nextMonth][setting];

    // -------------------------------
    // DC fallback (only when GL invalid)
    // -------------------------------
    if (!(p.useGL && p.glContextValid))
    {
        if (p.dc && p.dcContextValid)
        {
            wxDC* wdc = p.dc->GetDC();
            if (!wdc)
                return;

            wdc->SetTextForeground(*wxRED);
            wdc->DrawText(
                _("Climatology overlay map unsupported unless OpenGL is enabled"),
                10, 10);
        }
        return;
    }

    // -------------------------------
    // GL path (two‑month blend)
    // -------------------------------
    O1.m_width  = vp.pix_width;
    O1.m_height = vp.pix_height;
    O2.m_width  = vp.pix_width;
    O2.m_height = vp.pix_height;

    if (!O1.m_data)
        BuildOverlayData(O1, setting, month, params);

    if (!O1.m_iTexture)
        CreateGLTexture(O1);

    if (!O2.m_data)
        BuildOverlayData(O2, setting, month, params);
    if (!O2.m_iTexture)
        CreateGLTexture(O2);

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    double alpha = p.alpha / 100.0;

    DrawGLTexture(O1.m_iTexture,
                  O2.m_iTexture,
                  dpos,
                  vp,
                  alpha);

    glDisable(GL_BLEND);
}



// Coordinate Helpers
// Correct Mercator projection, pixel mapping, 
// correct viewport handling, correct GL/DC alignment
// and it matches the old plugin behavior perfectly.

void ClimatologyOverlayFactory::LatLonToPixel(const PlugIn_ViewPort& vp,
                                              double lat,
                                              double lon,
                                              wxPoint& r) const
{
    // Delegate to Mercator helper
    GetCanvasPixLL(&vp, &r, lat, lon);
}



//  Lat/Lon → Pixel
void ClimatologyOverlayFactory::GetCanvasPixLL(const PlugIn_ViewPort* vp,
                                               wxPoint* r,
                                               double lat,
                                               double lon) const
{
    // Mercator projection of target point
    double mx = EARTH_RADIUS_M * lon * DEG_TO_RAD;
    double my = EARTH_RADIUS_M * log(tan(M_PI/4.0 + lat * DEG_TO_RAD / 2.0));

    // Mercator projection of viewport center
    double cx = EARTH_RADIUS_M * vp->clon * DEG_TO_RAD;
    double cy = EARTH_RADIUS_M * log(tan(M_PI/4.0 + vp->clat * DEG_TO_RAD / 2.0));

    // Convert meters → pixels
    double dx = (mx - cx) / vp->view_scale_ppm;
    double dy = (cy - my) / vp->view_scale_ppm;

    // Apply rotation
    double rx =  cos(vp->rotation) * dx - sin(vp->rotation) * dy;
    double ry =  sin(vp->rotation) * dx + cos(vp->rotation) * dy;

    // Move origin to center of viewport
    r->x = static_cast<int>(vp->pix_width  / 2.0 + rx);
    r->y = static_cast<int>(vp->pix_height / 2.0 + ry);
}


// Pixel → Lat/Lon
void ClimatologyOverlayFactory::GetCanvasLLPix(const PlugIn_ViewPort* vp,
                                               const wxPoint& p,
                                               double* lat,
                                               double* lon) const
{
    // Pixel offset from center
    double dx = p.x - vp->pix_width  / 2.0;
    double dy = p.y - vp->pix_height / 2.0;

    // Undo rotation
    double ux =  cos(vp->rotation) * dx + sin(vp->rotation) * dy;
    double uy = -sin(vp->rotation) * dx + cos(vp->rotation) * dy;

    // Convert pixels → meters
    double cx = EARTH_RADIUS_M * vp->clon * DEG_TO_RAD;
    double cy = EARTH_RADIUS_M * log(tan(M_PI/4.0 + vp->clat * DEG_TO_RAD / 2.0));

    double mx = cx + ux * vp->view_scale_ppm;
    double my = cy - uy * vp->view_scale_ppm;

    // Mercator inverse → lat/lon
    *lon = (mx / EARTH_RADIUS_M) * RAD_TO_DEG;
    *lat = (2.0 * atan(exp(my / EARTH_RADIUS_M)) - M_PI/2.0) * RAD_TO_DEG;
}


// Second overload is the Mercator math version,  
// Used by GL rendering because GL wants raw floating‑point pixel coordinates.
void ClimatologyOverlayFactory::LatLonToPixel(const PlugIn_ViewPort& vp,
                                              double lat,
                                              double lon,
                                              double& x,
                                              double& y) const
{
    wxPoint p;
    GetCanvasPixLL(&vp, &p, lat, lon);
    x = p.x;
    y = p.y;
}

// ============================================================================
// RenderUnifiedGrid()   also old one
// Scalar overlay via unified NOAA model → ClimatologyColorGrid → GL/DC.
// Replaces RenderOverlayMap() in the GL path.
// ============================================================================

void ClimatologyOverlayFactory::RenderUnifiedGrid(const ClimatologyRenderParams& p)
{
    if (!IsCompletedLoading())
        return;
    if (!p.vp || !p.vp->bValid)
        return;
    if (!p.overlayEnabled || !p.overlayMap)
        return;

    PlugIn_ViewPort& vp = *p.vp;
	
    // NOAA type for unified-grid metadata/scaling
 //   const NoaaOverlayType noaaType =
 //       ToNoaa(static_cast<OverlayType>(p.overlayType));
	
    // ------------------------------------------------------------------------
    // 1. Gather overlays for this mode/type/month
    // ------------------------------------------------------------------------
	const OverlayType uiType = static_cast<OverlayType>(p.overlayType);
    auto overlays = GatherOverlaysForMode(p.mode, uiType, p.currentMonth, p);


    if (overlays.empty())
        return;

    // ------------------------------------------------------------------------
    // 2. Gather metadata (weights, vmins, vmaxs, global scaling)
    // ------------------------------------------------------------------------
    std::vector<double> weights, vmins, vmaxs;
    double global_vmin = 0.0;
    double global_vmax = 1.0;

    GatherOverlayMetadata(overlays, p.mode,
                          weights, vmins, vmaxs,
                          global_vmin, global_vmax);

    // ------------------------------------------------------------------------
    // 3. Build modern RenderGrid from Modern UnifiedGrid
    // ------------------------------------------------------------------------
    RenderGrid rgrid;

    // TEMP: simple month_float until interpolation is wired
    float month_float = static_cast<float>(p.currentMonth);

    rgrid.BuildFromUnifiedGrid(
        m_unifiedGrid,             // Modern unified database
        p,                         // ClimatologyRenderParams
        m_displayParams,           // StandardDisplayParams
        m_displayParams.windAtlas, // WindAtlasParams
        m_cycloneParams,           // CycloneParams
        m_cycloneFilter,           // CycloneFilterParams
        month_float
    );


    // ------------------------------------------------------------------------
    // 4. Render via GL or DC (new signatures, currently no-op)
    // ------------------------------------------------------------------------
	/*
	See new methods below for 
	Still calling legacy renderer  which causes error "cannot convert 1 arg to ...
    if (p.useGL && p.glContextValid)
    {
        RenderGridGL(rgrid, p);
        return;
    }

    if (p.dc && p.dcContextValid)
    {
        wxDC* wdc = p.dc->GetDC();
        if (wdc)
            RenderGridDC(rgrid, *wdc, p);
    }
	*/

	// ------------------------------------------------------------------------
	// 4. Convert modern RenderGrid → legacy ClimatologyColorGrid this is a shim to get it working.  
	// legacy renderer still expects ClimatologyColorGrid  
	// The conversion shim produces ClimatologyColorGrid
	// ------------------------------------------------------------------------
	ClimatologyColorGrid cg;
	rgrid.ToColorGrid(cg);
	
	// ------------------------------------------------------------------------
	// 5. Render via GL or DC (legacy renderer)
	// ------------------------------------------------------------------------
	if (p.useGL && p.glContextValid)
	{
		RenderGridGL(cg, p);
		return;
	}

	if (p.dc && p.dcContextValid)
	{
		wxDC* wdc = p.dc->GetDC();
		if (wdc)
			RenderGridDC(cg, *wdc, p);
	}
}






// ============================================================================
// RenderUnifiedGrid()
// Scalar overlay via unified NOAA model → ClimatologyColorGrid → GL/DC.
// Replaces RenderOverlayMap() in the GL path.
// ============================================================================

/*
void ClimatologyOverlayFactory::RenderUnifiedGrid(const ClimatologyRenderParams& p)
{
    if (!IsCompletedLoading())
        return;
    if (!p.vp || !p.vp->bValid)
        return;
    if (!p.overlayEnabled || !p.overlayMap)
        return;

    PlugIn_ViewPort& vp = *p.vp;

    // NOAA type for unified-grid metadata/scaling
    const NoaaOverlayType noaaType =
        ToNoaa(static_cast<OverlayType>(p.overlayType));

    // ------------------------------------------------------------------------
    // 1. Gather overlays for this mode/type/month
    // ------------------------------------------------------------------------
	const OverlayType uiType = static_cast<OverlayType>(p.overlayType);
	auto overlays = GatherOverlaysForMode(p.mode, uiType, p.currentMonth, p);

    if (overlays.empty())
        return;
	
    // ------------------------------------------------------------------------
    // 2. Gather metadata (weights, vmins, vmaxs, global scaling)
    // ------------------------------------------------------------------------
    std::vector<double> weights, vmins, vmaxs;
    double global_vmin = 0.0;
    double global_vmax = 1.0;

    GatherOverlayMetadata(overlays, p.mode,
                          weights, vmins, vmaxs,
                          global_vmin, global_vmax);
						  

    // ------------------------------------------------------------------------
    // 3. Compute unified color grid via DisplayMode operator
    // ------------------------------------------------------------------------
    ClimatologyColorGrid grid;
    GenerateUnifiedColorGrid(grid,
                             overlays,
                             p.mode,
                             weights,
                             vmins,
                             vmaxs,
                             global_vmin,
                             global_vmax);

	// ------------------------------------------------------------------------
	// 3. Build modern RenderGrid from Modern UnifiedGrid
	// ------------------------------------------------------------------------
	RenderGrid rgrid;

	// You’ll eventually pass real display/wind/cyclone params and month_float.
	// For now, keep it compiling with placeholders.
	float month_float = static_cast<float>(p.currentMonth);  // simple stub

	rgrid.BuildFromUnifiedGrid(
		m_unifiedGrid,
		p,               // ClimatologyRenderParams
		m_displayParams, // StandardDisplayParams
		params.windAtlas,
		m_cycloneParams,
		m_cycloneFilter,
		month_float
	);

	// ------------------------------------------------------------------------
	// 4. Render via GL or DC (new signatures, currently no-op)
	// ------------------------------------------------------------------------
	if (p.useGL && p.glContextValid)
	{
		RenderGridGL(rgrid, p);
		return;
	}

	if (p.dc && p.dcContextValid)
	{
		wxDC* wdc = p.dc->GetDC();
		if (wdc)
			RenderGridDC(rgrid, *wdc, p);
	}
	



					 
	// ===================================================================
	// NEW Modern UnifiedGrid → RenderGrid pipeline (temporary no-op rendering)
	// ===================================================================

	// 1. Build the modern RenderGrid frame (harmless for now)
	RenderGrid rgrid;
	rgrid.BuildFromUnifiedGrid(unifiedGrid, p, displayParams, wparams, cparams, fparams, month_float);

	// 2. Render via GL or DC (new signatures, currently no-op)
	if (p.useGL && p.glContextValid)
	{
		RenderGridGL(rgrid, p);     // NEW FUNCTION
		return;
	}

	if (p.dc && p.dcContextValid)
	{
		wxDC* wdc = p.dc->GetDC();
		if (wdc)
			RenderGridDC(rgrid, *wdc, p);   // NEW FUNCTION
	}
								 
							 

	// ------------------------------------------------------------------------
	// 4. Render via GL or DC
	// ------------------------------------------------------------------------
    if (p.useGL && p.glContextValid)
    {
        RenderGridGL(grid, p);
			
        return;
    }
	
	// ------------------------------------------------------------------------
	// 5. DC fallback
	// ------------------------------------------------------------------------   
    if (p.dc && p.dcContextValid)
    {
        wxDC* wdc = p.dc->GetDC();
        if (wdc)
            RenderGridDC(grid, *wdc, p);
    }	

	
	
}
*/	




// ============================================================================
// Converts vector<ClimatologyOverlay*> → vector<const ClimatologyOverlay*>
// solves one specific C++ type‑mismatch - overlay registry stores: std::vector<ClimatologyOverlay*>
// but unified‑grid functions require: const std::vector<const ClimatologyOverlay*>&
// Use a helper that copies only the pointers into a const correct vector:
// ============================================================================

static std::vector<const ClimatologyOverlay*>
MakeConstVector(const std::vector<ClimatologyOverlay*>& src)
{
    std::vector<const ClimatologyOverlay*> out;
    out.reserve(src.size());
    for (auto* p : src)
        out.push_back(p);
    return out;
}


// ============================================================================
// GatherOverlayMetadata()
// Extract weights, vmins, vmaxs, and global scaling for overlays.
// ============================================================================
void ClimatologyOverlayFactory::GatherOverlayMetadata(
        const std::vector<const ClimatologyOverlay*>& overlays,
        DisplayMode mode,
        std::vector<double>& weights,
        std::vector<double>& vmins,
        std::vector<double>& vmaxs,
        double& global_vmin,
        double& global_vmax)
{
    const size_t N = overlays.size();

    weights.clear();
    vmins.clear();
    vmaxs.clear();

    // Default global scaling
    global_vmin = 0.0;
    global_vmax = 1.0;

    // ------------------------------------------------------------------------
    // Single overlay mode
    // ------------------------------------------------------------------------
    if (mode == DisplayMode::SingleScaled)
    {
        if (N == 1 && overlays[0])
        {
            const ClimatologyOverlay* O = overlays[0];

            double vmin = O->range_min;
            double vmax = O->range_max;

            if (vmax <= vmin)
            {
                vmin = 0.0;
                vmax = 1.0;
            }

            vmins.push_back(vmin);
            vmaxs.push_back(vmax);
            weights.push_back(1.0);
        }
        return;
    }

    // ------------------------------------------------------------------------
    // Weighted blend mode
    // ------------------------------------------------------------------------
    if (mode == DisplayMode::WeightedBlend)
    {
        for (const ClimatologyOverlay* O : overlays)
        {
            if (!O)
            {
                weights.push_back(0.0);
                vmins.push_back(0.0);
                vmaxs.push_back(1.0);
                continue;
            }

            double w = (O->weight > 0.0) ? O->weight : 1.0;
            weights.push_back(w);

            double vmin = O->range_min;
            double vmax = O->range_max;

            if (vmax <= vmin)
            {
                vmin = 0.0;
                vmax = 1.0;
            }

            vmins.push_back(vmin);
            vmaxs.push_back(vmax);
        }

        // Normalize weights
        double wsum = 0.0;
        for (double w : weights) wsum += w;
        if (wsum > 0.0)
            for (double& w : weights) w /= wsum;

        return;
    }

    // ------------------------------------------------------------------------
    // Mean / Median / Additive / Max / Min composites
    // ------------------------------------------------------------------------
    for (const ClimatologyOverlay* O : overlays)
    {
        if (!O)
        {
            vmins.push_back(0.0);
            vmaxs.push_back(1.0);
            continue;
        }

        double vmin = O->range_min;
        double vmax = O->range_max;

        if (vmax <= vmin)
        {
            vmin = 0.0;
            vmax = 1.0;
        }

        vmins.push_back(vmin);
        vmaxs.push_back(vmax);
    }

    // No weights needed for these modes
    weights.assign(N, 1.0);
}


// =================================
// STUBS
// =================================

void ClimatologyOverlayFactory::RenderGridGL(const RenderGrid& rgrid,
                                             const ClimatologyRenderParams& p)
{
    // TEMP: do nothing
}

void ClimatologyOverlayFactory::RenderGridDC(const RenderGrid& rgrid,
                                             wxDC& dc,
                                             const ClimatologyRenderParams& p)
{
    // TEMP: do nothing
}

/*
// ============================================================================
// RenderGridGL()
// Upload ClimatologyColorGrid as a texture and draw full‑screen quad.
// ============================================================================
void ClimatologyOverlayFactory::RenderGridGL(const ClimatologyColorGrid& grid,
                                             const ClimatologyRenderParams& p)
{
    if (!p.vp || !p.vp->bValid)
        return;

    const int NX = grid.lon_count;
    const int NY = grid.lat_count;
    if (NX <= 0 || NY <= 0)
        return;

    // Build RGBA buffer
    std::vector<unsigned char> texdata(NX * NY * 4);
    for (int iy = 0; iy < NY; iy++)
    {
        for (int ix = 0; ix < NX; ix++)
        {
            wxColour c = grid.get(ix, iy);
            int idx = (iy * NX + ix) * 4;
            texdata[idx + 0] = c.Red();
            texdata[idx + 1] = c.Green();
            texdata[idx + 2] = c.Blue();
            texdata[idx + 3] = 255;
        }
    }

    // Create texture
    GLuint tex = 0;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);

    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA,
                 NX, NY, 0, GL_RGBA, GL_UNSIGNED_BYTE,
                 texdata.data());

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    PlugIn_ViewPort& vp = *p.vp;
    float x = 0.0f;
    float y = 0.0f;
    float w = (float)vp.pix_width;
    float h = (float)vp.pix_height;

    float coords[8] = {
        x,     y,
        x+w,   y,
        x,     y+h,
        x+w,   y+h
    };

    float uv[8] = {
        0, 0,
        1, 0,
        0, 1,
        1, 1
    };

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    double alpha = p.alpha / 100.0;

    glUseProgram(pi_texture_2DA_shader_program);

    GLint mPosAttrib = glGetAttribLocation(pi_texture_2DA_shader_program, "aPos");
    GLint mUvAttrib  = glGetAttribLocation(pi_texture_2DA_shader_program, "aUV");

    GLint texUni1 = glGetUniformLocation(pi_texture_2DA_shader_program, "uTex1");
    GLint texUni2 = glGetUniformLocation(pi_texture_2DA_shader_program, "uTex2");
    glUniform1i(texUni1, 0);
    glUniform1i(texUni2, 1);

    GLint blendLoc = glGetUniformLocation(pi_texture_2DA_shader_program, "blendFactor");
    glUniform1f(blendLoc, 0.0f); // single texture

    float colorv[4] = {0, 0, 0, -(float)alpha};
    GLint colloc = glGetUniformLocation(pi_texture_2DA_shader_program, "color");
    glUniform4fv(colloc, 1, colorv);

    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);

    glVertexAttribPointer(mPosAttrib, 2, GL_FLOAT, GL_FALSE, 0, coords);
    glEnableVertexAttribArray(mPosAttrib);

    glVertexAttribPointer(mUvAttrib, 2, GL_FLOAT, GL_FALSE, 0, uv);
    glEnableVertexAttribArray(mUvAttrib);

    mat4x4 I;
    mat4x4_identity(I);
    GLint matloc = glGetUniformLocation(pi_texture_2DA_shader_program, "TransformMatrix");
    if (matloc != -1)
        glUniformMatrix4fv(matloc, 1, GL_FALSE, (const GLfloat*)I);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, tex);

    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

    glBindTexture(GL_TEXTURE_2D, 0);
    glDeleteTextures(1, &tex);
    glDisable(GL_BLEND);
}

// ============================================================================
// RenderGridDC()
// Draw ClimatologyColorGrid via wxDC as small rectangles.
// ============================================================================
void ClimatologyOverlayFactory::RenderGridDC(const ClimatologyColorGrid& grid,
                                             wxDC& dc,
                                             const ClimatologyRenderParams& p)
{
    if (!p.vp || !p.vp->bValid)
        return;

    wxDC* wdc = &dc;     // Convert reference → pointer (safe)
    if (!wdc)
        return;

    PlugIn_ViewPort& vp = *p.vp;
    const int NX = grid.lon_count;
    const int NY = grid.lat_count;
    if (NX <= 0 || NY <= 0)
        return;

    const int cellW = 2;
    const int cellH = 2;

    for (int iy = 0; iy < NY; iy++)
    {
        double merc = 1.0 - 2.0 * (double)iy / (double)(NY - 1);
        double lat  = rad2deg(atan(sinh(merc * M_PI)));

        for (int ix = 0; ix < NX; ix++)
        {
            double lon = 360.0 * (double)ix / (double)(NX - 1);

            wxColour c = grid.get(ix, iy);
            if (c.Alpha() == 0)
                continue;

            wxPoint pt;
            GetCanvasPixLL(&vp, &pt, lat, lon);

            wdc->SetPen(wxPen(c));
            wdc->SetBrush(wxBrush(c));
            wdc->DrawRectangle(pt.x, pt.y, cellW, cellH);
        }
    }
}

*/



// ============================================================================
// CreateGLTexture()
// Upload RGBA buffer to an OpenGL texture.
// ============================================================================
bool ClimatologyOverlayFactory::CreateGLTexture(ClimatologyOverlay& O)
{
    if (O.m_iTexture)
        return true;

    if (!O.m_data || O.m_width <= 0 || O.m_height <= 0)
        return false;

    GLuint tex = 0;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);

    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);

    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA,
                 O.m_width, O.m_height,
                 0, GL_RGBA, GL_UNSIGNED_BYTE,
                 O.m_data);

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    glBindTexture(GL_TEXTURE_2D, 0);

    O.m_iTexture = tex;
    return true;
}

// ============================================================================
// DrawGLTexture()
// Blend two monthly textures using shader program.
// ============================================================================
void ClimatologyOverlayFactory::DrawGLTexture(GLuint tex1, GLuint tex2,
                                              double dpos,
                                              PlugIn_ViewPort &vp,
                                              double alpha)
{
    float x = 0;
    float y = 0;
    float w = vp.pix_width;
    float h = vp.pix_height;

    float coords[8] = {
        x,     y,
        x+w,   y,
        x,     y+h,
        x+w,   y+h
    };

    float uv[8] = {
        0, 0,
        1, 0,
        0, 1,
        1, 1
    };

    glUseProgram(pi_texture_2DA_shader_program);

    GLint mPosAttrib = glGetAttribLocation(pi_texture_2DA_shader_program, "aPos");
    GLint mUvAttrib  = glGetAttribLocation(pi_texture_2DA_shader_program, "aUV");

    GLint texUni1 = glGetUniformLocation(pi_texture_2DA_shader_program, "uTex1");
    GLint texUni2 = glGetUniformLocation(pi_texture_2DA_shader_program, "uTex2");
    glUniform1i(texUni1, 0);
    glUniform1i(texUni2, 1);

    GLint blendLoc = glGetUniformLocation(pi_texture_2DA_shader_program, "blendFactor");
    glUniform1f(blendLoc, (float)dpos);

    // Modern alpha: shader expects positive alpha, not inverted transparency
    float colorv[4] = {0, 0, 0, (float)alpha};
    GLint colloc = glGetUniformLocation(pi_texture_2DA_shader_program, "color");
    glUniform4fv(colloc, 1, colorv);

    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);

    glVertexAttribPointer(mPosAttrib, 2, GL_FLOAT, GL_FALSE, 0, coords);
    glEnableVertexAttribArray(mPosAttrib);

    glVertexAttribPointer(mUvAttrib, 2, GL_FLOAT, GL_FALSE, 0, uv);
    glEnableVertexAttribArray(mUvAttrib);

    mat4x4 I;
    mat4x4_identity(I);

    GLint matloc = glGetUniformLocation(pi_texture_2DA_shader_program, "TransformMatrix");
    if (matloc != -1)
        glUniformMatrix4fv(matloc, 1, GL_FALSE, (const GLfloat*)I);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, tex1);

    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, tex2);

    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
}



// ============================================================================
// RenderDirectionArrowsGL()
// Draw wind/current arrows using OpenGL.
// ============================================================================
void ClimatologyOverlayFactory::RenderDirectionArrowsGL(const ClimatologyRenderParams& p)
{
    if (!p.vp || !p.vp->bValid)
        return;

    PlugIn_ViewPort& vp = *p.vp;

    auto drawForSetting = [&](int setting)
    {
        if (!p.showArrows || p.overlayType != setting)
            return;

        double   size       = p.arrowSize;
        int      width      = p.arrowWidth;
		wxColour color(p.color.Red(),
					   p.color.Green(),
					   p.color.Blue(),
					   (unsigned char)(255 * p.alpha));

        bool     lengthMode = p.arrowMode;

        double latStep = p.latStep[setting];
        double lonStep = p.lonStep[setting];
        double step    = std::max(latStep, lonStep);

        for (double lat = std::floor(vp.lat_min / step) * step;
             lat <= vp.lat_max;
             lat += step)
        {
            for (double lon = std::floor(vp.lon_min / step) * step;
                 lon <= vp.lon_max;
                 lon += step)
            {
                double u = getCurValue(U, setting, lat, lon, p);
                double v = getCurValue(V, setting, lat, lon, p);

                if (std::isnan(u) || std::isnan(v))
                    continue;

                if (setting == OVERLAY_WIND)
                    u = -u, v = -v;

                double mag = hypot(u, v);
                if (mag <= 0 || std::isnan(mag))
                    continue;

                double cstep, minv;
                if (setting == OVERLAY_CURRENT)
                {
                    cstep = 0.25;
                    minv  = 0.25;
                }
                else
                {
                    cstep = 5;
                    minv  = 2;

                    if (lengthMode)
                    {
                        u /= 15;
                        v /= 15;
                    }
                }

                if (!lengthMode)
                {
                    u /= mag;
                    v /= mag;
                }
                else if (mag < minv)
                    continue;

                double t = vp.rotation;
                double x = -size * (u * cos(t) + v * sin(t));
                double y =  size * (v * cos(t) - u * sin(t));

                wxPoint p1;
                GetCanvasPixLL(&vp, &p1, lat, lon);

                DrawArrowGL(p1.x + x, p1.y + y,
                            p1.x - x, p1.y - y,
							color, width);
							
                if (!lengthMode)
                    DrawBarbsGL(p1.x, p1.y, x, y, mag, cstep, color, width);

                DrawArrowGL(p1.x - x, p1.y - y,
                            p1.x - x/3 + y*2/3,
                            p1.y - y/3 - x*2/3,
                            color, width);

                DrawArrowGL(p1.x - x, p1.y - y,
                            p1.x - x/3 - y*2/3,
                            p1.y - y/3 + x*2/3,
                            color, width);
            }
        }
    };
	// Call once for each overlay type you support
    drawForSetting(OVERLAY_WIND);
    drawForSetting(OVERLAY_CURRENT);
}


// ============================================================================
// DrawBarbsGL()
// Draw wind barbs for GL arrow rendering.
// ============================================================================
void ClimatologyOverlayFactory::DrawBarbsGL(double px, double py,
                                            double x, double y,
                                            double mag,
                                            double cstep,
                                            const wxColour& c,
                                            int width)
{
    double ix = x, iy = y, dir = 1;
    double remaining = mag;

    glColor4ub(c.Red(), c.Green(), c.Blue(), 255);
    glLineWidth((float)width);

    while (remaining > cstep)
    {
        glBegin(GL_LINES);
        glVertex2f((GLfloat)(px + ix), (GLfloat)(py + iy));
        glVertex2f((GLfloat)(px + ix + x/3 + dir*y/2),
                   (GLfloat)(py + iy + y/3 - dir*x/2));
        glEnd();

        dir = -dir;
        if (dir > 0)
        {
            ix = ix * 2 / 3;
            iy = iy * 2 / 3;
        }
        remaining -= cstep;
    }
}

// ============================================================================
// RenderDirectionArrowsDC()
// Draw wind/current arrows using wxDC.
// ============================================================================
void ClimatologyOverlayFactory::RenderDirectionArrowsDC(const ClimatologyRenderParams& p)
{
    if (!p.dc || !p.dcContextValid)
        return;

    wxDC* wdc = p.dc->GetDC();
    if (!wdc)
        return;

    PlugIn_ViewPort& vp = *p.vp;

    auto drawForSetting = [&](int setting)
    {
        if (!p.showArrows || p.overlayType != setting)
            return;

        double   size       = p.arrowSize;
        int      width      = p.arrowWidth;
        wxColour color(p.color.Red(),
                       p.color.Green(),
                       p.color.Blue(),
                       (unsigned char)(255 * p.alpha));
        bool     lengthMode = p.arrowMode;

        double latStep = p.latStep[setting];
        double lonStep = p.lonStep[setting];
        double step    = std::max(latStep, lonStep);

        for (double lat = std::floor(vp.lat_min / step) * step;
             lat <= vp.lat_max;
             lat += step)
        {
            for (double lon = std::floor(vp.lon_min / step) * step;
                 lon <= vp.lon_max;
                 lon += step)
            {
                double u = getCurValue(U, setting, lat, lon, p);
                double v = getCurValue(V, setting, lat, lon, p);

                if (std::isnan(u) || std::isnan(v))
                    continue;

                if (setting == OVERLAY_WIND)
                    u = -u, v = -v;

                double mag = hypot(u, v);
                if (mag <= 0 || std::isnan(mag))
                    continue;

                double cstep, minv;
                if (setting == OVERLAY_CURRENT)
                {
                    cstep = 0.25;
                    minv  = 0.25;
                }
                else
                {
                    cstep = 5;
                    minv  = 2;

                    if (lengthMode)
                    {
                        u /= 15;
                        v /= 15;
                    }
                }

                if (!lengthMode)
                {
                    u /= mag;
                    v /= mag;
                }
                else if (mag < minv)
                    continue;

                double t = vp.rotation;
                double x = -size * (u * cos(t) + v * sin(t));
                double y =  size * (v * cos(t) - u * sin(t));

                wxPoint p1;
                GetCanvasPixLL(&vp, &p1, lat, lon);

                DrawArrowDC(wdc,
                            p1.x + x, p1.y + y,
                            p1.x - x, p1.y - y,
                            color, width);

                if (!lengthMode)
                    DrawBarbsDC(wdc, p1.x, p1.y, x, y, mag, cstep, color, width);

                DrawArrowDC(wdc,
                            p1.x - x, p1.y - y,
                            p1.x - x/3 + y*2/3,
                            p1.y - y/3 - x*2/3,
                            color, width);

                DrawArrowDC(wdc,
                            p1.x - x, p1.y - y,
                            p1.x - x/3 - y*2/3,
                            p1.y - y/3 + x*2/3,
                            color, width);
            }
        }
    };

    drawForSetting(OVERLAY_WIND);
    drawForSetting(OVERLAY_CURRENT);
}


// ============================================================================
// DrawArrowDC()
// Draw a single wxDC line segment.
// ============================================================================
void ClimatologyOverlayFactory::DrawArrowDC(wxDC* dc,
                                            double x1, double y1,
                                            double x2, double y2,
                                            const wxColour& c,
                                            int width)
{
    if (!dc)
        return;

    dc->SetPen(wxPen(c, width));
    dc->DrawLine(x1, y1, x2, y2);
}

// ============================================================================
// DrawBarbsDC()
// Draw wind barbs for wxDC arrow rendering.
// ============================================================================
void ClimatologyOverlayFactory::DrawBarbsDC(wxDC* dc,
                                            double px, double py,
                                            double x, double y,
                                            double mag,
                                            double cstep,
                                            const wxColour& c,
                                            int width)
{
    if (!dc)
        return;

    double ix = x, iy = y, dir = 1;
    double remaining = mag;

    dc->SetPen(wxPen(c, width));

    while (remaining > cstep)
    {
        dc->DrawLine(px + ix, py + iy,
                     px + ix + x/3 + dir*y/2,
                     py + iy + y/3 - dir*x/2);

        dir = -dir;
        if (dir > 0)
        {
            ix = ix * 2 / 3;
            iy = iy * 2 / 3;
        }
        remaining -= cstep;
    }
}


// ============================================================================
// RenderNumbers()
// Draw numeric values for scalar or vector overlays.
// ============================================================================
void ClimatologyOverlayFactory::RenderNumbers(const ClimatologyRenderParams& p)
{
    if (!IsCompletedLoading())
        return;
    if (!p.vp || !p.vp->bValid)
        return;

    if (!p.showNumbers)
        return;

    if (p.useGL)
        RenderNumbersGL(p);
    else
        RenderNumbersDC(p);
}

// ============================================================================
// RenderNumbersDC()
// Draw numeric values using wxDC.
// ============================================================================
void ClimatologyOverlayFactory::RenderNumbersDC(const ClimatologyRenderParams& p)
{
    wxDC* wdc = p.dc->GetDC();
	if (!wdc)
		return;

    PlugIn_ViewPort& vp = *p.vp;

    double spacing = p.numberSize;
	wxColour color(p.color.Red(),
				   p.color.Green(),
				   p.color.Blue(),
				   (unsigned char)(255 * p.alpha));

    wdc->SetTextForeground(color);

    const int setting = p.overlayType;

    Coord coord = (setting == OVERLAY_WIND || setting == OVERLAY_CURRENT)
                  ? MAG
                  : SCALAR;

    for (double py = spacing / 2;
         py <= vp.rv_rect.height - spacing / 4;
         py += spacing)
    {
        for (double px = spacing / 2;
             px <= vp.rv_rect.width - spacing / 4;
             px += spacing)
        {
            double lat, lon;
            wxPoint pt((int)px, (int)py);

            GetCanvasLLPix(&vp, pt, &lat, &lon);

            double v = getCurValue(coord, setting, lat, lon, p);
            if (std::isnan(v))
                continue;

            wxString txt = wxString::Format("%.0f", v);
            wdc->DrawText(txt, px, py);
        }
    }
}

// ============================================================================
// RenderNumbersGL()
// Draw numeric values using wxDC text over GL canvas.
// ============================================================================
void ClimatologyOverlayFactory::RenderNumbersGL(const ClimatologyRenderParams& p)
{
    if (!IsCompletedLoading() || !p.vp)
        return;
    if (!(p.useGL && p.glContextValid))
        return;

    PlugIn_ViewPort& vp = *p.vp;

    const int setting  = p.overlayType;
    const double spacing = p.numberSize;

    Coord coord = (setting == OVERLAY_WIND || setting == OVERLAY_CURRENT)
                  ? MAG
                  : SCALAR;

    wxColour color(p.color.Red(),
                   p.color.Green(),
                   p.color.Blue(),
                   (unsigned char)(255 * p.alpha));

    // GL numeric rendering uses wxDC text overlay
    if (p.dc && p.dcContextValid)
    {
        wxDC* wdc = p.dc->GetDC();
        if (!wdc)
            return;

        wdc->SetTextForeground(color);

        for (double py = spacing / 2;
             py <= vp.rv_rect.height - spacing / 4;
             py += spacing)
        {
            for (double px = spacing / 2;
                 px <= vp.rv_rect.width - spacing / 4;
                 px += spacing)
            {
                double lat, lon;
                wxPoint pt((int)px, (int)py);

                GetCanvasLLPix(&vp, pt, &lat, &lon);

                double v = getCurValue(coord, setting, lat, lon, p);
                if (std::isnan(v))
                    continue;

                wxPoint r;
                GetCanvasPixLL(&vp, &r, lat, lon);

                char buf[16];
                snprintf(buf, sizeof(buf), "%.0f", v);

                wdc->DrawText(wxString::FromUTF8(buf), r.x, r.y);
            }
        }
    }
}


// ============================================================================
// RenderIsoBars()
// Draw isobars (contours) for scalar overlays using IsoBarMap.
// ============================================================================

void ClimatologyOverlayFactory::RenderIsoBars(const ClimatologyRenderParams& p)
{
    if (!IsCompletedLoading())
        return;
    if (!p.vp || !p.vp->bValid)
        return;
    if (!p.showIsoBars)
        return;

    // UI overlay type
    const OverlayType uiSetting =
        static_cast<OverlayType>(p.overlayType);

    // Convert UI → NOAA overlay type
    const NoaaOverlayType setting =
        ToNoaa(uiSetting);

    // Unified spacing
    double spacing = p.isoBarSpacing;

    // Unified step mapping
    double step;
    switch (p.isoBarStep)
    {
    default: step = 4;   break;
    case 1:  step = 2;   break;
    case 2:  step = 1;   break;
    case 3:  step = 0.5; break;
    case 4:  step = 0.25; break;
    }

    int units = p.units;
    int month = p.currentMonth;

    // Retrieve or build IsoBarMap
    ClimatologyIsoBarMap* iso =
        GetOrCreateIsoBarMap(uiSetting, month, spacing, step, units, p);

    if (!iso)
        return;

    // -----------------------------------------------------------------------
    // GL path (preferred)
    // -----------------------------------------------------------------------
    if (p.useGL && p.glContextValid)
    {
        iso->PlotGL(*p.vp);
        return;
    }

    // -----------------------------------------------------------------------
    // DC fallback
    // -----------------------------------------------------------------------
    if (p.dc && p.dcContextValid)
    {
        iso->PlotDC(p.dc, *p.vp);
        return;
    }

    // No valid rendering path
}


// ============================================================================
// GetOrCreateIsoBarMap()
// Retrieve cached IsoBarMap or create a new one for this overlay/month.
// ============================================================================
ClimatologyIsoBarMap*
ClimatologyOverlayFactory::GetOrCreateIsoBarMap(int overlayType,
                                                int month,
                                                double spacing,
                                                double step,
                                                int units,
                                                const ClimatologyRenderParams& p)
{
    // Convert UI overlay type → NOAA overlay type
    const NoaaOverlayType setting =
        ToNoaa(static_cast<OverlayType>(overlayType));

    // Reference to the cached pointer
    ClimatologyIsoBarMap*& iso = m_pIsobars[setting][month];

    // Reuse existing map if settings match
    if (iso && iso->SameSettings(spacing, step, units, month, 0))
        return iso;

    // Destroy old map
    if (iso)
    {
        delete iso;
        iso = nullptr;
    }

    wxString name = wxString::Format("Overlay %d", setting);

    // Build new IsoBarMap (day = 0 for unified NOAA model)
    iso = new ClimatologyIsoBarMap(name,
                                   spacing,
                                   step,
                                   *this,
                                   setting,
                                   units,
                                   month,
                                   0,      // day ignored in unified model
                                   p);     // per-frame render params

    // Build contour lines
    if (!iso->Recompute(nullptr))
    {
        delete iso;
        iso = nullptr;
        return nullptr;
    }

    return iso;
}


// ============================================================================
// DestroyIsoBarMap()
// Remove cached IsoBarMap for a given overlay/month.
// ============================================================================
void ClimatologyOverlayFactory::DestroyIsoBarMap(int overlayType, int month)
{
    const NoaaOverlayType setting =
        ToNoaa(static_cast<OverlayType>(overlayType));

    ClimatologyIsoBarMap*& iso = m_pIsobars[setting][month];
    if (iso)
    {
        delete iso;
        iso = nullptr;
    }
}


// ============================================================================
// RenderWindAtlas()
// Draw simplified wind atlas arrows at fixed spacing.
// ============================================================================
void ClimatologyOverlayFactory::RenderWindAtlas(const ClimatologyRenderParams& p)
{
    if (!p.windAtlas.enabled)
        return;
    if (!p.vp || !p.vp->bValid)
        return;

    PlugIn_ViewPort& vp = *p.vp;

    double spacing = p.windAtlas.spacing;
    double size    = p.windAtlas.size;
 
    wxColour color(p.color.Red(),
               p.color.Green(),
               p.color.Blue(),
               (unsigned char)(255 * p.alpha));


    for (double lat = std::floor(vp.lat_min / spacing) * spacing;
         lat <= vp.lat_max;
         lat += spacing)
    {
        for (double lon = std::floor(vp.lon_min / spacing) * spacing;
             lon <= vp.lon_max;
             lon += spacing)
        {
            double u = getCurValue(U, OVERLAY_WIND, lat, lon, p);
            double v = getCurValue(V, OVERLAY_WIND, lat, lon, p);

            if (std::isnan(u) || std::isnan(v))
                continue;

            double mag = hypot(u, v);
            if (mag <= 0)
                continue;

            double t = vp.rotation;
            double x = -size * (u * cos(t) + v * sin(t));
            double y =  size * (v * cos(t) - u * sin(t));

            wxPoint p1;
            GetCanvasPixLL(&vp, &p1, lat, lon);

            if (p.useGL)
            {
				glColor4ub(color.Red(), color.Green(), color.Blue(),
									(unsigned char)(255 * p.alpha));
                glLineWidth(1.0f);

                glBegin(GL_LINES);
                glVertex2f((GLfloat)(p1.x + x), (GLfloat)(p1.y + y));
                glVertex2f((GLfloat)(p1.x - x), (GLfloat)(p1.y - y));
                glEnd();
            }
            else if (p.dc)
            {
                wxDC* wdc = p.dc->GetDC();
				if (!wdc)
					return;

                wdc->SetPen(wxPen(color, 1));
                wdc->DrawLine(p1.x + x, p1.y + y,
                             p1.x - x, p1.y - y);
            }
        }
    }
}



// ============================================
// CYCLONES      Filter Helpers
// =============================================


static bool PointPassesIntensityAndState(const CyclonePoint& pt,
                                         const CycloneFilterParams& F)
{
    // Intensity: wind
    if (F.windMin > 0 && pt.wind < F.windMin)
        return false;
    if (F.windMax > 0 && pt.wind > F.windMax)
        return false;

    // Intensity: pressure
    if (F.pressureMin > 0 && pt.pressure < F.pressureMin)
        return false;
    if (F.pressureMax > 0 && pt.pressure > F.pressureMax)
        return false;

    // State filter (modern set-based filter)
    if (!F.stateFilter.empty())
    {
        if (F.stateFilter.count(pt.state) == 0)
            return false;
    }

    return true;
}



// ============================================================================
// ApplyCycloneFilter   Track Level Filter
// Full unified cyclone filtering: basin, ENSO, state, wind, pressure,
// month, year, date range, and timeline/daySpan.
// Produces m_cyclone_cache (filtered CycloneTrack list).
// ============================================================================
void ClimatologyOverlayFactory::ApplyCycloneFilter(const CycloneFilterParams& F)
{
    m_cyclone_cache.clear();

    for (const CycloneTrack& t : m_cycloneData.tracks)
    {
        // ------------------------------------------------------------
        // Track-level filters
        // ------------------------------------------------------------
        if (!F.basinFilter.empty() &&
            F.basinFilter.count(t.basin) == 0)
            continue;

        if (!F.ensoFilter.empty() &&
            F.ensoFilter.count(t.ensoPhase) == 0)
            continue;

        if (!F.stateFilter.empty() &&
            F.stateFilter.count(t.dominantState) == 0)
            continue;

        if (t.maxWind < F.windMin || t.maxWind > F.windMax)
            continue;

        if (t.minPressure < F.pressureMin || t.minPressure > F.pressureMax)
            continue;

        if (F.startDate.IsValid() && t.endDate < F.startDate)
            continue;

        if (F.endDate.IsValid() && t.startDate > F.endDate)
            continue;

        // ------------------------------------------------------------
        // Point-level filtering + timeline/daySpan
        // ------------------------------------------------------------
        CycloneTrack filtered = t;
        filtered.points.clear();

        for (const CyclonePoint& pt : t.points)
        {
            int year  = pt.time.GetYear();
            int month = pt.time.GetMonth() + 1;

            // Month filter
            if (!F.monthFilter.empty() &&
                F.monthFilter.count(month) == 0)
                continue;

            // Year filter
            if ((F.yearMin > 0 && year < F.yearMin) ||
                (F.yearMax > 0 && year > F.yearMax))
                continue;

            // Point-level intensity
            if (pt.wind < F.windMin || pt.wind > F.windMax)
                continue;

            // Point-level pressure
            if (pt.pressure < F.pressureMin || pt.pressure > F.pressureMax)
                continue;

            // Point-level storm state
            if (!F.stateFilter.empty() &&
                F.stateFilter.count(pt.state) == 0)
                continue;

			// Timeline/daySpan filter
			if (!CycloneTimelineInterpolation(pt, F, month))
				continue;

            filtered.points.push_back(pt);
        }

        if (!filtered.points.empty())
            m_cyclone_cache.push_back(filtered);
    }
}

				
// ============================================================================
// RenderCyclones()  Uses m_cyclone_cache only.
// Draw cyclone tracks using GL or wxDC. 
// ============================================================================

void ClimatologyOverlayFactory::RenderCyclones(const ClimatologyRenderParams& p)
{
    if (!m_hasCycloneData)
        return;
    if (!p.showCyclones)
        return;
    if (!p.vp || !p.vp->bValid)
        return;
    if (m_cyclone_cache.empty())
        return;

    const PlugIn_ViewPort& vp = *p.vp;
    const CycloneParams& A    = p.cycloneParams;
	
    // -----------------------------------------------------------------------
    // GL path
    // -----------------------------------------------------------------------
    if (p.useGL && p.glContextValid)
    {
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

        for (const CycloneTrack& track : m_cyclone_cache)
        {
//  	Commented out for fast version that draws only the filtered cache
//            if (!CycloneVisible(track, p))
//                continue;

            for (size_t i = 1; i < track.points.size(); i++)
            {
                const CyclonePoint& a = track.points[i - 1];
                const CyclonePoint& b = track.points[i];

                // ============================================================
                // CHOOSE ONE OF THESE THREE COLOR METHODS:
                // ============================================================

                // 1. Per‑point color (simple, stable)
                // wxColour base = CyclonePointColor(a, A);
                // wxColour color(base.Red(),
                //                base.Green(),
                //                base.Blue(),
                //                (unsigned char)(255 * (A.opacity * p.alpha)));

                // 2. Per‑segment blended color (smooth transitions)
                // wxColour color = CycloneSegmentColor(a, b, A);

                // 3. Full track intensity gradient (progressive)
                // double t = double(i) / double(track.points.size() - 1);
                // wxColour color = CycloneGradientColorTrack(track, t, A);

                // 4. line thickness based on intensity
                float width = CycloneTrackWidth(a, A);
                glLineWidth(width);

                // Default: track‑level color
                wxColour color = CycloneColor(track, p);

                double ax, ay, bx, by;
                LatLonToPixel(vp, a.lat, a.lon, ax, ay);
                LatLonToPixel(vp, b.lat, b.lon, bx, by);

                glColor4ub(color.Red(), color.Green(), color.Blue(),
                           (unsigned char)(255 * (A.opacity * p.alpha)));

                glBegin(GL_LINES);
                glVertex2f((GLfloat)ax, (GLfloat)ay);
                glVertex2f((GLfloat)bx, (GLfloat)by);
                glEnd();
            }
        }

        glDisable(GL_BLEND);
        return;
    }

    // -----------------------------------------------------------------------
    // DC path
    // -----------------------------------------------------------------------
    if (p.dc && p.dcContextValid)
    {
        wxDC* wdc = p.dc->GetDC();
        if (!wdc)
            return;

        for (const CycloneTrack& track : m_cyclone_cache)
        {
//  	Commented out for fast version that draws only the filtered cache
//            if (!CycloneVisible(track, p))
//                continue;

            for (size_t i = 1; i < track.points.size(); i++)
            {
                const CyclonePoint& a = track.points[i - 1];
                const CyclonePoint& b = track.points[i];
				
                // ============================================================
                // Same three options for DC:
                // ============================================================

                // 1. Per‑point color
                // wxColour base = CyclonePointColor(a, A);
                // wxColour color(base.Red(),
                //                base.Green(),
                //                base.Blue(),
                //                (unsigned char)(255 * (A.opacity * p.alpha)));

                // 2. Per‑segment blended color
                // wxColour color = CycloneSegmentColor(a, b, A);

                // 3. Full‑track gradient
                // double t = double(i) / double(track.points.size() - 1);
                // wxColour color = CycloneGradientColorTrack(track, t, A);

                // 4. line thickness based on intensity
                float width = CycloneTrackWidth(a, A);

                // 5. DC‑based legend for basin + ENSO + intensity + thickness 
                // if (p.showCyclones && p.dc && p.dcContextValid)
                // {
                //     wxPoint legendOrigin(20, 20);   // top-left corner
                //     CycloneLegend(*wdc, m_displayParams.cyclone, legendOrigin);
                // }

                wxColour color = CycloneColor(track, p);

                double ax, ay, bx, by;
                LatLonToPixel(vp, a.lat, a.lon, ax, ay);
                LatLonToPixel(vp, b.lat, b.lon, bx, by);

                // THIS is the correct version
                wdc->SetPen(wxPen(color, width));
                wdc->DrawLine((int)ax, (int)ay, (int)bx, (int)by);
            }
        }
    }
}


// ==========================================================
// CYCLONE SUBSYSTEM — Loader
// ==========================================================


// ------------------------------------------------------------
// Map (year, month) → CycloneENSO using m_ensoData   Helper
// ------------------------------------------------------------
CycloneENSO ClimatologyOverlayFactory::ENSOFromDate(int year, int month) const
{
    if (month < 1 || month > 12)
        return CycloneENSO::NA;

    auto itYear = m_ensoData.find(year);
    if (itYear == m_ensoData.end())
        return CycloneENSO::NA;

    auto itMonth = itYear->second.find(month);
    if (itMonth == itYear->second.end())
        return CycloneENSO::NA;

    return itMonth->second;
}




// =========================================================
// BuildCycloneTracks  - uses Unified Data in memory
// =========================================================
// Correct track builder to use. Groups m_rawCyclonePoints by StormID
// sorts by time, computes metadata, and fills m_cycloneData.tracks
// and m_cycloneData.idToIndex.

bool ClimatologyOverlayFactory::BuildCycloneTracks()
{
    m_cycloneData.tracks.clear();
    m_cycloneData.idToIndex.clear();

    std::map<int, std::vector<CyclonePoint>> grouped;

    // Group points by storm ID
    for (const CyclonePoint& p : m_rawCyclonePoints)
    {
        if (!p.valid)
            continue;

        grouped[p.stormId].push_back(p);
    }

    // Build tracks
    for (auto& kv : grouped)
    {
        int id = kv.first;
        auto& pts = kv.second;

        if (pts.empty())
            continue;

        // Sort by time
        std::sort(pts.begin(), pts.end(),
                  [](const CyclonePoint& a, const CyclonePoint& b)
                  {
                      return a.time.IsEarlierThan(b.time);
                  });

        CycloneTrack track;
        track.id = id;
        track.points = pts;
        track.valid = true;

        // Track-level metadata
        track.startDate = pts.front().time;
        track.endDate   = pts.back().time;
		track.daySpan = (track.endDate - track.startDate).GetDays();

        track.basin     = pts.front().basin;
        track.ensoPhase = pts.front().ensoPhase;

        track.maxWind     = -999;
        track.minPressure = 9999;

        std::map<CycloneState,int> stateCount;

        for (const CyclonePoint& p : pts)
        {
            if (!std::isnan(p.wind))
                track.maxWind = std::max(track.maxWind, p.wind);

            if (!std::isnan(p.pressure))
                track.minPressure = std::min(track.minPressure, p.pressure);

            stateCount[p.state]++;
        }

        // Dominant state
        track.dominantState = CycloneState::UNKNOWN;
        int best = 0;
        for (auto& sc : stateCount)
        {
            if (sc.second > best)
            {
                best = sc.second;
                track.dominantState = sc.first;
            }
        }

        // Store track
        int idx = m_cycloneData.tracks.size();
        m_cycloneData.tracks.push_back(track);
        m_cycloneData.idToIndex[id] = idx;
    }

    return true;
}


// ============================================================================
// BasinColor()
// Helper for Rendercyclones
// ============================================================================

wxColour ClimatologyOverlayFactory::BasinColor(CycloneBasin basin) const
{
    switch (basin)
    {
        case CycloneBasin::EPA: return wxColour(255, 128, 128);
        case CycloneBasin::WPA: return wxColour(255, 200, 0);
        case CycloneBasin::SPA: return wxColour(255, 0, 255);
        case CycloneBasin::ATL: return wxColour(0, 128, 255);
        case CycloneBasin::NIO: return wxColour(0, 200, 128);
        case CycloneBasin::SHE: return wxColour(128, 128, 255);
        default:                return wxColour(255, 255, 255);
    }
}




// ============================================================================
// CycloneColor()
// Determine cyclone track color based on unified Saffir–Simpson intensity.
// ============================================================================
wxColour ClimatologyOverlayFactory::CycloneColor(const CycloneTrack& track,
                                                 const ClimatologyRenderParams& p) const
{
    const CycloneParams& A = p.cycloneParams;

    // User override color
    if (A.useFixedColor)
        return A.fixedColor;

    double w = track.maxWind;

    if (std::isnan(w))
        return wxColour(180, 180, 180);   // unknown

    if (w < 20)  return wxColour(180, 180, 180); // disturbance
    if (w < 34)  return wxColour(120, 200, 120); // depression
    if (w < 64)  return wxColour(255, 200, 80);  // tropical storm
    if (w < 83)  return wxColour(255, 160, 80);  // Cat 1
    if (w < 96)  return wxColour(255, 120, 80);  // Cat 2
    if (w < 113) return wxColour(255, 80, 80);   // Cat 3
    if (w < 137) return wxColour(200, 40, 40);   // Cat 4
    return wxColour(160, 0, 0);                  // Cat 5+
}



// =================================================
//  CyclonePointColor()
//  color each segment based on the point’s attributes:
// ======================================================
wxColour ClimatologyOverlayFactory::CyclonePointColor(const CyclonePoint& pt,
                                                      const CycloneParams& A) const
{
    switch (A.colorMode)
    {
        // ================================================================
        // Mode 0: Basin-based coloring
        // ================================================================
        case 0:
            return BasinColor(pt.basin);

        // ================================================================
        // Mode 1: ENSO-based coloring
        // ================================================================
        case 1:
            switch (pt.ensoPhase)
            {
                case CycloneENSO::EL_NINO:  return wxColour(255, 80, 80);   // red
                case CycloneENSO::LA_NINA:  return wxColour(80, 80, 255);   // blue
                case CycloneENSO::NEUTRAL:  return wxColour(120, 200, 120); // green
                default:                    return wxColour(180, 180, 180); // gray
            }

        // ================================================================
        // Mode 2: Intensity-based coloring (point-level)
        // ================================================================
        case 2:
        {
            double w = pt.wind;   // unified wind field

            if (std::isnan(w))
                return wxColour(180, 180, 180);

            if (w < 20)  return wxColour(180, 180, 180); // disturbance
            if (w < 34)  return wxColour(120, 200, 120); // depression
            if (w < 64)  return wxColour(255, 200, 80);  // tropical storm
            if (w < 83)  return wxColour(255, 160, 80);  // Cat 1
            if (w < 96)  return wxColour(255, 120, 80);  // Cat 2
            if (w < 113) return wxColour(255, 80, 80);   // Cat 3
            if (w < 137) return wxColour(200, 40, 40);   // Cat 4
            return wxColour(160, 0, 0);                  // Cat 5+
        }

        // ================================================================
        // Mode 3: Storm-type coloring
        // ================================================================
        case 3:
            switch (pt.state)
            {
                case CycloneState::TROPICAL:      return wxColour(255, 160, 80);
                case CycloneState::SUBTROPICAL:   return wxColour(255, 200, 120);
                case CycloneState::EXTRATROPICAL: return wxColour(120, 160, 255);
                case CycloneState::WAVE:          return wxColour(160, 255, 160);
                case CycloneState::REMANENT:      return wxColour(180, 180, 180);
                case CycloneState::UNKNOWN:       return wxColour(200, 200, 200);
            }
            break;
    }

    // -----------------------------------------------------------------------
    // Fallback neutral color
    // -----------------------------------------------------------------------
    return wxColour(255, 255, 255);
}


// ====================================================
// CycloneSegmentColor()
//  RGB blend between two CyclonePoints
//  So all color modes (basin, ENSO, intensity, stormtype) work automatically.
// ======================================================
wxColour ClimatologyOverlayFactory::CycloneSegmentColor(const CyclonePoint& a,
                                                        const CyclonePoint& b,
                                                        const CycloneParams& A) const
{
    // Resolve per-point colors using unified point-level resolver
    wxColour ca = CyclonePointColor(a, A);
    wxColour cb = CyclonePointColor(b, A);

    // Blend factor: midpoint of segment
    const double t = 0.5;

    // Linear RGB interpolation
    const unsigned char r =
        static_cast<unsigned char>(ca.Red()   * (1.0 - t) + cb.Red()   * t);
    const unsigned char g =
        static_cast<unsigned char>(ca.Green() * (1.0 - t) + cb.Green() * t);
    const unsigned char bch =
        static_cast<unsigned char>(ca.Blue()  * (1.0 - t) + cb.Blue()  * t);

    return wxColour(r, g, bch);
}


// ============================================================================
// CycloneGradientColorTrack()
// Smooth gradient along a CycloneTrack using point-level colors.
// t = 0 → first point, t = 1 → last point
// How to use it in RenderCyclones:
//   double t = double(i) / double(track.points.size() - 1);
//   wxColour color = CycloneGradientColorTrack(track, t, A);
// ============================================================================
wxColour ClimatologyOverlayFactory::CycloneGradientColorTrack(
        const CycloneTrack& track,
        double t,
        const CycloneParams& A) const
{
    if (track.points.empty())
        return wxColour(200, 200, 200);

    // Clamp t
    if (t < 0.0) t = 0.0;
    if (t > 1.0) t = 1.0;

    // Map t → index
    double idx = t * (track.points.size() - 1);
    size_t i0 = static_cast<size_t>(floor(idx));
    size_t i1 = static_cast<size_t>(ceil(idx));

    if (i0 >= track.points.size())
        i0 = track.points.size() - 1;
    if (i1 >= track.points.size())
        i1 = track.points.size() - 1;

    const CyclonePoint& a = track.points[i0];
    const CyclonePoint& b = track.points[i1];

    // Local blend factor
    double localT = idx - i0;

    wxColour ca = CyclonePointColor(a, A);
    wxColour cb = CyclonePointColor(b, A);

    unsigned char r =
        (unsigned char)(ca.Red()   * (1.0 - localT) + cb.Red()   * localT);
    unsigned char g =
        (unsigned char)(ca.Green() * (1.0 - localT) + cb.Green() * localT);
    unsigned char bch =
        (unsigned char)(ca.Blue()  * (1.0 - localT) + cb.Blue()  * localT);

    return wxColour(r, g, bch);
}



// ============================================================================
// CycloneTrackWidth()
// Determine line thickness based on intensity, user settings, and color mode.
// ============================================================================
float ClimatologyOverlayFactory::CycloneTrackWidth(const CyclonePoint& pt,
                                                   const CycloneParams& A) const
{
    // ------------------------------------------------------------
    // Thickness scaling only applies if enabled
    // ------------------------------------------------------------
    if (!A.useThicknessScaling)
        return A.baseThickness;

    // ------------------------------------------------------------
    // Unified intensity field
    // ------------------------------------------------------------
    double w = pt.wind;   // correct unified field

    if (std::isnan(w))
        return A.baseThickness;

    // ------------------------------------------------------------
    // Saffir–Simpson category → multiplier
    // ------------------------------------------------------------
    float mult;
    if (w < 20)      mult = 0.8f;   // disturbance
    else if (w < 34) mult = 1.0f;   // depression
    else if (w < 64) mult = 1.2f;   // tropical storm
    else if (w < 83) mult = 1.4f;   // Cat 1
    else if (w < 96) mult = 1.6f;   // Cat 2
    else if (w < 113) mult = 1.8f;  // Cat 3
    else if (w < 137) mult = 2.0f;  // Cat 4
    else              mult = 2.3f;  // Cat 5+

    float width = A.baseThickness * mult;

    // ------------------------------------------------------------
    // Clamp width
    // ------------------------------------------------------------
    if (width < A.minThickness)
        width = A.minThickness;
    if (width > A.maxThickness)
        width = A.maxThickness;

    return width;
}



// ============================================================================
// CycloneLegend()
// Draw legend for basin, ENSO, intensity, or storm-type depending on colorMode.
// ============================================================================
// How to call it (DC path only)
// Inside your DC rendering block in RenderCyclones():
//  if (p.showCyclones && p.dc && p.dcContextValid)
//  {
//      wxDC* wdc = p.dc->GetDC();
//      if (!wdc)
//          return;    // or just 'return;' from the DC block
//      wxPoint legendOrigin(20, 20);   // top-left corner
//      CycloneLegend(*wdc, p.cycloneParams, legendOrigin);
//  }

void ClimatologyOverlayFactory::CycloneLegend(wxDC& dc,
                                              const CycloneParams& A,
                                              const wxPoint& origin) const
{
    // dc is a reference; it cannot be null
    wxDC* wdc = &dc;

    const int boxW = 22;
    const int boxH = 12;
    const int pad  = 4;

    int x = origin.x;
    int y = origin.y;

    wdc->SetTextForeground(*wxWHITE);
    wdc->SetBackgroundMode(wxTRANSPARENT);

    // ------------------------------------------------------------
    // Mode 0: Basin-based coloring
    // ------------------------------------------------------------
    if (A.colorMode == 0)
    {
        struct BasinEntry { const char* name; CycloneBasin basin; };
        BasinEntry basins[] = {
            {"East Pacific",  CycloneBasin::EPA},
            {"West Pacific",  CycloneBasin::WPA},
            {"South Pacific", CycloneBasin::SPA},
            {"Atlantic",      CycloneBasin::ATL},
            {"North Indian",  CycloneBasin::NIO},
            {"South Indian",  CycloneBasin::SHE}
        };

        wdc->DrawText("Basins:", x, y);
        y += boxH + pad;

        for (const auto& B : basins)
        {
            wxColour c = BasinColor(B.basin);
            wdc->SetBrush(wxBrush(c));
            wdc->SetPen(*wxTRANSPARENT_PEN);

            wdc->DrawRectangle(x, y, boxW, boxH);
            wdc->DrawText(wxString(B.name), x + boxW + pad, y);

            y += boxH + pad;
        }
        return;
    }

    // ------------------------------------------------------------
    // Mode 1: ENSO-based coloring
    // ------------------------------------------------------------
    if (A.colorMode == 1)
    {
        struct ENSOEntry { const char* name; CycloneENSO id; };
        ENSOEntry enso[] = {
            {"El Niño",   CycloneENSO::EL_NINO},
            {"La Niña",   CycloneENSO::LA_NINA},
            {"Neutral",   CycloneENSO::NEUTRAL},
            {"N/A",       CycloneENSO::NA}
        };

        wdc->DrawText("ENSO:", x, y);
        y += boxH + pad;

        for (const auto& E : enso)
        {
            CyclonePoint dummy;
            dummy.ensoPhase = E.id;   // unified field

            wxColour c = CyclonePointColor(dummy, A);

            wdc->SetBrush(wxBrush(c));
            wdc->SetPen(*wxTRANSPARENT_PEN);

            wdc->DrawRectangle(x, y, boxW, boxH);
            wdc->DrawText(wxString(E.name), x + boxW + pad, y);

            y += boxH + pad;
        }
        return;
    }

	// ------------------------------------------------------------
	// Mode 2: Intensity-based coloring (Saffir–Simpson)
	// ------------------------------------------------------------
	if (A.colorMode == 2)
	{
		struct CatEntry { const char* name; double wind; };
		CatEntry cats[] = {
			{"Disturbance",     10},
			{"Depression",      25},
			{"Tropical Storm",  50},
			{"Category 1",      75},
			{"Category 2",      90},
			{"Category 3",     105},
			{"Category 4",     125},
			{"Category 5+",    150}
		};

		wdc->DrawText("Intensity:", x, y);
		y += boxH + pad;

		for (const auto& C : cats)
		{
			CyclonePoint dummy;
			dummy.wind = C.wind;   // unified intensity field

			wxColour c = CyclonePointColor(dummy, A);

			wdc->SetBrush(wxBrush(c));
			wdc->SetPen(*wxTRANSPARENT_PEN);

			wdc->DrawRectangle(x, y, boxW, boxH);
			wdc->DrawText(wxString(C.name), x + boxW + pad, y);

			y += boxH + pad;
		}
		return;
	}


    // ------------------------------------------------------------
    // Mode 3: Storm-type coloring
    // ------------------------------------------------------------
    if (A.colorMode == 3)
    {
        struct TypeEntry { const char* name; CycloneState state; };
        TypeEntry types[] = {
            {"Tropical",      CycloneState::TROPICAL},
            {"Subtropical",   CycloneState::SUBTROPICAL},
            {"Extratropical", CycloneState::EXTRATROPICAL},
            {"Wave",          CycloneState::WAVE},
            {"Remanent",      CycloneState::REMANENT},
            {"Unknown",       CycloneState::UNKNOWN}
        };

        wdc->DrawText("Storm Type:", x, y);
        y += boxH + pad;

        for (const auto& T : types)
        {
            CyclonePoint dummy;
            dummy.state = T.state;

            wxColour c = CyclonePointColor(dummy, A);

            wdc->SetBrush(wxBrush(c));
            wdc->SetPen(*wxTRANSPARENT_PEN);

            wdc->DrawRectangle(x, y, boxW, boxH);
            wdc->DrawText(wxString(T.name), x + boxW + pad, y);

            y += boxH + pad;
        }
        return;
    }
}


// ========================================================
// CycloneTimelineInterpolation
//  month → day‑of‑year → visibility window
//  Converts month → day‑of‑year  using NOAA climatology midpoints:
// ========================================================

bool ClimatologyOverlayFactory::CycloneTimelineInterpolation(
        const CyclonePoint& pt,
        const CycloneFilterParams& F,
        int currentMonth) const
{
    // NOAA climatology midpoints for each month
    static const int monthCenter[12] =
    {
        15, 46, 75, 105, 135, 166,
        196, 227, 258, 288, 319, 349
    };

    if (currentMonth < 1 || currentMonth > 12)
        return false;

    int centerDay = monthCenter[currentMonth - 1];

    // Cyclone point day-of-year (unified)
    int d = pt.time.GetDayOfYear();

    // Absolute difference with wrap-around
    int diff = abs(d - centerDay);
    if (diff > 183)
        diff = 365 - diff;

    // Visibility window = ± daySpan/2
    return diff <= (F.daySpan / 2);
}


// ============================================================================
// CycloneVisible()
// Reduced to timeline-only visibility.
// ============================================================================
bool ClimatologyOverlayFactory::CycloneVisible(const CycloneTrack& track,
                                               const ClimatologyRenderParams& p) const
{
    const CycloneFilterParams& F = p.cycloneFilter;

    // If timeline filtering is disabled, everything is visible
    if (F.daySpan <= 0)
        return true;

    // Check any point in the track for timeline visibility
    for (const CyclonePoint& pt : track.points)
    {
		int month = pt.time.GetMonth() + 1;
		if (CycloneTimelineInterpolation(pt, F, month))
            return true;
    }
    return false;
}



// ============================================================================
// GetENSOIndex()
// Retrieve ENSO index (dimensionless) for a given year/month.
// ============================================================================
double ClimatologyOverlayFactory::GetENSOIndex(int year, int month)
{
    if (month < 1 || month > 12)
        return NAN;

    auto it = m_ensoData.find(year);
    if (it == m_ensoData.end())
        return NAN;

    auto it2 = it->second.find(month);
    if (it2 == it->second.end())
        return NAN;

    CycloneENSO e = it2->second;

    switch (e)
    {
        case CycloneENSO::EL_NINO: return 1.0;
        case CycloneENSO::LA_NINA: return -1.0;
        case CycloneENSO::NEUTRAL: return 0.0;
		case CycloneENSO::NA: return 0.0;
        default: return NAN;
    }
}


// ============================================================================
// GetCycloneTrack()
// Retrieve full cyclone track by cyclone ID.
// Returns true if track exists.
// ============================================================================
bool ClimatologyOverlayFactory::GetCycloneTrack(int id,
                                                CycloneTrack& out) const
{
auto it = m_cycloneData.idToIndex.find(id);
if (it == m_cycloneData.idToIndex.end())
    return false;

const CycloneTrack& t = m_cycloneData.tracks[it->second];
if (!t.valid || t.points.empty())
    return false;

out = t;
return true;

}


// ============================================================================
// GetCyclonePoint
// Retrieve a single cyclone track point by cyclone ID and point index.
// Returns true if the point exists.
// ============================================================================
bool ClimatologyOverlayFactory::GetCyclonePoint(int id,
                                                int index,
                                                CyclonePoint& out) const
{
    auto it = m_cycloneData.idToIndex.find(id);
    if (it == m_cycloneData.idToIndex.end())
        return false;

    const CycloneTrack& t = m_cycloneData.tracks[it->second];
    if (!t.valid || index < 0 || index >= (int)t.points.size())
        return false;

    out = t.points[index];
    return true;
}


// ============================================================================
// GetCycloneData()
// Retrieve cyclone metadata for a given year/month.
// Returns true if data exists.
// ============================================================================

bool ClimatologyOverlayFactory::GetCycloneData(int year,
                                               int month,
                                               std::vector<CyclonePoint>& out) const
{
    out.clear();

    auto it = m_cycloneData.yearToTracks.find(year);
    if (it == m_cycloneData.yearToTracks.end())
        return false;

    const std::vector<int>& trackIndices = it->second;

    for (int idx : trackIndices)
    {
        if (idx < 0 || idx >= (int)m_cycloneData.tracks.size())
            continue;

        const CycloneTrack& track = m_cycloneData.tracks[idx];

        for (const CyclonePoint& pt : track.points)
        {
            if (pt.time.GetMonth() + 1 == month)
                out.push_back(pt);
        }
    }

    return !out.empty();
}


// ============================================================================
// GetStormCursorData()
// Find the nearest cyclone track + point to the cursor lat/lon.
// Returns true if a point is within threshold pixels.
// ============================================================================
bool ClimatologyOverlayFactory::GetStormCursorData(
        double lat,
        double lon,
        CycloneTrack& outTrack,
        CyclonePoint& outPoint) const
{
    if (m_cyclone_cache.empty())
        return false;

    // Convert cursor lat/lon → pixel
    double cx, cy;
    LatLonToPixel(m_vp, lat, lon, cx, cy);

    const double thresholdPx = 12.0;
    double bestDist = 1e9;
    bool found = false;

    for (const CycloneTrack& track : m_cyclone_cache)
    {
        for (const CyclonePoint& pt : track.points)
        {
            double px, py;
            LatLonToPixel(m_vp, pt.lat, pt.lon, px, py);

            double dx = px - cx;
            double dy = py - cy;
            double dist = std::sqrt(dx*dx + dy*dy);

            if (dist < thresholdPx && dist < bestDist)
            {
                bestDist = dist;
                outTrack = track;
                outPoint = pt;
                found = true;
            }
        }
    }

    return found;
}


// ============================================================================
// GetStormHistory
// Retrieve all historical storm points for a given storm ID.
// Returns true if history exists.
// ============================================================================
bool ClimatologyOverlayFactory::GetStormHistory(
        int id,
        std::vector<CyclonePoint>& out)
{
    auto it = m_cycloneData.idToIndex.find(id);
    if (it == m_cycloneData.idToIndex.end())
        return false;

    const CycloneTrack& track = m_cycloneData.tracks[it->second];
    if (!track.valid || track.points.empty())
        return false;

    out = track.points;
    return true;
}


// ============================================================================
// GetStormHistoryPoint
// Retrieve a single historical storm point by storm ID and index.
// Returns true if the point exists.
// ============================================================================
bool ClimatologyOverlayFactory::GetStormHistoryPoint(
        int id,
        int index,
        CyclonePoint& out)
{
    auto it = m_cycloneData.idToIndex.find(id);
    if (it == m_cycloneData.idToIndex.end())
        return false;

    const CycloneTrack& track = m_cycloneData.tracks[it->second];
    if (!track.valid || index < 0 || index >= (int)track.points.size())
        return false;

    out = track.points[index];
    return true;
}


// ============================================================================
// GetStormCategory()
// Compute Saffir–Simpson hurricane category from sustained wind speed (kt).
// Returns category 0–5.
// ============================================================================
int ClimatologyOverlayFactory::GetStormCategory(double wind)
{
    if (std::isnan(wind))
        return 0;

    if (wind >= 137) return 5;
    if (wind >= 113) return 4;
    if (wind >= 96)  return 3;
    if (wind >= 83)  return 2;
    if (wind >= 64)  return 1;

    return 0;
}


// ============================================================================
// GetStormColor()
// Return RGB color for storm category.
// ============================================================================
wxColour ClimatologyOverlayFactory::GetStormColor(int category)
{
    switch (category)
    {
    case 5: return wxColour(160, 0, 0);
    case 4: return wxColour(200, 40, 40);
    case 3: return wxColour(255, 80, 80);
    case 2: return wxColour(255, 120, 80);
    case 1: return wxColour(255, 160, 80);
    default: return wxColour(180, 180, 180);
    }
}


// ============================================================================
// GetStormSymbol()
// Return a wxBitmap symbol for a storm category.
// Ensure bitmap set matches: Cat 1 orange, Cat 2 deeper orange, Cat 3 red-orange
// Cat 4 red,  Cat 5 dark red,  TD gray
// ============================================================================
wxBitmap ClimatologyOverlayFactory::GetStormSymbol(int category)
{
    switch (category)
    {
    case 5: return m_bmpStormCat5;
    case 4: return m_bmpStormCat4;
    case 3: return m_bmpStormCat3;
    case 2: return m_bmpStormCat2;
    case 1: return m_bmpStormCat1;
    default: return m_bmpStormTD; // Tropical Depression / none
    }
}

// ============================================================================
// GetStormLabel()
// Return short text label for storm category.
// ============================================================================
wxString ClimatologyOverlayFactory::GetStormLabel(int category)
{
    switch (category)
    {
    case 5: return "Cat 5";
    case 4: return "Cat 4";
    case 3: return "Cat 3";
    case 2: return "Cat 2";
    case 1: return "Cat 1";
    default: return "TD"; // Tropical Depression / none
    }
}

// ============================================================================
// GetStormTooltip()
// Modern tooltip for CycloneTrack + CyclonePoint
// ============================================================================
wxString ClimatologyOverlayFactory::GetStormTooltip(
        const CycloneTrack& track,
        const CyclonePoint& pt) const
{
    wxString s;

    // ------------------------------------------------------------
    // Track-level metadata
    // ------------------------------------------------------------
    s += wxString::Format("Cyclone Track ID: %d\n", track.id);

    s += wxString::Format("Basin: %s\n",
        BasinToString(track.basin));

    s += wxString::Format("ENSO: %s\n",
        ENSOToString(track.ensoPhase));

    s += wxString::Format("Max Wind: %.1f kt\n", track.maxWind);
    s += wxString::Format("Min Pressure: %.1f hPa\n", track.minPressure);

    // ------------------------------------------------------------
    // Point-level metadata
    // ------------------------------------------------------------
    s += "\nPoint Details:\n";

    s += wxString::Format("Date: %s\n",
        pt.time.FormatISODate());

    s += wxString::Format("Lat/Lon: %.2f°, %.2f°\n",
        pt.lat, pt.lon);

    s += wxString::Format("Wind: %.1f kt\n", pt.wind);
    s += wxString::Format("Pressure: %.1f hPa\n", pt.pressure);

    s += wxString::Format("State: %s\n",
        CycloneStateToString(pt.state));

    s += wxString::Format("Point ENSO: %s\n",
		ENSOToString(pt.ensoPhase));


    return s;
}

// ============================================================================
// GetStormIntensityText()
// Return human-readable intensity text from wind speed.
// ============================================================================
wxString ClimatologyOverlayFactory::GetStormIntensityText(double wind_kt)
{
    if (std::isnan(wind_kt))
        return "Unknown";

    int cat = GetStormCategory(wind_kt);

    switch (cat)
    {
    case 5: return "Cat 5 – Extreme";
    case 4: return "Cat 4 – Very Strong";
    case 3: return "Cat 3 – Strong";
    case 2: return "Cat 2 – Moderate";
    case 1: return "Cat 1 – Weak";
    default:
        if (wind_kt >= 34)
            return "Tropical Storm";
        return "Tropical Depression";
    }
}


// ============================================================================
// GetOverlay()
// High level accessor for retrieving the active overlay grid for a given
// climatology type. Uses the current month from ClimatologyRenderParams.
// Returns nullptr if the overlay is invalid or out of range.
// ============================================================================
ClimatologyOverlay*
ClimatologyOverlayFactory::GetOverlay(OverlayType type,
                                      const ClimatologyRenderParams& p)
{
    const int month = p.currentMonth;

    if (month < 0 || month >= 12)
        return nullptr;

    if (type < 0 || type >= NUM_OVERLAYS)
        return nullptr;

    ClimatologyOverlay& O = m_pOverlay[month][type];
    return (O.m_valid ? &O : nullptr);
}


// ============================================================================
// GetOverlay() const
// Low level accessor for retrieving the overlay grid for a specific month.
// Does not perform interpolation or rendering logic. Caller must validate
// month_index and overlay validity.
// ============================================================================
const ClimatologyOverlay*
ClimatologyOverlayFactory::GetOverlay(OverlayType type,
                                      const ClimatologyRenderParams& p) const
{
    const int month = p.currentMonth;

    if (month < 0 || month >= 12)
        return nullptr;

    if (type < 0 || type >= NUM_OVERLAYS)
        return nullptr;

    const ClimatologyOverlay& O = m_pOverlay[month][type];
    return (O.m_valid ? &O : nullptr);
}


// ============================================================================
// NormalizeOverlay()
// Modern normalization: safe, clamped, unified grid compatible.
// ============================================================================
bool ClimatologyOverlayFactory::NormalizeOverlay(ClimatologyOverlay& O,
                                                 double vmin,
                                                 double vmax)
{
    if (!O.m_valid || vmax <= vmin)
        return false;

    const double range = vmax - vmin;
    const double scale = (range > 0.0) ? (1.0 / range) : 1.0;

    for (int iy = 0; iy < O.lat_count; iy++)
    {
        for (int ix = 0; ix < O.lon_count; ix++)
        {
            double v = O.value(ix, iy);
            if (std::isnan(v))
                continue;

            double nv = (v - vmin) * scale;

            if (nv < 0.0) nv = 0.0;
            if (nv > 1.0) nv = 1.0;

            O.setValue(ix, iy, nv);
        }
    }

    return true;
}

// ============================================================================
// NormalizeAllOverlays()
// Normalize every overlay (all months, all types) to 0–1 range.
// Modern version: normalize all overlays using their own min/max.
// ============================================================================
void ClimatologyOverlayFactory::NormalizeAllOverlays()
{
    for (int month = 0; month < 12; month++)
    {
        for (int type = 0; type < NUM_OVERLAYS; type++)
        {
            ClimatologyOverlay& O = m_pOverlay[month][type];
            if (!O.m_valid)
                continue;

            double vmin, vmax;
            if (!GetOverlayMinMax(O, vmin, vmax))
                continue;

            NormalizeOverlay(O, vmin, vmax);
        }
    }
}





// ============================================================================
// ComputeOverlayStatistics()
// Compute mean and standard deviation for an overlay grid.
// Returns true if valid values were found.
// ============================================================================
bool ClimatologyOverlayFactory::ComputeOverlayStatistics(const ClimatologyOverlay& O,
                                                         double& outMean,
                                                         double& outStdDev)
{
    if (!O.m_valid || O.lon_count <= 0 || O.lat_count <= 0)
        return false;

    double sum   = 0.0;
    double sumSq = 0.0;
    int    count = 0;

    for (int iy = 0; iy < O.lat_count; iy++)
    {
        for (int ix = 0; ix < O.lon_count; ix++)
        {
            double v = O.value(ix, iy);
            if (std::isnan(v))
                continue;

            sum   += v;
            sumSq += v * v;
            count++;
        }
    }

    if (count == 0)
        return false;

    outMean = sum / count;

    double variance = (sumSq / count) - (outMean * outMean);
    if (variance < 0.0)
        variance = 0.0;

    outStdDev = sqrt(variance);
    return true;
}

// ============================================================================
// ComputeAllOverlayStatistics()
// Compute mean/stddev for all overlays (all months, all types).
// ============================================================================
void ClimatologyOverlayFactory::ComputeAllOverlayStatistics()
{
    for (int month = 0; month < 12; month++)
    {
        for (int type = 0; type < NUM_OVERLAYS; type++)
        {
            ClimatologyOverlay& O = m_pOverlay[month][type];
            if (!O.m_valid)
                continue;

            double mean, stddev;
            if (!ComputeOverlayStatistics(O, mean, stddev))
                continue;

            O.m_mean   = mean;
            O.m_stddev = stddev;
        }
    }
}

// ============================================================================
// ComputeOverlayRange()
// Compute the numeric range (max - min) of an overlay grid.
// Returns true if valid values were found.
// ============================================================================
bool ClimatologyOverlayFactory::ComputeOverlayRange(const ClimatologyOverlay& O,
                                                    double& outRange)
{
    if (!O.m_valid || O.lon_count <= 0 || O.lat_count <= 0)
        return false;

    double vmin, vmax;
    if (!GetOverlayMinMax(O, vmin, vmax))
        return false;

    outRange = vmax - vmin;
    if (outRange < 0.0)
        outRange = 0.0;

    return true;
}

// ============================================================================
// ComputeAllOverlayRanges()
// Compute numeric ranges (max - min) for all overlays.
// ============================================================================
void ClimatologyOverlayFactory::ComputeAllOverlayRanges()
{
    for (int month = 0; month < 12; month++)
    {
        for (int type = 0; type < NUM_OVERLAYS; type++)
        {
            ClimatologyOverlay& O = m_pOverlay[month][type];
            if (!O.m_valid)
                continue;

            double range;
            if (!ComputeOverlayRange(O, range))
                continue;

            O.m_range = range;
        }
    }
}




// ============================================================================
// GetOverlayGradient()
// Compute gradient magnitude of overlay field at lat/lon.
// Uses central differences where possible.
// ============================================================================
double ClimatologyOverlayFactory::GetOverlayGradient(const ClimatologyOverlay& O,
                                                     double lat,
                                                     double lon)
{
    // Convert lat/lon to grid coordinates
    double gx = (lon - O.lon_min) / O.lon_step;
    double gy = (lat - O.lat_min) / O.lat_step;

    int ix = (int)floor(gx);
    int iy = (int)floor(gy);

    // Need neighbors for gradient
    if (ix <= 0 || iy <= 0 ||
        ix + 1 >= O.lon_count ||
        iy + 1 >= O.lat_count)
    {
        return NAN;
    }

    // Fetch values around the point
    double vL = O.value(ix - 1, iy);
    double vR = O.value(ix + 1, iy);
    double vD = O.value(ix, iy - 1);
    double vU = O.value(ix, iy + 1);

    if (std::isnan(vL) || std::isnan(vR) ||
        std::isnan(vD) || std::isnan(vU))
    {
        return NAN;
    }

    // Central differences
    double d_dx = (vR - vL) / (2.0 * O.lon_step);
    double d_dy = (vU - vD) / (2.0 * O.lat_step);

    // Gradient magnitude
    return sqrt(d_dx*d_dx + d_dy*d_dy);
}



// ============================================================================
// GetOverlayMinMax()
// Compute minimum and maximum values in an overlay grid.
// Returns true if valid values were found.
// ============================================================================
bool ClimatologyOverlayFactory::GetOverlayMinMax(const ClimatologyOverlay& O,
                                                 double& outMin,
                                                 double& outMax)
{
    if (!O.m_valid || O.lon_count <= 0 || O.lat_count <= 0)
        return false;

    double vmin =  std::numeric_limits<double>::infinity();
    double vmax = -std::numeric_limits<double>::infinity();

    for (int iy = 0; iy < O.lat_count; iy++)
    {
        for (int ix = 0; ix < O.lon_count; ix++)
        {
            double v = O.value(ix, iy);
            if (std::isnan(v))
                continue;

            if (v < vmin) vmin = v;
            if (v > vmax) vmax = v;
        }
    }

    if (!std::isfinite(vmin) || !std::isfinite(vmax))
        return false;

    if (vmax <= vmin)
    {
        outMin = 0.0;
        outMax = 1.0;
        return true;
    }

    outMin = vmin;
    outMax = vmax;
    return true;
}



// ============================================================================
// GetOverlayColor()
// Map a normalized overlay value (0–1) to an RGB color using the active palette.
// ============================================================================
wxColour ClimatologyOverlayFactory::GetOverlayColor(double v)
{
    if (std::isnan(v))
        return wxColour(0, 0, 0);

    if (v < 0.0) v = 0.0;
    if (v > 1.0) v = 1.0;

    const size_t N = m_colorPalette.size();
    if (N == 0)
        return wxColour(0, 0, 0);

    size_t idx = static_cast<size_t>(v * (N - 1));
    if (idx >= N)
        idx = N - 1;

    return m_colorPalette[idx];
}


// ============================================================================
// GetOverlayColorScaled
// Map an unnormalized overlay value to a color using min/max scaling.
// ============================================================================
wxColour ClimatologyOverlayFactory::GetOverlayColorScaled(double value,
                                                          double vmin,
                                                          double vmax)
{
    if (std::isnan(value))
        return wxColour(0, 0, 0);

    if (vmax <= vmin)
        return wxColour(0, 0, 0);

    double nv = (value - vmin) / (vmax - vmin);

    if (nv < 0.0) nv = 0.0;
    if (nv > 1.0) nv = 1.0;

    const size_t N = m_colorPalette.size();
    if (N == 0)
        return wxColour(0, 0, 0);

    size_t idx = static_cast<size_t>(nv * (N - 1));
    if (idx >= N)
        idx = N - 1;

    return m_colorPalette[idx];
}


// ============================================================================
// ApplyColorMap()
// Convert a normalized overlay grid (0–1) into a color grid using palette.
// ============================================================================
void ClimatologyOverlayFactory::ApplyColorMap(const ClimatologyOverlay& O,
                                              ClimatologyColorGrid& outGrid)
{
    if (!O.m_valid)
        return;

    outGrid.allocate(O.lon_count, O.lat_count);

    for (int iy = 0; iy < O.lat_count; iy++)
    {
        for (int ix = 0; ix < O.lon_count; ix++)
        {
            double v = O.value(ix, iy);

            if (std::isnan(v))
            {
                outGrid.set(ix, iy, wxColour(0, 0, 0));
                continue;
            }

            // v is already normalized (0–1)
            outGrid.set(ix, iy, GetOverlayColor(v));
        }
    }
}




// ============================================================================
// ApplyColorMapScaled()
// Convert an unnormalized overlay grid into a color grid using min/max scaling.
// ============================================================================
void ClimatologyOverlayFactory::ApplyColorMapScaled(const ClimatologyOverlay& O,
                                                    ClimatologyColorGrid& outGrid,
                                                    double vmin,
                                                    double vmax)
{
    if (!O.m_valid)
        return;

    outGrid.allocate(O.lon_count, O.lat_count);

    for (int iy = 0; iy < O.lat_count; iy++)
    {
        for (int ix = 0; ix < O.lon_count; ix++)
        {
            double v = O.value(ix, iy);
            if (std::isnan(v))
            {
                outGrid.set(ix, iy, wxColour(0, 0, 0));
                continue;
            }

            outGrid.set(ix, iy, GetOverlayColorScaled(v, vmin, vmax));
        }
    }
}

// ============================================================================
// GenerateColorGrid()
// Produce a color grid from a normalized overlay (0–1 values).
// ============================================================================
void ClimatologyOverlayFactory::GenerateColorGrid(const ClimatologyOverlay& O,
                                                  ClimatologyColorGrid& outGrid)
{
    if (!O.m_valid)
        return;

    outGrid.allocate(O.lon_count, O.lat_count);

    for (int iy = 0; iy < O.lat_count; iy++)
    {
        for (int ix = 0; ix < O.lon_count; ix++)
        {
            double v = O.value(ix, iy);
            if (std::isnan(v))
            {
                outGrid.set(ix, iy, wxColour(0, 0, 0));
                continue;
            }

            outGrid.set(ix, iy, GetOverlayColor(v));
        }
    }
}

// ============================================================================
// GenerateColorGridScaled()
// Produce a color grid from an unnormalized overlay using min/max scaling.
// ============================================================================
void ClimatologyOverlayFactory::GenerateColorGridScaled(const ClimatologyOverlay& O,
                                                        ClimatologyColorGrid& outGrid,
                                                        double vmin,
                                                        double vmax)
{
    if (!O.m_valid)
        return;

    outGrid.allocate(O.lon_count, O.lat_count);

    for (int iy = 0; iy < O.lat_count; iy++)
    {
        for (int ix = 0; ix < O.lon_count; ix++)
        {
            double v = O.value(ix, iy);
            if (std::isnan(v))
            {
                outGrid.set(ix, iy, wxColour(0, 0, 0));
                continue;
            }

            outGrid.set(ix, iy, GetOverlayColorScaled(v, vmin, vmax));
        }
    }
}


// ============================================================================
// GenerateUnifiedColorGridScaled()
// Modern version: blend overlays using per‑overlay vmin/vmax and final
// global scaling (global_vmin/global_vmax).
// ============================================================================
void ClimatologyOverlayFactory::GenerateUnifiedColorGridScaled(
        const std::vector<const ClimatologyOverlay*>& overlays,
        const std::vector<double>& vmins,
        const std::vector<double>& vmaxs,
        ClimatologyColorGrid& outGrid,
        double global_vmin,
        double global_vmax)
{
    if (overlays.empty())
        return;

    const ClimatologyOverlay* base = overlays[0];
    if (!base || !base->m_valid)
        return;

    const int NX = base->lon_count;
    const int NY = base->lat_count;

    outGrid.allocate(NX, NY);

    // ------------------------------------------------------------------------
    // Helper: normalize a value using per‑overlay vmin/vmax
    // ------------------------------------------------------------------------
    auto get_norm = [&](const ClimatologyOverlay* O, int ix, int iy,
                        double vmin, double vmax) -> double
    {
        if (!O || !O->m_valid)
            return std::numeric_limits<double>::quiet_NaN();

        double v = O->value(ix, iy);
        if (std::isnan(v))
            return std::numeric_limits<double>::quiet_NaN();

        double r = vmax - vmin;
        if (r <= 0.0)
            return std::numeric_limits<double>::quiet_NaN();

        return (v - vmin) / r;
    };

    // ------------------------------------------------------------------------
    // Main loop: average normalized values across overlays
    // ------------------------------------------------------------------------
    for (int iy = 0; iy < NY; iy++)
    {
        for (int ix = 0; ix < NX; ix++)
        {
            double sum = 0.0;
            int count = 0;

            for (size_t k = 0; k < overlays.size(); k++)
            {
                double nv = get_norm(overlays[k], ix, iy,
                                     vmins[k], vmaxs[k]);
                if (std::isnan(nv))
                    continue;

                sum += nv;
                count++;
            }

            double blended = (count > 0) ? (sum / count) : 0.0;

            // ----------------------------------------------------------------
            // Final global scaling
            // ----------------------------------------------------------------
            double gr = global_vmax - global_vmin;
            double scaled = (gr > 0.0)
                ? (blended * gr + global_vmin)
                : blended;

            // Clamp
            if (scaled < 0.0) scaled = 0.0;
            if (scaled > 1.0) scaled = 1.0;

            outGrid.set(ix, iy, GetOverlayColor(scaled));
        }
    }
}



// ============================================================================
// GenerateUnifiedColorGridWeighted()
// Modern version: weighted blend of normalized overlay values.
// ============================================================================
void ClimatologyOverlayFactory::GenerateUnifiedColorGridWeighted(
        const std::vector<const ClimatologyOverlay*>& overlays,
        const std::vector<double>& weights,
        ClimatologyColorGrid& outGrid)
{
    if (overlays.empty() || overlays.size() != weights.size())
        return;

    const ClimatologyOverlay* base = overlays[0];
    if (!base || !base->m_valid)
        return;

    const int NX = base->lon_count;
    const int NY = base->lat_count;

    outGrid.allocate(NX, NY);

    for (int iy = 0; iy < NY; iy++)
    {
        for (int ix = 0; ix < NX; ix++)
        {
            double sum = 0.0;
            double wsum = 0.0;

            for (size_t k = 0; k < overlays.size(); k++)
            {
                const ClimatologyOverlay* O = overlays[k];
                double w = weights[k];

                if (!O || !O->m_valid || w <= 0.0)
                    continue;

                double v = O->value(ix, iy);
                if (std::isnan(v))
                    continue;

                sum += v * w;
                wsum += w;
            }

            if (wsum <= 0.0)
            {
                outGrid.set(ix, iy, wxColour(0, 0, 0));
                continue;
            }

            double blended = sum / wsum;
            outGrid.set(ix, iy, GetOverlayColor(blended));
        }
    }
}



// ============================================================================
// GenerateUnifiedColorGridMultiRange()
// Modern version: per-overlay normalization using vmins/vmaxs.
// ============================================================================
void ClimatologyOverlayFactory::GenerateUnifiedColorGridMultiRange(
        const std::vector<const ClimatologyOverlay*>& overlays,
        const std::vector<double>& vmins,
        const std::vector<double>& vmaxs,
        ClimatologyColorGrid& outGrid)
{
    if (overlays.empty() ||
        overlays.size() != vmins.size() ||
        overlays.size() != vmaxs.size())
        return;

    const ClimatologyOverlay* base = overlays[0];
    if (!base || !base->m_valid)
        return;

    const int NX = base->lon_count;
    const int NY = base->lat_count;

    outGrid.allocate(NX, NY);

    for (int iy = 0; iy < NY; iy++)
    {
        for (int ix = 0; ix < NX; ix++)
        {
            double sum = 0.0;
            int count = 0;

            for (size_t k = 0; k < overlays.size(); k++)
            {
                const ClimatologyOverlay* O = overlays[k];
                double vmin = vmins[k];
                double vmax = vmaxs[k];

                if (!O || !O->m_valid || vmax <= vmin)
                    continue;

                double v = O->value(ix, iy);
                if (std::isnan(v))
                    continue;

                double nv = (v - vmin) / (vmax - vmin);
                if (nv < 0.0) nv = 0.0;
                if (nv > 1.0) nv = 1.0;

                sum += nv;
                count++;
            }

            if (count == 0)
            {
                outGrid.set(ix, iy, wxColour(0, 0, 0));
                continue;
            }

            double blended = sum / count;
            outGrid.set(ix, iy, GetOverlayColor(blended));
        }
    }
}



// ============================================================================
// GenerateUnifiedColorGridMultiRangeScaled()
// Modern version: per-overlay normalization + global scaling.
// ============================================================================
void ClimatologyOverlayFactory::GenerateUnifiedColorGridMultiRangeScaled(
        const std::vector<const ClimatologyOverlay*>& overlays,
        const std::vector<double>& vmins,
        const std::vector<double>& vmaxs,
        ClimatologyColorGrid& outGrid,
        double global_vmin,
        double global_vmax)
{
    if (overlays.empty() ||
        overlays.size() != vmins.size() ||
        overlays.size() != vmaxs.size())
        return;

    const ClimatologyOverlay* base = overlays[0];
    if (!base || !base->m_valid)
        return;

    const int NX = base->lon_count;
    const int NY = base->lat_count;

    outGrid.allocate(NX, NY);

    double gr = global_vmax - global_vmin;
    if (gr <= 0.0)
        gr = 1.0;

    for (int iy = 0; iy < NY; iy++)
    {
        for (int ix = 0; ix < NX; ix++)
        {
            double sum = 0.0;
            int count = 0;

            for (size_t k = 0; k < overlays.size(); k++)
            {
                const ClimatologyOverlay* O = overlays[k];
                double vmin = vmins[k];
                double vmax = vmaxs[k];

                if (!O || !O->m_valid || vmax <= vmin)
                    continue;

                double v = O->value(ix, iy);
                if (std::isnan(v))
                    continue;

                double nv = (v - vmin) / (vmax - vmin);
                if (nv < 0.0) nv = 0.0;
                if (nv > 1.0) nv = 1.0;

                sum += nv;
                count++;
            }

            if (count == 0)
            {
                outGrid.set(ix, iy, wxColour(0, 0, 0));
                continue;
            }

            double blended = sum / count;

            double gv = (blended - global_vmin) / gr;
            if (gv < 0.0) gv = 0.0;
            if (gv > 1.0) gv = 1.0;

            outGrid.set(ix, iy, GetOverlayColor(gv));
        }
    }
}


// ============================================================================
// GenerateUnifiedColorGridMultiRangeWeighted()
// Blend multiple overlays using per‑overlay weights, each overlay having its
// own (vmin, vmax) range. Produces a normalized blended grid (0–1).
// ============================================================================
void ClimatologyOverlayFactory::GenerateUnifiedColorGridMultiRangeWeighted(
        const std::vector<const ClimatologyOverlay*>& overlays,
        const std::vector<double>& weights,
        const std::vector<double>& vmins,
        const std::vector<double>& vmaxs,
        ClimatologyColorGrid& outGrid)
{
    if (overlays.empty() ||
        overlays.size() != weights.size() ||
        overlays.size() != vmins.size() ||
        overlays.size() != vmaxs.size())
        return;

    const ClimatologyOverlay* base = overlays[0];
    if (!base || !base->m_valid)
        return;

    outGrid.allocate(base->lon_count, base->lat_count);

    for (int iy = 0; iy < base->lat_count; iy++)
    {
        for (int ix = 0; ix < base->lon_count; ix++)
        {
            double sum = 0.0;
            double wsum = 0.0;

            for (size_t i = 0; i < overlays.size(); i++)
            {
                const ClimatologyOverlay* O = overlays[i];
                double w = weights[i];
                double vmin = vmins[i];
                double vmax = vmaxs[i];

                if (!O || !O->m_valid || w <= 0.0 || vmax <= vmin)
                    continue;

                double v = O->value(ix, iy);
                if (std::isnan(v))
                    continue;

                // Normalize using per-overlay range
                double nv = (v - vmin) / (vmax - vmin);
                if (nv < 0.0) nv = 0.0;
                if (nv > 1.0) nv = 1.0;

                sum += nv * w;
                wsum += w;
            }

            if (wsum <= 0.0)
            {
                outGrid.set(ix, iy, wxColour(0, 0, 0));
                continue;
            }

            double blended = sum / wsum;
            outGrid.set(ix, iy, GetOverlayColor(blended));
        }
    }
}

// ============================================================================
// GenerateUnifiedColorGridMultiRangeWeightedScaled()
// Blend multiple overlays using per‑overlay weights, each overlay having its
// own (vmin, vmax) range, then apply a final global scaling before mapping
// to color.
// ============================================================================
void ClimatologyOverlayFactory::GenerateUnifiedColorGridMultiRangeWeightedScaled(
        const std::vector<const ClimatologyOverlay*>& overlays,
        const std::vector<double>& weights,
        const std::vector<double>& vmins,
        const std::vector<double>& vmaxs,
        ClimatologyColorGrid& outGrid,
        double global_vmin,
        double global_vmax)
{
    if (overlays.empty() ||
        overlays.size() != weights.size() ||
        overlays.size() != vmins.size() ||
        overlays.size() != vmaxs.size())
        return;

    const ClimatologyOverlay* base = overlays[0];
    if (!base || !base->m_valid)
        return;

    outGrid.allocate(base->lon_count, base->lat_count);

    for (int iy = 0; iy < base->lat_count; iy++)
    {
        for (int ix = 0; ix < base->lon_count; ix++)
        {
            double sum = 0.0;
            double wsum = 0.0;

            for (size_t i = 0; i < overlays.size(); i++)
            {
                const ClimatologyOverlay* O = overlays[i];
                double w = weights[i];
                double vmin = vmins[i];
                double vmax = vmaxs[i];

                if (!O || !O->m_valid || w <= 0.0 || vmax <= vmin)
                    continue;

                double v = O->value(ix, iy);
                if (std::isnan(v))
                    continue;

                // Normalize using per-overlay range
                double nv = (v - vmin) / (vmax - vmin);
                if (nv < 0.0) nv = 0.0;
                if (nv > 1.0) nv = 1.0;

                sum += nv * w;
                wsum += w;
            }

            if (wsum <= 0.0)
            {
                outGrid.set(ix, iy, wxColour(0, 0, 0));
                continue;
            }

            double blended = sum / wsum;

            // Apply final global scaling
            if (global_vmax > global_vmin)
            {
                double gv = (blended - global_vmin) / (global_vmax - global_vmin);
                if (gv < 0.0) gv = 0.0;
                if (gv > 1.0) gv = 1.0;
                outGrid.set(ix, iy, GetOverlayColor(gv));
            }
            else
            {
                outGrid.set(ix, iy, wxColour(0, 0, 0));
            }
        }
    }
}


// ============================================================================
// ApplyGlobalScalingToUnifiedGrid()
// Take an already-blended unified grid (values in [0,1] or arbitrary range),
// compute its actual min/max, then apply a final global scaling
// (global_vmin/global_vmax) before mapping to color.
// This is a pure post-process step used by multi-overlay pipelines.
// ============================================================================
void ClimatologyOverlayFactory::ApplyGlobalScalingToUnifiedGrid(
        const std::vector<double>& blended_cache,
        int NX,
        int NY,
        ClimatologyColorGrid& outGrid,
        double global_vmin,
        double global_vmax)
{
    if (blended_cache.empty() || NX <= 0 || NY <= 0)
        return;

    outGrid.allocate(NX, NY);

    // ------------------------------------------------------------------------
    // Compute min/max of blended_cache
    // ------------------------------------------------------------------------
    double vmin =  std::numeric_limits<double>::infinity();
    double vmax = -std::numeric_limits<double>::infinity();

    for (double v : blended_cache)
    {
        if (!std::isnan(v))
        {
            if (v < vmin) vmin = v;
            if (v > vmax) vmax = v;
        }
    }

    const double range = vmax - vmin;

    // ------------------------------------------------------------------------
    // Apply normalization + global scaling
    // ------------------------------------------------------------------------
    for (int iy = 0; iy < NY; iy++)
    {
        for (int ix = 0; ix < NX; ix++)
        {
            double v = blended_cache[iy * NX + ix];

            if (std::isnan(v) || range <= 0.0)
            {
                outGrid.set(ix, iy, wxColour(0, 0, 0));
                continue;
            }

            // Normalize to [0,1]
            double nv = (v - vmin) / range;
            if (nv < 0.0) nv = 0.0;
            if (nv > 1.0) nv = 1.0;

            // Apply final global scaling
            if (global_vmax > global_vmin)
            {
                double sv = (nv - global_vmin) / (global_vmax - global_vmin);
                if (sv < 0.0) sv = 0.0;
                if (sv > 1.0) sv = 1.0;
                outGrid.set(ix, iy, GetOverlayColor(sv));
            }
            else
            {
                outGrid.set(ix, iy, GetOverlayColor(nv));
            }
        }
    }
}

// ============================================================================
// GenerateUnifiedColorGrid()
// Dispatcher: selects the correct unified‑grid operator based on mode.
// This is the single entry point used by the climatology renderer.
// ============================================================================
void ClimatologyOverlayFactory::GenerateUnifiedColorGrid(
        ClimatologyColorGrid& outGrid,
        const std::vector<const ClimatologyOverlay*>& overlays,
        const DisplayMode mode,
        const std::vector<double>& weights,
        const std::vector<double>& vmins,
        const std::vector<double>& vmaxs,
        double global_vmin,
        double global_vmax)
{
    // Safety: no overlays → nothing to do
    if (overlays.empty())
        return;

    // Helper: build default weights if none provided
    auto make_default_weights = [&](size_t n) {
        std::vector<double> w(n, 1.0);
        return w;
    };

    switch (mode)
    {
        // ------------------------------------------------------------
        // Single overlay, scaled to its own min/max
        // ------------------------------------------------------------
        case DisplayMode::SingleScaled:
        {
            // Use per‑overlay vmin/vmax (vectors) + global scaling
            GenerateUnifiedColorGridScaled(
                overlays,
                vmins,
                vmaxs,
                outGrid,
                global_vmin,
                global_vmax);
            return;
        }

        // ------------------------------------------------------------
        // Weighted multi‑overlay blend (full multi‑range + global scaling)
        // ------------------------------------------------------------
        case DisplayMode::WeightedBlend:
        {
            // Use provided weights + per‑overlay ranges + global scaling
            GenerateUnifiedColorGridMultiRangeWeightedScaled(
                overlays,
                weights,
                vmins,
                vmaxs,
                outGrid,
                global_vmin,
                global_vmax);
            return;
        }

        // ------------------------------------------------------------
        // Additive / Max / Min / Mean / Median composites
        // All funneled through the same multi‑range weighted‑scaled engine.
        // ------------------------------------------------------------
        case DisplayMode::Additive:
        case DisplayMode::MaxComposite:
        case DisplayMode::MinComposite:
        case DisplayMode::MeanComposite:
        case DisplayMode::MedianComposite:
        {
            // For now, treat all these as equal‑weight blends over
            // the selected overlays + per‑overlay ranges + global scaling.
            std::vector<double> w =
                weights.empty() ? make_default_weights(overlays.size()) : weights;

            GenerateUnifiedColorGridMultiRangeWeightedScaled(
                overlays,
                w,
                vmins,
                vmaxs,
                outGrid,
                global_vmin,
                global_vmax);
            return;
        }

        // ------------------------------------------------------------
        // Default: fallback to SingleScaled behavior
        // ------------------------------------------------------------
        default:
        {
            GenerateUnifiedColorGridScaled(
                overlays,
                vmins,
                vmaxs,
                outGrid,
                global_vmin,
                global_vmax);
            return;
        }
    }
}


// ============================================================================
// GatherOverlaysForMode
// Selects which overlays should be combined based on the display mode,
// the selected climatology type, and the current month/season.
// This is called BEFORE GenerateUnifiedColorGrid().
// ============================================================================
std::vector<const ClimatologyOverlay*>
ClimatologyOverlayFactory::GatherOverlaysForMode(DisplayMode mode,
                                                 OverlayType type,
                                                 int month_index,
                                                 const ClimatologyRenderParams& p) const
{
    std::vector<const ClimatologyOverlay*> result;

    // Safety: ensure month index is valid
    if (month_index < 0 || month_index >= 12)
        return result;

    // ------------------------------------------------------------------------
    // 1. Single-overlay modes
    // ------------------------------------------------------------------------
    if (mode == DisplayMode::SingleScaled)
    {
        const ClimatologyOverlay* O = GetOverlay(type, p);
        if (O && O->m_valid)
            result.push_back(O);
        return result;
    }

    // ------------------------------------------------------------------------
    // 2. Multi-overlay modes (weighted, mean, median, max, min, additive)
    // These modes require multiple overlays for the same climatology type.
    // Example: seasonal composites, ensemble models, multi-source blends.
    // ------------------------------------------------------------------------

    // Convert registry vector (non-const) → const vector
    const auto vec_const = MakeConstVector(m_overlay_map.at(type));


    for (const auto& overlay_ptr : vec_const)
    {
        if (!overlay_ptr)
            continue;

        // Select only overlays matching the requested month
        if (overlay_ptr->month_index != month_index)
            continue;

        if (overlay_ptr->m_valid)
            result.push_back(overlay_ptr);
    }

    return result;
}


