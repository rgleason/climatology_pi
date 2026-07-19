// ============================================================================
// ClimatologyOverlayFactory.cpp  (Model A — unified render params, safe‑clean)
// ============================================================================

#include <GL/glew.h>
#include <wx/glcanvas.h>
#include <GL/gl.h>

#include <wx/wx.h>
#include <wx/wfstream.h>
#include <wx/zstream.h>
#include <wx/textfile.h>
#include <wx/dc.h>
#include <wx/dcclient.h>
#include <wx/log.h>
#include <wx/notifmsg.h>
#include <wx/filename.h>
#include <wx/tokenzr.h>

#include <memory>
#include <cmath>
#include <algorithm>
#include <functional>
#include <map>

#include "ocpn_plugin.h"
#include "IsoBarMap.h"
#include "ClimatologyOverlayFactory.h"
#include "ClimatologyDataModel.h"
#include "climatology_shaders.h"
#include "ClimatologyCoord.h"
#include "ClimatologyConstants.h"
#include "ClimatologyEnums.h"
#include "CycloneFilterParams.h"
#include "OverlayMapping.h"

#include "defs.h"

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
    m_params = p;
}

// ============================================================================
// SetCycloneFilter()
// Store cyclone filter parameters.
// ============================================================================
void ClimatologyOverlayFactory::SetCycloneFilter(const CycloneFilterParams& params)
{
    m_cycloneParams = params;
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
// Earlier version  LoadInternal()  - older -correct
// ===========================================================================

bool ClimatologyOverlayFactory::LoadInternal(wxGenericProgressDialog* progress)
{
    bool ok = true;

    if (progress)
        progress->Pulse(_("Loading NOAA scalar datasets…"));
    ok &= LoadScalarNOAA();
    if (!ok) return false;

    if (progress)
        progress->Pulse(_("Loading NOAA wind/current datasets…"));
    ok &= LoadWindNOAA();
    ok &= LoadCurrentNOAA();
    if (!ok) return false;

    if (progress)
        progress->Pulse(_("Mapping NOAA datasets into unified model…"));

    for (int m = 0; m < 12; m++)
    {
        LoadScalarField(OVERLAY_PRESSURE,   m, m_slp[m],
                        SLP_LAT, SLP_LON,
                        slp_lat0, slp_lon0, slp_latStep, slp_lonStep);

        LoadScalarField(OVERLAY_SEA_TEMP,        m, m_sst[m],
                        SST_LAT, SST_LON,
                        sst_lat0, sst_lon0, sst_latStep, sst_lonStep);

        LoadScalarField(OVERLAY_AIR_TEMP,   m, m_at[m],
                        AT_LAT, AT_LON,
                        at_lat0, at_lon0, at_latStep, at_lonStep);

        LoadScalarField(OVERLAY_CLOUD,      m, m_cld[m],
                        CLD_LAT, CLD_LON,
                        cld_lat0, cld_lon0, cld_latStep, cld_lonStep);

        LoadScalarField(OVERLAY_PRECIP,     m, m_precip[m],
                        PCP_LAT, PCP_LON,
                        pcp_lat0, pcp_lon0, pcp_latStep, pcp_lonStep);

        LoadScalarField(OVERLAY_RH,         m, m_rhum[m],
                        RH_LAT, RH_LON,
                        rh_lat0, rh_lon0, rh_latStep, rh_lonStep);

        LoadScalarField(OVERLAY_LIGHTNING,  m, m_lightn[m],
                        LGT_LAT, LGT_LON,
                        lgt_lat0, lgt_lon0, lgt_latStep, lgt_lonStep);

        LoadVectorField(OVERLAY_WIND,    m,
                        m_WindData[m]->u,
                        m_WindData[m]->v,
                        WIND_LAT, WIND_LON,
                        wind_lat0, wind_lon0, wind_latStep, wind_lonStep);

        LoadVectorField(OVERLAY_CURRENT, m,
                        m_CurrentData[m]->u,
                        m_CurrentData[m]->v,
                        CUR_LAT, CUR_LON,
                        cur_lat0, cur_lon0, cur_latStep, cur_lonStep);
    }

    LoadScalarField(OVERLAY_SEA_DEPTH, 0,
                    m_seadepth,
                    DEP_LAT, DEP_LON,
                    dep_lat0, dep_lon0, dep_latStep, dep_lonStep);

    if (progress)
        progress->Pulse(_("Averaging monthly datasets…"));
    ok &= AverageWindData();
    ok &= AverageCurrentData();
    if (!ok) return false;

    if (progress)
        progress->Pulse(_("Loading cyclone + ENSO datasets…"));
    // your existing cyclone + ENSO loaders here

    return ok;
}

// ===============================================================
// Unified mapping loaders - older
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
// Raw NOAA Loaders  - older
// ======================================================================

bool ClimatologyOverlayFactory::LoadScalarNOAA()
{
    bool ok = true;

    for (int m = 0; m < 12; m++)
    {
        if (!m_slp[m])     m_slp[m]     = new float[SLP_LAT * SLP_LON];
        if (!m_sst[m])     m_sst[m]     = new float[SST_LAT * SST_LON];
        if (!m_at[m])      m_at[m]      = new float[AT_LAT  * AT_LON];
        if (!m_cld[m])     m_cld[m]     = new float[CLD_LAT * CLD_LON];
        if (!m_precip[m])  m_precip[m]  = new float[PCP_LAT * PCP_LON];
        if (!m_rhum[m])    m_rhum[m]    = new float[RH_LAT  * RH_LON];
        if (!m_lightn[m])  m_lightn[m]  = new float[LGT_LAT * LGT_LON];

        ok &= ReadNOAAFile("slp",     m, m_slp[m],     SLP_LAT, SLP_LON);
        ok &= ReadNOAAFile("sst",     m, m_sst[m],     SST_LAT, SST_LON);
        ok &= ReadNOAAFile("at",      m, m_at[m],      AT_LAT,  AT_LON);
        ok &= ReadNOAAFile("cloud",   m, m_cld[m],     CLD_LAT, CLD_LON);
        ok &= ReadNOAAFile("precip",  m, m_precip[m],  PCP_LAT, PCP_LON);
        ok &= ReadNOAAFile("rhum",    m, m_rhum[m],    RH_LAT,  RH_LON);
        ok &= ReadNOAAFile("lightn",  m, m_lightn[m],  LGT_LAT, LGT_LON);
    }

    if (!m_seadepth)
        m_seadepth = new float[DEP_LAT * DEP_LON];

    ok &= ReadNOAAFile("seadepth", 0, m_seadepth, DEP_LAT, DEP_LON);

    return ok;
}

bool ClimatologyOverlayFactory::LoadWindNOAA()
{
    bool ok = true;

    for (int m = 0; m < 12; m++)
    {
        if (!m_WindData[m])
            m_WindData[m] = new WindData(WIND_LAT, WIND_LON);

        ok &= ReadNOAAFile("wind_u", m, m_WindData[m]->u, WIND_LAT, WIND_LON);
        ok &= ReadNOAAFile("wind_v", m, m_WindData[m]->v, WIND_LAT, WIND_LON);
    }

    return ok;
}

bool ClimatologyOverlayFactory::LoadCurrentNOAA()
{
    bool ok = true;

    for (int m = 0; m < 12; m++)
    {
        if (!m_CurrentData[m])
            m_CurrentData[m] = new CurrentData(CUR_LAT, CUR_LON);

        ok &= ReadNOAAFile("current_u", m, m_CurrentData[m]->u, CUR_LAT, CUR_LON);
        ok &= ReadNOAAFile("current_v", m, m_CurrentData[m]->v, CUR_LAT, CUR_LON);
    }

    return ok;
}

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

// ====================================================================
// HasDataFor / getValueMonth / getCurValue / BuildOverlayData  - Older
// ======================================================================

bool ClimatologyOverlayFactory::HasDataFor(int setting, int month)
{
    if (month < 0 || month >= 12)
        return false;

    switch (setting)
    {
    case OVERLAY_WIND:
        return m_WindData[month] != nullptr;

    case OVERLAY_CURRENT:
        return m_CurrentData[month] != nullptr;

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


// Earlier versions rewritten for compatibility with rest of file.
double ClimatologyOverlayFactory::getValueMonth(enum Coord coord,
                                                int setting,
                                                double lat, double lon,
                                                int month)
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

double ClimatologyOverlayFactory::getCurValue(enum Coord coord,
                                              int setting,
                                              double lat, double lon,
                                              const ClimatologyRenderParams& p)
{
    int m1 = p.cyclone.currentMonth;
    int m2 = p.cyclone.nextMonth;
    double d = p.cyclone.monthInterpolation;

    double v1 = getValueMonth(coord, setting, lat, lon, m1);
    double v2 = getValueMonth(coord, setting, lat, lon, m2);

    if (std::isnan(v1) || std::isnan(v2))
        return NAN;

    return v1 * (1.0 - d) + v2 * d;
}



/* Earlier versions - correct

double ClimatologyOverlayFactory::getValueMonth(enum Coord coord,
                                                int setting,
                                                double lat, double lon,
                                                int month)
{
    if (!HasDataFor(setting, month))
        return NAN;

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

    double val = m_data[setting][month].lookup(lat, lon);
    if (std::isnan(val))
        return NAN;

    return val;
}

double ClimatologyOverlayFactory::getCurValue(enum Coord coord, int setting,
                                              double lat, double lon,
                                              const ClimatologyRenderParams& p)
{
    int m1 = p.cyclone.currentMonth;
    int m2 = p.cyclone.nextMonth;
    double d = p.cyclone.monthInterpolation;

    double v1 = getValueMonth(coord, setting, lat, lon, m1);
    double v2 = getValueMonth(coord, setting, lat, lon, m2);

    if (std::isnan(v1) || std::isnan(v2))
        return NAN;

    return v1 * (1.0 - d) + v2 * d;
}

*/ 

bool ClimatologyOverlayFactory::BuildOverlayData(ClimatologyOverlay& O,
                                                 int setting,
                                                 int month)
{
    if (O.m_data)
        return true;

    const int w = O.m_width;
    const int h = O.m_height;

    O.m_data = new unsigned char[w * h * 4];

    Coord coord = (setting == OVERLAY_WIND || setting == OVERLAY_CURRENT)
                  ? MAG
                  : SCALAR;

    for (int y = 0; y < h; y++)
    {
        double merc = 1.0 - 2.0 * (double)y / (double)(h - 1);
        double lat  = rad2deg(atan(sinh(merc * M_PI)));

        for (int x = 0; x < w; x++)
        {
            double lon = 360.0 * (double)x / (double)(w - 1);
            double v = getValueMonth(coord, setting, lat, lon, month);

            int idx = 4 * (y * w + x);

            if (std::isnan(v))
            {
                O.m_data[idx + 0] = 0;
                O.m_data[idx + 1] = 0;
                O.m_data[idx + 2] = 0;
                O.m_data[idx + 3] = 0;
            }
            else
            {
                unsigned char r, g, b;
                ColorMap(setting, v, r, g, b);

                O.m_data[idx + 0] = r;
                O.m_data[idx + 1] = g;
                O.m_data[idx + 2] = b;
                O.m_data[idx + 3] = 255;
            }
        }
    }

    return true;
}


// Lookup lookup implementations for ClimatologyMonthlyData

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




/*
// ============================================================================
// LoadInternal()   Newer  - Remove
// Load unified NOAA scalar + vector datasets.
// ============================================================================
bool ClimatologyOverlayFactory::LoadInternal(wxGenericProgressDialog* progress)
{
    wxLogMessage("Climatology: LoadInternal() starting");

    bool ok = true;
    wxString dataDir = m_dataDir;

    // --- Load scalar overlays ------------------------------------------------
    if (progress) progress->Pulse(_("Loading scalar climatology datasets…"));

    struct ScalarSpec { int setting; const char* name; };
    ScalarSpec scalars[] = {
        { OVERLAY_PRESSURE,      "sealevelpressure" },
        { OVERLAY_SEA_TEMP,      "seasurfacetemperature" },
        { OVERLAY_AIR_TEMP,      "airtemperature" },
        { OVERLAY_CLOUD,         "cloud" },
        { OVERLAY_PRECIP,        "precipitation" },
        { OVERLAY_RH,            "relativehumidity" },
        { OVERLAY_LIGHTNING,     "lightning" }
    };

    for (auto& S : scalars)
    {
        for (int m = 0; m < 12; m++)
        {
            int rows, cols;
            double lat0, lon0, latStep, lonStep;

            float* buf = ReadNOAAFileUnified(
                S.name, m,
                rows, cols,
                lat0, lon0,
                latStep, lonStep);

            if (!buf)
                return false;

            LoadScalarFieldUnified(S.setting, m,
                                   buf, rows, cols,
                                   lat0, lon0, latStep, lonStep);

            delete[] buf;
        }
    }

    // --- Load sea depth ------------------------------------------------------
    {
        int rows, cols;
        double lat0, lon0, latStep, lonStep;

        float* buf = ReadNOAAFileUnified(
            "seadepth", 0,
            rows, cols,
            lat0, lon0,
            latStep, lonStep);

        if (!buf)
            return false;

        LoadScalarFieldUnified(OVERLAY_SEA_DEPTH, 0,
                               buf, rows, cols,
                               lat0, lon0, latStep, lonStep);

        delete[] buf;
    }

    // --- Load vector overlays ------------------------------------------------
    if (progress) progress->Pulse(_("Loading wind/current datasets…"));

    struct VectorSpec { int setting; const char* prefix; };
    VectorSpec vectors[] = {
        { OVERLAY_WIND,    "wind" },
        { OVERLAY_CURRENT, "current" }
    };

    for (auto& V : vectors)
    {
        for (int m = 0; m < 12; m++)
        {
            int rows, cols;
            double lat0, lon0, latStep, lonStep;

            float* u = ReadNOAAFileUnified(
                wxString::Format("%s%02d_u", V.prefix, m+1),
                m,
                rows, cols,
                lat0, lon0,
                latStep, lonStep);

            float* v = ReadNOAAFileUnified(
                wxString::Format("%s%02d_v", V.prefix, m+1),
                m,
                rows, cols,
                lat0, lon0,
                latStep, lonStep);

            if (!u || !v)
                return false;

            LoadVectorFieldUnified(V.setting, m,
                                   u, v,
                                   rows, cols,
                                   lat0, lon0, latStep, lonStep);

            delete[] u;
            delete[] v;
        }
    }

    // --- Cyclones + ENSO -----------------------------------------------------
    if (progress) progress->Pulse(_("Loading cyclone + ENSO datasets…"));

    m_hasCycloneData = !m_cyclones.empty();
    m_hasENSOData    = LoadENSODataFromCSV("enso.csv");

	// Apply filter rules to loaded tracks
	ApplyCycloneFilter();

    m_bCompletedLoading = ok;
    wxLogMessage("Climatology: LoadInternal() completed");

    return ok;
}

// ============================================================================
// ApplyCycloneFilters()
// Where Cyclone filters are set. Then this is called by LoadInternal()
// ============================================================================
//  Timeline filtering is optional    F.enableTimeline


void ClimatologyOverlayFactory::ApplyCycloneFilter()
{
    m_cyclone_cache.clear();

    const CycloneFilterParams& F = m_cycloneParams;

    // Convert wxDateTime → day-of-year for fast comparisons
    const int startDOY = F.startDate.IsValid() ? F.startDate.GetDayOfYear() : 1;
    const int endDOY   = F.endDate.IsValid()   ? F.endDate.GetDayOfYear()   : 366;

    for (const Cyclone& C : m_cyclones)
    {
        // --------------------------------------------------------------
        // Track-level ENSO filtering
        // --------------------------------------------------------------
        if (C.enso == ENSOPeriod::EL_NINO      && !F.allowElNino)       continue;
        if (C.enso == ENSOPeriod::LA_NINA      && !F.allowLaNina)       continue;
        if (C.enso == ENSOPeriod::NEUTRAL      && !F.allowNeutral)      continue;
        if (C.enso == ENSOPeriod::NA           && !F.allowNotAvailable) continue;

        // --------------------------------------------------------------
        // Track-level basin filtering
        // --------------------------------------------------------------
        switch (C.basinId)
        {
            case EPA: if (!F.allowEPA) continue; break;
            case WPA: if (!F.allowWPA) continue; break;
            case SPA: if (!F.allowSPA) continue; break;
            case ATL: if (!F.allowATL) continue; break;
            case NIO: if (!F.allowNIO) continue; break;
            case SHE: if (!F.allowSHE) continue; break;
        }

        Cyclone filtered;
        filtered.basinId = C.basinId;
        filtered.enso    = C.enso;

        // --------------------------------------------------------------
        // Point-level filtering
        // --------------------------------------------------------------
        for (const CyclonePoint& P : C.points)
        {
            // Date range
            if (P.dayOfYear < startDOY) continue;
            if (P.dayOfYear > endDOY)   continue;

            // ----------------------------------------------------------
            // Timeline interpolation (month → visibility)
            // ----------------------------------------------------------
            if (F.enableTimeline)
            {
                if (!CycloneTimelineInterpolation(P, F, F.currentMonth))
                    continue;
            }

            // Intensity
            if (P.windspeed < F.minWind)       continue;
            if (P.pressure  > F.maxPressure)   continue;

            // Storm-type mask
            if (!(F.stateMask & (1 << static_cast<int>(P.state)))) continue;

            filtered.points.push_back(P);
        }

        if (!filtered.points.empty())
            m_cyclone_cache.push_back(filtered);
    }

    m_hasCycloneData = !m_cyclone_cache.empty();
}
*/






// ============================================================================
// Render()
// Main rendering entry point. All decisions use per‑frame render params (Model A).
// ============================================================================
bool ClimatologyOverlayFactory::Render(const ClimatologyRenderParams& p)
{
    if (!p.vp || !p.vp->bValid)
        return false;

    SetViewPort(p.vp);
	
    // const int setting = p.overlayType;
	// UI overlaytype
	const OverlayType uiSetting =
        static_cast<OverlayType>(p.overlayType);
	// factory NOAAoverlaytype -> ui 	
	const NoaaOverlayType setting = 
        ToNoaa(uiSetting);

    // Wind Atlas overlay  (UI enum)
    if (p.windAtlas.enabled && p.overlayEnabled &&
        uiSetting == OVERLAY_WIND_ATLAS)
    {
        RenderWindAtlas(p);
    }

    // Cyclones
    if (p.cyclone.showCyclones)
        RenderCyclones(p);

    // GL path
    if (!p.dc->GetDC())
    {
		if (p.overlayEnabled && p.overlayMap)
			RenderUnifiedGrid(p);

        if (p.showArrows && uiSetting == OVERLAY_WIND)
            RenderDirectionArrows(p);

        if (p.showArrows && uiSetting == OVERLAY_CURRENT)
            RenderDirectionArrows(p);

        if (p.showIsoBars)
            RenderIsoBars(p);

        if (p.showNumbers)
            RenderNumbers(p);

        return true;
    }

    // DC path
    if (p.showArrows && uiSetting == OVERLAY_WIND)
        RenderDirectionArrows(p);

    if (p.showArrows && uiSetting == OVERLAY_CURRENT)
        RenderDirectionArrows(p);

    if (uiSetting == OVERLAY_PRESSURE && p.showIsoBars)
        RenderIsoBars(p);

    if (p.showNumbers)
        RenderNumbers(p);

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

    PlugIn_ViewPort& vp = *p.vp;
    const int setting = p.overlayType;

    if (!p.overlayEnabled || !p.overlayMap)
        return;

    int month     = p.cyclone.currentMonth;
    int nextMonth = p.cyclone.nextMonth;
    double dpos   = p.cyclone.monthInterpolation;

    // Sea depth is non‑monthly
    if (setting == OVERLAY_SEA_DEPTH)
    {
        month     = 0;
        nextMonth = 0;
        dpos      = 1.0;
    }

    // Clamp interpolation
    if (p.cyclone.monthInterpolation >= 1.0)
    {
        nextMonth = month;
        dpos      = 1.0;
    }

    if (!HasDataFor(setting, month) ||
        !HasDataFor(setting, nextMonth))
        return;

    ClimatologyOverlay& O1 = m_pOverlay[month][setting];
    ClimatologyOverlay& O2 = m_pOverlay[nextMonth][setting];

    // DC path unsupported
    if (p.dc->GetDC())
    {
        wxDC* dc = p.dc->GetDC();
        dc->SetTextForeground(*wxRED);
        dc->DrawText(
            _("Climatology overlay map unsupported unless OpenGL is enabled"),
            10, 10);
        return;
    }

    // Set texture dimensions
    O1.m_width  = vp.pix_width;
    O1.m_height = vp.pix_height;
    O2.m_width  = vp.pix_width;
    O2.m_height = vp.pix_height;

    // Build + upload textures
    if (!O1.m_data)
        BuildOverlayData(O1, setting, month);
    if (!O1.m_iTexture)
        CreateGLTexture(O1);

    if (!O2.m_data)
        BuildOverlayData(O2, setting, nextMonth);
    if (!O2.m_iTexture)
        CreateGLTexture(O2);

    // Blend two months
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    double alpha = p.transparency / 100.0;

    DrawGLTexture(O1.m_iTexture,
                  O2.m_iTexture,
                  dpos,
                  vp,
                  alpha);

    glDisable(GL_BLEND);
}


// ============================================================================
// RenderUnifiedGrid()
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
//    const int setting   = p.overlayType;
//  Convert from UI overlaytype to factory NOAA overlaytype 
	const NoaaOverlayType setting =
    ToNoaa(static_cast<OverlayType>(p.overlayType));


    // ------------------------------------------------------------------------
    // 1. Gather overlays for this mode/type/month
    // ------------------------------------------------------------------------
    auto overlays = GatherOverlaysForMode(p.mode, setting, p.cyclone.currentMonth);
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
    // 4. Render via GL or DC
    // ------------------------------------------------------------------------
    if (!p.dc->GetDC())
        RenderGridGL(grid, p);
    else
        RenderGridDC(grid, *p.dc, p);
}


// ============================================================================
// GatherOverlaysForMode()
// Select overlays for the given DisplayMode, overlayType, and month.
// ============================================================================
std::vector<const ClimatologyOverlay*>
ClimatologyOverlayFactory::GatherOverlaysForMode(DisplayMode mode,
                                                 int overlayType,
                                                 int month_index)
{
    std::vector<const ClimatologyOverlay*> result;
	
	// Convert UI overlayType → NOAA overlayType
    NoaaOverlayType nt = ToNoaa(static_cast<OverlayType>(overlayType));

    // ------------------------------------------------------------------------
    // Safety: month range
    // ------------------------------------------------------------------------
    if (month_index < 0 || month_index >= 12)
        return result;

    // ------------------------------------------------------------------------
    // Single overlay mode
    // ------------------------------------------------------------------------
    if (mode == DisplayMode::SingleScaled)
    {
        const ClimatologyOverlay* O = &m_pOverlay[month_index][nt];
        if (O && O->m_valid)
            result.push_back(O);
        return result;
    }

    // ------------------------------------------------------------------------
    // Multi‑overlay modes (Weighted, Mean, Median, Max, Min, Additive)
    // All overlays for this type and month are included.
    // ------------------------------------------------------------------------
    for (int m = 0; m < 12; m++)
    {
        const ClimatologyOverlay* O = &m_pOverlay[m][nt];
        if (!O || !O->m_valid)
            continue;

        // Only overlays matching the requested month
        if (m != month_index)
            continue;

        result.push_back(O);
    }

    return result;
}


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


// ============================================================================
// GenerateUnifiedColorGrid()
// Dispatch to the correct unified‑grid operator based on DisplayMode.
// ============================================================================
void ClimatologyOverlayFactory::GenerateUnifiedColorGrid(
        ClimatologyColorGrid& outGrid,
        const std::vector<const ClimatologyOverlay*>& overlays,
        DisplayMode mode,
        const std::vector<double>& weights,
        const std::vector<double>& vmins,
        const std::vector<double>& vmaxs,
        double global_vmin,
        double global_vmax)
{
    if (overlays.empty())
        return;

    switch (mode)
    {
    case DisplayMode::SingleScaled:
    {
        const auto* O = overlays[0];
        double vmin = vmins.empty() ? 0.0 : vmins[0];
        double vmax = vmaxs.empty() ? 1.0 : vmaxs[0];

        GenerateUnifiedColorGridFromSingleOverlayScaled(
            O, vmin, vmax, outGrid, global_vmin, global_vmax);
        return;
    }

    case DisplayMode::WeightedBlend:
    {
        GenerateUnifiedColorGridMultiRangeWeightedGlobalScaled(
            overlays, weights, vmins, vmaxs,
            outGrid, global_vmin, global_vmax);
        return;
    }

    case DisplayMode::Additive:
    {
        GenerateUnifiedColorGridMultiOverlayAdditive(
            overlays, outGrid, global_vmin, global_vmax);
        return;
    }

    case DisplayMode::MaxComposite:
    {
        GenerateUnifiedColorGridMultiOverlayMax(
            overlays, outGrid, global_vmin, global_vmax);
        return;
    }

    case DisplayMode::MinComposite:
    {
        GenerateUnifiedColorGridMultiOverlayMin(
            overlays, outGrid, global_vmin, global_vmax);
        return;
    }

    case DisplayMode::MeanComposite:
    {
        GenerateUnifiedColorGridMultiOverlayMean(
            overlays, outGrid, global_vmin, global_vmax);
        return;
    }

    case DisplayMode::MedianComposite:
    {
        GenerateUnifiedColorGridMultiOverlayMedian(
            overlays, outGrid, global_vmin, global_vmax);
        return;
    }

    default:
    {
        // Fallback: single overlay
        const auto* O = overlays[0];
        double vmin = vmins.empty() ? 0.0 : vmins[0];
        double vmax = vmaxs.empty() ? 1.0 : vmaxs[0];

        GenerateUnifiedColorGridFromSingleOverlayScaled(
            O, vmin, vmax, outGrid, global_vmin, global_vmax);
        return;
    }
    }
}



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

    double alpha = p.transparency / 100.0;

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

            dc.SetPen(wxPen(c));
            dc.SetBrush(wxBrush(c));
            dc.DrawRectangle(pt.x, pt.y, cellW, cellH);
        }
    }
}

/* Later version - See older version above

// ============================================================================
// BuildOverlayData()
// Build RGBA pixel buffer for scalar overlay texture.
// Model A: values come from unified NOAA grid via getValueMonth().
// ============================================================================
bool ClimatologyOverlayFactory::BuildOverlayData(ClimatologyOverlay& O,
                                                 int setting,
                                                 int month)
{
    if (O.m_data)
        return true;

    const int w = O.m_width;
    const int h = O.m_height;

    O.m_data = new unsigned char[w * h * 4];

    Coord coord = (setting == OVERLAY_WIND || setting == OVERLAY_CURRENT)
                  ? MAG
                  : SCALAR;

    for (int y = 0; y < h; y++)
    {
        double merc = 1.0 - 2.0 * (double)y / (double)(h - 1);
        double lat  = rad2deg(atan(sinh(merc * M_PI)));

        for (int x = 0; x < w; x++)
        {
            double lon = 360.0 * (double)x / (double)(w - 1);

            double v = getValueMonth(coord, setting, lat, lon, month);

            int idx = 4 * (y * w + x);

            if (std::isnan(v))
            {
                O.m_data[idx + 0] = 0;
                O.m_data[idx + 1] = 0;
                O.m_data[idx + 2] = 0;
                O.m_data[idx + 3] = 0;
            }
            else
            {
                unsigned char r, g, b;
                ColorMap(setting, v, r, g, b);

                O.m_data[idx + 0] = r;
                O.m_data[idx + 1] = g;
                O.m_data[idx + 2] = b;
                O.m_data[idx + 3] = 255;
            }
        }
    }

    return true;
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
                                              double transparency)
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

    float colorv[4] = {0, 0, 0, -(float)transparency};
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
        wxColour color      = p.color;
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
							color, width)
							
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

    drawForSetting(OVERLAY_WIND);
    drawForSetting(OVERLAY_CURRENT);
}
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
							color, width)
							
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
    wxDC* dc = p.dc->GetDC();
    if (!dc)
        return;

    PlugIn_ViewPort& vp = *p.vp;

    auto drawForSetting = [&](int setting)
    {
        if (!p.showArrows || p.overlayType != setting)
            return;

        double   size       = p.arrowSize;
        int      width      = p.arrowWidth;
        wxColour color      = p.color;
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

                DrawArrowDC(dc,
                            p1.x + x, p1.y + y,
                            p1.x - x, p1.y - y,
                            color, width);

                if (!lengthMode)
                    DrawBarbsDC(dc, p1.x, p1.y, x, y, mag, cstep, color, width);

                DrawArrowDC(dc,
                            p1.x - x, p1.y - y,
                            p1.x - x/3 + y*2/3,
                            p1.y - y/3 - x*2/3,
                            color, width);

                DrawArrowDC(dc,
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
    wxDC* dc = p.dc->GetDC();
    if (!dc)
        return;

    PlugIn_ViewPort& vp = *p.vp;

    double spacing = p.numberSize;
    wxColour color = p.color;

    dc->SetTextForeground(color);

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
            dc->DrawText(txt, px, py);
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

    PlugIn_ViewPort& vp = *p.vp;

    const int setting  = p.overlayType;
    const double spacing = p.numberSize;

    Coord coord = (setting == OVERLAY_WIND || setting == OVERLAY_CURRENT)
                  ? MAG
                  : SCALAR;

    wxColour color = p.color;

    // GL numeric rendering uses wxDC text overlay
    if (p.dc && p.dc->dc)
    {
        wxDC* dc = p.dc->dc;
        dc->SetTextForeground(color);

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

                dc->DrawText(wxString::FromUTF8(buf), r.x, r.y);
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

    const int setting = p.overlayType;

    double spacing = p.isoBarSpacing;
    double step    = p.isoBarStep;
    int    units   = p.units;
    int    month   = p.cyclone.currentMonth;

    // Retrieve or build IsoBarMap for this overlay/month.
    ClimatologyIsoBarMap* iso =
        GetOrCreateIsoBarMap(setting, month, spacing, step, units, p);

    if (!iso)
        return;

    iso->Plot(p.dc, *p.vp);
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
    ClimatologyIsoBarMap*& iso = m_pIsobars[overlayType][month];

    // Reuse existing IsoBarMap if settings match.
    if (iso && iso->SameSettings(spacing, step, units, month, 15))
        return iso;

    // Otherwise destroy and rebuild.
    if (iso)
    {
        delete iso;
        iso = nullptr;
    }

    wxString name = wxString::Format("Overlay %d", overlayType);

    iso = new ClimatologyIsoBarMap(name,
                                   spacing,
                                   step,
                                   *this,
                                   overlayType,
                                   units,
                                   month,
                                   15,
                                   p);

    // Build contour lines.
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
    ClimatologyIsoBarMap*& iso = m_pIsobars[overlayType][month];
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
    double opacity = p.windAtlas.opacity;

    wxColour color = p.color;

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
                           (unsigned char)(opacity * 255.0));
                glLineWidth(1.0f);

                glBegin(GL_LINES);
                glVertex2f((GLfloat)(p1.x + x), (GLfloat)(p1.y + y));
                glVertex2f((GLfloat)(p1.x - x), (GLfloat)(p1.y - y));
                glEnd();
            }
            else if (p.dc->GetDC())
            {
                wxDC* dc = p.dc->GetDC();
                dc->SetPen(wxPen(color, 1));
                dc->DrawLine(p1.x + x, p1.y + y,
                             p1.x - x, p1.y - y);
            }
        }
    }
}

// ============================================================================
// RenderCyclones()
// Draw cyclone tracks using GL or wxDC.
// ============================================================================
void ClimatologyOverlayFactory::RenderCyclones(const ClimatologyRenderParams& p)
{
    if (!m_hasCycloneData)
        return;
    if (!p.cyclone.showCyclones)
        return;
    if (!p.vp || !p.vp->bValid)
        return;

    const PlugIn_ViewPort& vp = *p.vp;

    // Use filtered cyclone cache
    const auto& tracks = m_cyclone_cache;
    if (tracks.empty())
        return;

    const CycloneParams& A = m_displayParams.cyclone;

    // -----------------------------------------------------------------------
    // GL path
    // -----------------------------------------------------------------------
    if (p.useGL && p.glContextValid)
    {
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glLineWidth(2.0f);

        for (const Cyclone& C : tracks)
        {
            for (size_t i = 1; i < C.points.size(); i++)
            {
                const CyclonePoint& a = C.points[i - 1];
                const CyclonePoint& b = C.points[i];

                // ============================================================
                // CHOOSE ONE OF THESE THREE COLOR METHODS:
                // ============================================================

                // 1. Per‑point color (simple, stable)
                wxColour color = CyclonePointColor(a, A);

                // 2. Per‑segment blended color (smooth transitions)
                // wxColour color = CycloneSegmentColor(a, b, A);

                // 3. Full track intensity gradient (progressive)
                // double t = double(i) / double(C.points.size() - 1);
                // wxColour color = CycloneGradientColor(C, t, A);
				
				// 4. line thickness based on intensity
				// float width = CycloneTrackWidth(a, A);
				//  glLineWidth(width);


                // ============================================================

                wxPoint pa = LatLonToPixel(&vp, a.lat, a.lon);
                wxPoint pb = LatLonToPixel(&vp, b.lat, b.lon);

                glColor4ub(color.Red(), color.Green(), color.Blue(),
                           (unsigned char)(A.opacity * 255.0));

                glBegin(GL_LINES);
                glVertex2f((GLfloat)pa.x, (GLfloat)pa.y);
                glVertex2f((GLfloat)pb.x, (GLfloat)pb.y);
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
        wxDC* dc = p.dc->GetDC();
        if (!dc)
            return;

        for (const Cyclone& C : tracks)
        {
            for (size_t i = 1; i < C.points.size(); i++)
            {
                const CyclonePoint& a = C.points[i - 1];
                const CyclonePoint& b = C.points[i];

                // ============================================================
                // Same three options for DC:
                // ============================================================

                // 1. Per‑point color
                wxColour color = CyclonePointColor(a, A);

                // 2. Per‑segment blended color
                // wxColour color = CycloneSegmentColor(a, b, A);

                // 3. Full‑track gradient
                // double t = double(i) / double(C.points.size() - 1);
                // wxColour color = CycloneGradientColor(C, t, A);
				
				// 4. line thickness based on intensity
				// float width = CycloneTrackWidth(a, A);
				//  dc->SetPen(wxPen(color, width));
				
				// 5. DC‑based legend for basin + ENSO + intensity + thickness 
				// if (p.cyclone.showCyclones && p.dc && p.dcContextValid)
				// {
				// 		wxPoint legendOrigin(20, 20);   // top-left corner
				// 		CycloneLegend(p.dc->GetDC(), m_displayParams.cyclone, legendOrigin);
				//	}


                // ============================================================

                wxPoint pa = LatLonToPixel(&vp, a.lat, a.lon);
                wxPoint pb = LatLonToPixel(&vp, b.lat, b.lon);

                dc->SetPen(wxPen(color, 2));
                dc->DrawLine(pa.x, pa.y, pb.x, pb.y);
            }
        }
    }
}

// ============================================================================
// BasinColor()
// Helper for Rendercyclones
// ============================================================================

wxColour ClimatologyOverlayFactory::BasinColor(int basinId)
{
    switch (basinId)
    {
        case EPA: return wxColour(255, 128, 128);
        case WPA: return wxColour(255, 200, 0);
        case SPA: return wxColour(255, 0, 255);
        case ATL: return wxColour(0, 128, 255);
        case NIO: return wxColour(0, 200, 128);
        case SHE: return wxColour(128, 128, 255);
        default:  return wxColour(255, 255, 255);
    }
}


// ============================================================================
// CycloneColor()
// Helper (Track‑level color resolver)
// ============================================================================
wxColour ClimatologyOverlayFactory::CycloneColor(const Cyclone& C,
                                                 const CycloneParams& A) const
{
    // -----------------------------------------------------------------------
    // 1. Color mode selector
    // -----------------------------------------------------------------------
    switch (A.colorMode)
    {
        // ================================================================
        // Mode 0: Basin-based coloring (default)
        // ================================================================
        case 0:
            return BasinColor(C.basinId);

        // ================================================================
        // Mode 1: ENSO-based coloring
        // ================================================================
        case 1:
            switch (C.enso)
            {
                case ENSOPeriod::EL_NINO:  return wxColour(255, 80, 80);   // red
                case ENSOPeriod::LA_NINA:  return wxColour(80, 80, 255);   // blue
                case ENSOPeriod::NEUTRAL:  return wxColour(120, 200, 120); // green
                case ENSOPeriod::NA:       return wxColour(180, 180, 180); // gray
            }
            break;

        // ================================================================
        // Mode 2: Intensity-based coloring (max wind)
        // ================================================================
        case 2:
        {
            // Compute track max wind
            double w = 0.0;
            for (const CyclonePoint& P : C.points)
                w = std::max(w, P.maxWind);

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
        // Mode 3: Storm-type coloring (CycloneState)
        // ================================================================
        case 3:
        {
            // Use first point's state as representative
            if (C.points.empty())
                return wxColour(200, 200, 200);

            CycloneState s = C.points.front().state;

            switch (s)
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
    }

    // -----------------------------------------------------------------------
    // Fallback neutral color
    // -----------------------------------------------------------------------
    return wxColour(255, 255, 255);
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
            switch (pt.enso)
            {
                case CycloneENSO::ElNino:  return wxColour(255, 80, 80);   // red
                case CycloneENSO::LaNina:  return wxColour(80, 80, 255);   // blue
                case CycloneENSO::Neutral: return wxColour(120, 200, 120); // green
                default:                   return wxColour(180, 180, 180); // gray
            }

        // ================================================================
        // Mode 2: Intensity-based coloring (point-level)
        // ================================================================
        case 2:
        {
            double w = pt.maxWind;   // unified wind field

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
    // Resolve per-point colors using your unified point-level resolver
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


// ====================================================
// CycloneGradientColor()
//  Intensity‑weighted gradient across entire track
//  So all color modes (basin, ENSO, intensity, stormtype) work automatically.
// ======================================================

wxColour ClimatologyOverlayFactory::CycloneGradientColor(const Cyclone& C,
                                                         double t,
                                                         const CycloneParams& A) const
{
    // Clamp t to [0,1]
    if (t < 0.0) t = 0.0;
    if (t > 1.0) t = 1.0;

    // -----------------------------------------------------------------------
    // Compute min/max intensity across the entire track
    // -----------------------------------------------------------------------
    double minW = 1e9;
    double maxW = -1e9;

    for (const CyclonePoint& P : C.points)
    {
        minW = std::min(minW, P.maxWind);
        maxW = std::max(maxW, P.maxWind);
    }

    if (minW > maxW)   // degenerate track
        return wxColour(200, 200, 200);

    // -----------------------------------------------------------------------
    // Interpolate intensity at position t along the track
    // -----------------------------------------------------------------------
    double w = minW + (maxW - minW) * t;

    // -----------------------------------------------------------------------
    // Map intensity → color (same scale as CyclonePointColor)
    // -----------------------------------------------------------------------
    if (w < 20)  return wxColour(180, 180, 180); // disturbance
    if (w < 34)  return wxColour(120, 200, 120); // depression
    if (w < 64)  return wxColour(255, 200, 80);  // tropical storm
    if (w < 83)  return wxColour(255, 160, 80);  // Cat 1
    if (w < 96)  return wxColour(255, 120, 80);  // Cat 2
    if (w < 113) return wxColour(255, 80, 80);   // Cat 3
    if (w < 137) return wxColour(200, 40, 40);   // Cat 4

    return wxColour(160, 0, 0);                  // Cat 5+
}

// ============================================================================
// CycloneTrackWidth()
// ine thickness based on intensity
// ============================================================================

float ClimatologyOverlayFactory::CycloneTrackWidth(const CyclonePoint& pt,
                                                   const CycloneParams& A) const
{
    const double w = pt.maxWind;   // unified intensity field

    // Base thickness (user‑configurable)
    const float base = A.baseThickness;      // e.g., 1.5f
    const float scale = A.intensityScale;    // e.g., 0.05f

    // Saffir–Simpson categories → thickness multiplier
    if (w < 20)   return base * 0.8f;   // disturbance
    if (w < 34)   return base * 1.0f;   // depression
    if (w < 64)   return base * 1.2f;   // tropical storm
    if (w < 83)   return base * 1.4f;   // Cat 1
    if (w < 96)   return base * 1.6f;   // Cat 2
    if (w < 113)  return base * 1.8f;   // Cat 3
    if (w < 137)  return base * 2.0f;   // Cat 4

    return base * 2.3f;                // Cat 5+
}


// ============================================================================
// CycloneLegend()
// DC‑based legend for basin + ENSO + intensity + thickness
// ============================================================================


void ClimatologyOverlayFactory::CycloneLegend(wxDC* dc,
                                              const CycloneParams& A,
                                              const wxPoint& origin)
{
    if (!dc)
        return;

    int x = origin.x;
    int y = origin.y;

    const int swatch = 22;     // color box size
    const int spacing = 4;     // vertical spacing
    const int textOffset = 28; // text start

    dc->SetTextForeground(*wxWHITE);

    // -----------------------------------------------------------------------
    // BASIN COLORS
    // -----------------------------------------------------------------------
    dc->DrawText("Basins:", x, y);
    y += 18;

    struct BasinEntry { const char* name; int id; };
    BasinEntry basins[] = {
        {"East Pacific", EPA},
        {"West Pacific", WPA},
        {"South Pacific", SPA},
        {"Atlantic",     ATL},
        {"North Indian", NIO},
        {"South Indian", SHE}
    };

    for (auto& B : basins)
    {
        wxColour c = BasinColor(B.id);
        dc->SetBrush(wxBrush(c));
        dc->SetPen(*wxTRANSPARENT_PEN);
        dc->DrawRectangle(x, y, swatch, swatch);

        dc->DrawText(B.name, x + textOffset, y + 2);
        y += swatch + spacing;
    }

    y += 10;

    // -----------------------------------------------------------------------
    // ENSO COLORS
    // -----------------------------------------------------------------------
    dc->DrawText("ENSO:", x, y);
    y += 18;

    struct ENSOEntry { const char* name; CycloneENSO id; };
    ENSOEntry enso[] = {
        {"El Niño",   CycloneENSO::ElNino},
        {"La Niña",   CycloneENSO::LaNina},
        {"Neutral",   CycloneENSO::Neutral},
        {"N/A",       CycloneENSO::NA}
    };

    for (auto& E : enso)
    {
        wxColour c;
        switch (E.id)
        {
            case CycloneENSO::ElNino:  c = wxColour(255, 80, 80); break;
            case CycloneENSO::LaNina:  c = wxColour(80, 80, 255); break;
            case CycloneENSO::Neutral: c = wxColour(120, 200, 120); break;
            default:                   c = wxColour(180, 180, 180); break;
        }

        dc->SetBrush(wxBrush(c));
        dc->SetPen(*wxTRANSPARENT_PEN);
        dc->DrawRectangle(x, y, swatch, swatch);

        dc->DrawText(E.name, x + textOffset, y + 2);
        y += swatch + spacing;
    }

    y += 10;

    // -----------------------------------------------------------------------
    // INTENSITY SCALE (Saffir–Simpson)
    // -----------------------------------------------------------------------
    dc->DrawText("Intensity:", x, y);
    y += 18;

    struct IntensityEntry { const char* name; double wind; };
    IntensityEntry intens[] = {
        {"Disturbance",     15},
        {"Depression",      30},
        {"Tropical Storm",  50},
        {"Category 1",      75},
        {"Category 2",      90},
        {"Category 3",     105},
        {"Category 4",     125},
        {"Category 5+",    150}
    };

    for (auto& I : intens)
    {
        CyclonePoint dummy;
        dummy.maxWind = I.wind;

        wxColour c = CyclonePointColor(dummy, A);

        dc->SetBrush(wxBrush(c));
        dc->SetPen(*wxTRANSPARENT_PEN);
        dc->DrawRectangle(x, y, swatch, swatch);

        dc->DrawText(I.name, x + textOffset, y + 2);
        y += swatch + spacing;
    }

    y += 10;

    // -----------------------------------------------------------------------
    // THICKNESS SCALE
    // -----------------------------------------------------------------------
    dc->DrawText("Line Thickness:", x, y);
    y += 18;

    double winds[] = {20, 40, 60, 80, 100, 120, 140};

    for (double w : winds)
    {
        CyclonePoint dummy;
        dummy.maxWind = w;

        float width = CycloneTrackWidth(dummy, A);
        wxColour c = CyclonePointColor(dummy, A);

        dc->SetPen(wxPen(c, width));
        dc->DrawLine(x, y + swatch/2, x + 60, y + swatch/2);

        wxString label;
        label.Printf("%d kt", (int)w);
        dc->DrawText(label, x + 70, y + swatch/2 - 8);

        y += swatch + spacing;
    }
}

// ========================================================
// CycloneTimelineInterpolation
//  month → day‑of‑year → visibility window
//  Converts month → day‑of‑year  using NOAA climatology midpoints:
// ========================================================

bool ClimatologyOverlayFactory::CycloneTimelineInterpolation(const CyclonePoint& pt,
                                                             const CycloneFilterParams& F,
                                                             int currentMonth) const
{
    // Convert month → center day-of-year
    // Uses NOAA climatology midpoints for each month
    static const int monthCenter[12] =
    {
        15,   // Jan
        46,   // Feb
        75,   // Mar
        105,  // Apr
        135,  // May
        166,  // Jun
        196,  // Jul
        227,  // Aug
        258,  // Sep
        288,  // Oct
        319,  // Nov
        349   // Dec
    };

    int centerDay = monthCenter[currentMonth - 1];

    // Cyclone point day-of-year
    int d = pt.dayOfYear;

    // Compute absolute difference with wrap-around
    int diff = abs(d - centerDay);
    if (diff > 183)        // wrap around year boundary
        diff = 365 - diff;

    // Visibility window = ± daySpan/2
    return diff <= (F.daySpan / 2);
}


// ============================================================================
// CycloneVisible()
// Determine if a cyclone track should be drawn based on filter parameters.
// ============================================================================
bool ClimatologyOverlayFactory::CycloneVisible(const CycloneTrack& track,
                                               const ClimatologyRenderParams& p) const
{
    // Filter by basin
    if (!p.cycloneParams.basinFilter.empty())
    {
        if (p.cycloneParams.basinFilter.find(track.basin) ==
            p.cycloneParams.basinFilter.end())
        {
            return false;
        }
    }

    // Filter by category
    if (!p.cycloneParams.categoryFilter.empty())
    {
        bool anyMatch = false;
        for (const CyclonePoint& pt : track.points)
        {
            if (p.cycloneParams.categoryFilter.count(pt.category))
            {
                anyMatch = true;
                break;
            }
        }
        if (!anyMatch)
            return false;
    }

    // Filter by month
    if (!p.cycloneParams.monthFilter.empty())
    {
        bool anyMatch = false;
        for (const CyclonePoint& pt : track.points)
        {
            if (p.cycloneParams.monthFilter.count(pt.month))
            {
                anyMatch = true;
                break;
            }
        }
        if (!anyMatch)
            return false;
    }

    // Filter by year range
    if (p.cycloneParams.yearMin > 0 || p.cycloneParams.yearMax > 0)
    {
        bool anyMatch = false;
        for (const CyclonePoint& pt : track.points)
        {
            if ((p.cycloneParams.yearMin <= 0 ||
                 pt.year >= p.cycloneParams.yearMin) &&
                (p.cycloneParams.yearMax <= 0 ||
                 pt.year <= p.cycloneParams.yearMax))
            {
                anyMatch = true;
                break;
            }
        }
        if (!anyMatch)
            return false;
    }

    return true;
}


// ============================================================================
// CycloneColor()
// Determine cyclone track color based on category or user override.
// ============================================================================
wxColour ClimatologyOverlayFactory::CycloneColor(const CycloneTrack& track,
                                                 const ClimatologyRenderParams& p) const
{
    // User override color
    if (p.cycloneParams.useFixedColor)
        return p.cycloneParams.fixedColor;

    // Category-based color
    int maxCat = 0;
    for (const CyclonePoint& pt : track.points)
        maxCat = std::max(maxCat, pt.category);

    switch (maxCat)
    {
        case 1: return wxColour(0,   200, 0);     // TS / Cat 1
        case 2: return wxColour(255, 255, 0);     // Cat 2
        case 3: return wxColour(255, 165, 0);     // Cat 3
        case 4: return wxColour(255, 100, 0);     // Cat 4
        case 5: return wxColour(255, 0,   0);     // Cat 5
        default: return wxColour(180, 180, 180);  // Unknown / TD
    }
}

// ============================================================================
// LoadENSODataFromCSV()
// Load ENSO index values from a CSV file.
// ============================================================================
bool ClimatologyOverlayFactory::LoadENSODataFromCSV(const wxString& filename)
{
    wxFileName fn(m_dataDir, filename);
    if (!fn.FileExists())
        return false;

    wxTextFile tf(fn.GetFullPath());
    if (!tf.Open())
        return false;

    m_enso.clear();

    for (wxString line = tf.GetFirstLine();
         !tf.Eof();
         line = tf.GetNextLine())
    {
        if (line.IsEmpty())
            continue;

        wxArrayString parts = wxStringTokenize(line, ",");
        if (parts.size() < 3)
            continue;

        ENSORecord rec;
        rec.year  = wxAtoi(parts[0]);
        rec.month = wxAtoi(parts[1]);
        rec.value = wxAtof(parts[2]);

        m_enso.push_back(rec);
    }

    return !m_enso.empty();
}

/*    See earlier version above

// ============================================================================
// getCurValue()
// Unified accessor for scalar or vector values using NOAA grid model.
// ============================================================================
double ClimatologyOverlayFactory::getCurValue(Coord c,
                                              int setting,
                                              double lat,
                                              double lon,
                                              const ClimatologyRenderParams& p)
{
    // Determine month interpolation
    int m1 = p.cyclone.currentMonth;
    int m2 = p.cyclone.nextMonth;
    double dpos = p.cyclone.monthInterpolation;

    // Sea depth is non-monthly
    if (setting == OVERLAY_SEA_DEPTH)
    {
        m1 = 0;
        m2 = 0;
        dpos = 1.0;
    }

    // Clamp interpolation
    if (dpos >= 1.0)
    {
        m2 = m1;
        dpos = 1.0;
    }

    // Retrieve monthly values
    double v1 = getValueMonth(c, setting, lat, lon, m1);
    double v2 = getValueMonth(c, setting, lat, lon, m2);

    if (std::isnan(v1) || std::isnan(v2))
        return NAN;

    // Linear interpolation
    return v1 * (1.0 - dpos) + v2 * dpos;
}

// ============================================================================
// getValueMonth()
// Retrieve a scalar or vector magnitude from the unified NOAA grid.
// ============================================================================
double ClimatologyOverlayFactory::getValueMonth(Coord c,
                                                int setting,
                                                double lat,
                                                double lon,
                                                int month)
{
    // Validate month
    if (month < 0 || month >= 12)
        return NAN;

    // Retrieve grid for this setting/month
    ClimatologyOverlay& O = m_pOverlay[month][setting];

    if (!O.m_valid)
        return NAN;

    // Convert lat/lon to grid indices
    double gx = (lon - O.lon_min) / O.lon_step;
    double gy = (lat - O.lat_min) / O.lat_step;

    int ix = (int)floor(gx);
    int iy = (int)floor(gy);

    if (ix < 0 || iy < 0 ||
        ix + 1 >= O.lon_count ||
        iy + 1 >= O.lat_count)
    {
        return NAN;
    }

    // Bilinear interpolation
    double dx = gx - ix;
    double dy = gy - iy;

    double v00 = O.value(ix,     iy);
    double v10 = O.value(ix + 1, iy);
    double v01 = O.value(ix,     iy + 1);
    double v11 = O.value(ix + 1, iy + 1);

    if (std::isnan(v00) || std::isnan(v10) ||
        std::isnan(v01) || std::isnan(v11))
    {
        return NAN;
    }

    double v0 = v00 * (1.0 - dx) + v10 * dx;
    double v1 = v01 * (1.0 - dx) + v11 * dx;

    double v = v0 * (1.0 - dy) + v1 * dy;

    // Vector magnitude mode
    if (c == MAG && (setting == OVERLAY_WIND || setting == OVERLAY_CURRENT))
    {
        // Retrieve U/V components
        double u = getValueMonth(U, setting, lat, lon, month);
        double v2 = getValueMonth(V, setting, lat, lon, month);

        if (std::isnan(u) || std::isnan(v2))
            return NAN;

        return hypot(u, v2);
    }

    return v;
}

*/

// ============================================================================
// ColorMap()
// Convert a scalar value into an RGB color for the overlay.
// ============================================================================
void ClimatologyOverlayFactory::ColorMap(int setting,
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

        case OVERLAY_PRECIPITATION:
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
// GetOverlayMap()
// Retrieve overlay grid for a given setting/month.
// ============================================================================
ClimatologyOverlay&
ClimatologyOverlayFactory::GetOverlayMap(int setting, int month)
{
    // Validate month
    if (month < 0 || month >= 12)
        month = 0;

    // Validate setting
    if (setting < 0 || setting >= OVERLAY_COUNT)
        setting = 0;

    return m_pOverlay[month][setting];
}

/
// ============================================================================
// GetWindDir()
// Compute wind direction (degrees true) from U/V components.
// ============================================================================
double ClimatologyOverlayFactory::GetWindDir(const ClimatologyRenderParams& p,
                                             double lat,
                                             double lon)
{
    // Retrieve U/V components
    double u = GetOverlayValue(OVERLAY_WIND_U, p, lat, lon);
    double v = GetOverlayValue(OVERLAY_WIND_V, p, lat, lon);

    if (std::isnan(u) || std::isnan(v))
        return NAN;

    // Meteorological convention:
    // Direction wind is *coming from*, measured clockwise from north.
    //
    // atan2 returns angle of vector (u east, v north),
    // but meteorology uses opposite sign and 0° = north.
    double dir = atan2(-u, -v) * 180.0 / M_PI;

    if (dir < 0)
        dir += 360.0;

    return dir;
}

// ============================================================================
// GetWindSpeed()
// Compute wind speed magnitude from U/V components.
// ============================================================================
double ClimatologyOverlayFactory::GetWindSpeed(const ClimatologyRenderParams& p,
                                               double lat,
                                               double lon)
{
    // Retrieve U/V components
    double u = GetOverlayValue(OVERLAY_WIND_U, p, lat, lon);
    double v = GetOverlayValue(OVERLAY_WIND_V, p, lat, lon);

    if (std::isnan(u) || std::isnan(v))
        return NAN;

    // Magnitude (m/s)
    return hypot(u, v);
}

// ============================================================================
// GetCurrentDir()
// Compute ocean current direction (degrees true) from U/V components.
// ============================================================================
double ClimatologyOverlayFactory::GetCurrentDir(const ClimatologyRenderParams& p,
                                                double lat,
                                                double lon)
{
    // Retrieve U/V components
    double u = GetOverlayValue(OVERLAY_CURRENT_U, p, lat, lon);
    double v = GetOverlayValue(OVERLAY_CURRENT_V, p, lat, lon);

    if (std::isnan(u) || std::isnan(v))
        return NAN;

    // Oceanographic convention:
    // Direction current is *flowing toward*, measured clockwise from north.
    //
    // atan2(v, u) gives direction of vector (u east, v north),
    // convert to degrees true.
    double dir = atan2(u, v) * 180.0 / M_PI;

    if (dir < 0)
        dir += 360.0;

    return dir;
}

// ============================================================================
// GetCurrentSpeed()
// Compute ocean current speed magnitude from U/V components.
// ============================================================================
double ClimatologyOverlayFactory::GetCurrentSpeed(const ClimatologyRenderParams& p,
                                                  double lat,
                                                  double lon)
{
    // Retrieve U/V components
    double u = GetOverlayValue(OVERLAY_CURRENT_U, p, lat, lon);
    double v = GetOverlayValue(OVERLAY_CURRENT_V, p, lat, lon);

    if (std::isnan(u) || std::isnan(v))
        return NAN;

    // Magnitude (m/s)
    return hypot(u, v);
}

// ============================================================================
// GetPressure()
// Retrieve sea-level pressure (hPa) from overlay grid.
// ============================================================================
double ClimatologyOverlayFactory::GetPressure(const ClimatologyRenderParams& p,
                                              double lat,
                                              double lon)
{
    // Pressure is stored in OVERLAY_PRESSURE
    double p = GetOverlayValue(OVERLAY_PRESSURE, p, lat, lon);

    if (std::isnan(p))
        return NAN;

    // NOAA dataset stores pressure in Pa; convert to hPa
    return p * 0.01;
}

// ============================================================================
// GetAirTemp()
// Retrieve air temperature (°C) from overlay grid.
// ============================================================================
double ClimatologyOverlayFactory::GetAirTemp(const ClimatologyRenderParams& p,
                                             double lat,
                                             double lon)
{
    // Air temperature stored in OVERLAY_AIR_TEMP
    double t = GetOverlayValue(OVERLAY_AIR_TEMP, p, lat, lon);

    if (std::isnan(t))
        return NAN;

    // NOAA dataset stores temperature in Kelvin; convert to Celsius
    return t - 273.15;
}

// ============================================================================
// GetSeaTemp()
// Retrieve sea-surface temperature (°C) from overlay grid.
// ============================================================================
double ClimatologyOverlayFactory::GetSeaTemp(const ClimatologyRenderParams& p,
                                             double lat,
                                             double lon)
{
    // Sea temperature stored in OVERLAY_SEA_TEMP
    double t = GetOverlayValue(OVERLAY_SEA_TEMP, p, lat, lon);

    if (std::isnan(t))
        return NAN;

    // NOAA dataset stores SST in Kelvin; convert to Celsius
    return t - 273.15;
}


// ============================================================================
// GetCloudCover()
// Retrieve cloud cover (%) from overlay grid.
// ============================================================================
double ClimatologyOverlayFactory::GetCloudCover(const ClimatologyRenderParams& p,
                                                double lat,
                                                double lon)
{
    // Cloud cover stored in OVERLAY_CLOUD
    double c = GetOverlayValue(OVERLAY_CLOUD, p, lat, lon);

    if (std::isnan(c))
        return NAN;

    // NOAA dataset stores cloud fraction 0–1; convert to percent
    return c * 100.0;
}


// ============================================================================
// GetPrecipitation()
// Retrieve precipitation (mm/month) from overlay grid.
// ============================================================================
double ClimatologyOverlayFactory::GetPrecipitation(const ClimatologyRenderParams& p,
                                                   double lat,
                                                   double lon)
{
    // Precipitation stored in OVERLAY_PRECIPITATION
    double p = GetOverlayValue(OVERLAY_PRECIPITATION, p, lat, lon);

    if (std::isnan(p))
        return NAN;

    // NOAA dataset stores precipitation in kg/m^2/s (equivalent to mm/s)
    // Convert to mm/month
    //
    // Seconds per month (approx): 30 days
    const double secondsPerMonth = 30.0 * 24.0 * 3600.0;

    return p * secondsPerMonth;
}

// ============================================================================
// GetSeaDepth()
// Retrieve bathymetry (meters) from overlay grid.
// ============================================================================
double ClimatologyOverlayFactory::GetSeaDepth(double lat, double lon)
{
    // Sea depth is stored in OVERLAY_SEA_DEPTH, month index always 0
    ClimatologyOverlay& O = m_pOverlay[0][OVERLAY_SEA_DEPTH];

    if (!O.m_valid)
        return NAN;

    // Convert lat/lon to grid indices
    double gx = (lon - O.lon_min) / O.lon_step;
    double gy = (lat - O.lat_min) / O.lat_step;

    int ix = (int)floor(gx);
    int iy = (int)floor(gy);

    if (ix < 0 || iy < 0 ||
        ix + 1 >= O.lon_count ||
        iy + 1 >= O.lat_count)
    {
        return NAN;
    }

    // Bilinear interpolation
    double dx = gx - ix;
    double dy = gy - iy;

    double v00 = O.value(ix,     iy);
    double v10 = O.value(ix + 1, iy);
    double v01 = O.value(ix,     iy + 1);
    double v11 = O.value(ix + 1, iy + 1);

    if (std::isnan(v00) || std::isnan(v10) ||
        std::isnan(v01) || std::isnan(v11))
    {
        return NAN;
    }

    double v0 = v00 * (1.0 - dx) + v10 * dx;
    double v1 = v01 * (1.0 - dx) + v11 * dx;

    return v0 * (1.0 - dy) + v1 * dy;
}


// ============================================================================
// GetENSOIndex()
// Retrieve ENSO index (dimensionless) for a given year/month.
// ============================================================================
double ClimatologyOverlayFactory::GetENSOIndex(int year, int month)
{
    // Validate month
    if (month < 0 || month >= 12)
        return NAN;

    // ENSO dataset stored in m_ENSOIndex[year][month]
    auto it = m_ENSOIndex.find(year);
    if (it == m_ENSOIndex.end())
        return NAN;

    const std::vector<double>& v = it->second;
    if ((int)v.size() <= month)
        return NAN;

    double e = v[month];
    if (std::isnan(e))
        return NAN;

    return e;
}

// ============================================================================
// GetCycloneData()
// Retrieve cyclone metadata for a given year/month.
// Returns true if data exists.
// ============================================================================
bool ClimatologyOverlayFactory::GetCycloneData(int year,
                                               int month,
                                               CycloneData& out)
{
    // Validate month
    if (month < 0 || month >= 12)
        return false;

    // Cyclone dataset stored in m_Cyclones[year][month]
    auto it = m_Cyclones.find(year);
    if (it == m_Cyclones.end())
        return false;

    const std::vector<CycloneData>& v = it->second;
    if ((int)v.size() <= month)
        return false;

    const CycloneData& c = v[month];
    if (!c.valid)
        return false;

    out = c;
    return true;
}

// ============================================================================
// GetCycloneTrack()
// Retrieve full cyclone track by cyclone ID.
// Returns true if track exists.
// ============================================================================
bool ClimatologyOverlayFactory::GetCycloneTrack(int id,
                                                CycloneTrack& out)
{
    // Cyclone tracks stored in m_CycloneTracks[id]
    auto it = m_CycloneTracks.find(id);
    if (it == m_CycloneTracks.end())
        return false;

    const CycloneTrack& t = it->second;
    if (!t.valid || t.points.empty())
        return false;

    out = t;
    return true;
}

// ============================================================================
// GetCyclonePoint()
// Retrieve a single cyclone track point by cyclone ID and point index.
// Returns true if the point exists.
// ============================================================================
bool ClimatologyOverlayFactory::GetCyclonePoint(int id,
                                                int index,
                                                CyclonePoint& out)
{
    // Cyclone tracks stored in m_CycloneTracks[id]
    auto it = m_CycloneTracks.find(id);
    if (it == m_CycloneTracks.end())
        return false;

    const CycloneTrack& t = it->second;
    if (!t.valid || index < 0 || index >= (int)t.points.size())
        return false;

    out = t.points[index];
    return true;
}


// ============================================================================
// GetStormCursorData()
// Retrieve storm (cyclone) data nearest to a given lat/lon cursor position.
// Returns true if a storm point is found within search radius.
// ============================================================================
bool ClimatologyOverlayFactory::GetStormCursorData(double lat,
                                                   double lon,
                                                   StormCursorData& out)
{
    const double searchRadiusDeg = 1.0; // ~60 nm radius

    bool found = false;
    double bestDist = 1e9;

    // Iterate all cyclone tracks
    for (const auto& kv : m_CycloneTracks)
    {
        const CycloneTrack& track = kv.second;
        if (!track.valid)
            continue;

        // Iterate all points in track
        for (const CyclonePoint& p : track.points)
        {
            double dlat = lat - p.lat;
            double dlon = lon - p.lon;
            double dist = sqrt(dlat*dlat + dlon*dlon);

            if (dist < searchRadiusDeg && dist < bestDist)
            {
                bestDist = dist;
                found = true;

                out.id       = kv.first;
                out.lat      = p.lat;
                out.lon      = p.lon;
                out.wind     = p.wind;
                out.pressure = p.pressure;
                out.year     = p.year;
                out.month    = p.month;
                out.day      = p.day;
                out.hour     = p.hour;
            }
        }
    }

    return found;
}

// ============================================================================
// GetStormHistory()
// Retrieve all historical storm points for a given storm ID.
// Returns true if history exists.
// ============================================================================
bool ClimatologyOverlayFactory::GetStormHistory(int id,
                                                std::vector<CyclonePoint>& out)
{
    auto it = m_CycloneTracks.find(id);
    if (it == m_CycloneTracks.end())
        return false;

    const CycloneTrack& track = it->second;
    if (!track.valid || track.points.empty())
        return false;

    out = track.points;
    return true;
}

// ============================================================================
// GetStormHistoryPoint()
// Retrieve a single historical storm point by storm ID and index.
// Returns true if the point exists.
// ============================================================================
bool ClimatologyOverlayFactory::GetStormHistoryPoint(int id,
                                                     int index,
                                                     CyclonePoint& out)
{
    auto it = m_CycloneTracks.find(id);
    if (it == m_CycloneTracks.end())
        return false;

    const CycloneTrack& track = it->second;
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
int ClimatologyOverlayFactory::GetStormCategory(double wind_kt)
{
    if (std::isnan(wind_kt))
        return 0;

    // Saffir–Simpson scale (approximate thresholds)
    if (wind_kt >= 137) return 5;
    if (wind_kt >= 113) return 4;
    if (wind_kt >= 96)  return 3;
    if (wind_kt >= 83)  return 2;
    if (wind_kt >= 64)  return 1;

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
    case 5: return wxColour(255, 0, 0);       // Extreme
    case 4: return wxColour(255, 64, 0);      // Very strong
    case 3: return wxColour(255, 128, 0);     // Strong
    case 2: return wxColour(255, 192, 0);     // Moderate
    case 1: return wxColour(255, 255, 0);     // Weak
    default: return wxColour(160, 160, 160);  // Tropical depression / none
    }
}

// ============================================================================
// GetStormSymbol()
// Return a wxBitmap symbol for a storm category.
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
// Build a detailed tooltip string for a storm point.
// ============================================================================
wxString ClimatologyOverlayFactory::GetStormTooltip(const CyclonePoint& p)
{
    wxString s;

    s << wxString::Format("Storm ID: %d\n", p.id);
    s << wxString::Format("Date: %04d-%02d-%02d %02d:00\n",
                          p.year, p.month, p.day, p.hour);
    s << wxString::Format("Position: %.2f°, %.2f°\n", p.lat, p.lon);
    s << wxString::Format("Wind: %.0f kt\n", p.wind);
    s << wxString::Format("Pressure: %.0f hPa\n", p.pressure);

    int cat = GetStormCategory(p.wind);
    s << wxString::Format("Category: %s\n", GetStormLabel(cat));

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
// High‑level accessor for retrieving the active overlay grid for a given
// climatology type. Uses the current month from ClimatologyRenderParams.
// Returns nullptr if the overlay is invalid or out of range.
// ============================================================================


ClimatologyOverlay*
ClimatologyOverlayFactory::GetOverlay(OverlayType type,
                                      const ClimatologyRenderParams& p)
{
    int month = p.cyclone.currentMonth;

    if (month < 0 || month >= 12)
        return nullptr;

    if (type < 0 || type >= OVERLAY_COUNT)
        return nullptr;

    ClimatologyOverlay& O = m_pOverlay[month][type];
    return O.m_valid ? &O : nullptr;
}



// ============================================================================
// GetOverlay() const
// Low‑level accessor for retrieving the overlay grid for a specific month.
// Does not perform interpolation or rendering logic. Caller must validate
// month_index and overlay validity.
// ============================================================================
const ClimatologyOverlay*
ClimatologyOverlayFactory::GetOverlay(OverlayType type,
                                      const ClimatologyRenderParams& p) const
{
    int month = p.cyclone.currentMonth;

    if (month < 0 || month >= 12)
        return nullptr;

    if (type < 0 || type >= OVERLAY_COUNT)
        return nullptr;

    const ClimatologyOverlay& O = m_pOverlay[month][type];
    return O.m_valid ? &O : nullptr;
}



// ============================================================================
// GetOverlayValue()
// Retrieve overlay value for a given type/month at lat/lon.
// Performs bounds checking and delegates to bilinear interpolation.
// ============================================================================
double ClimatologyOverlayFactory::GetOverlayValue(OverlayType type,
                                                  const ClimatologyRenderParams& p,
                                                  double lat,
                                                  double lon)
{
    if (type < 0 || type >= OVERLAY_COUNT)
        return NAN;

    int month = p.cyclone.currentMonth;
    if (month < 0 || month >= 12)
        return NAN;

    ClimatologyOverlay& O = m_pOverlay[month][type];
    if (!O.m_valid)
        return NAN;

    if (lat < O.lat_min || lat > O.lat_max ||
        lon < O.lon_min || lon > O.lon_max)
    {
        return NAN;
    }

    return GetOverlayValueBilinear(O, lat, lon);
}


// ============================================================================
// GetOverlayValueBilinear()
// Bilinear interpolation of overlay grid values at lat/lon.
// ============================================================================
double ClimatologyOverlayFactory::GetOverlayValueBilinear(const ClimatologyOverlay& O,
                                                          double lat,
                                                          double lon)
{
    // Convert lat/lon to grid coordinates
    double gx = (lon - O.lon_min) / O.lon_step;
    double gy = (lat - O.lat_min) / O.lat_step;

    int ix = (int)floor(gx);
    int iy = (int)floor(gy);

    // Bounds check
    if (ix < 0 || iy < 0 ||
        ix + 1 >= O.lon_count ||
        iy + 1 >= O.lat_count)
    {
        return NAN;
    }

    double dx = gx - ix;
    double dy = gy - iy;

    // Fetch grid values
    double v00 = O.value(ix,     iy);
    double v10 = O.value(ix + 1, iy);
    double v01 = O.value(ix,     iy + 1);
    double v11 = O.value(ix + 1, iy + 1);

    if (std::isnan(v00) || std::isnan(v10) ||
        std::isnan(v01) || std::isnan(v11))
    {
        return NAN;
    }

    // Bilinear interpolation
    double v0 = v00 * (1.0 - dx) + v10 * dx;
    double v1 = v01 * (1.0 - dx) + v11 * dx;

    return v0 * (1.0 - dy) + v1 * dy;
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

    double vmin = 1e12;
    double vmax = -1e12;
    bool found = false;

    for (int iy = 0; iy < O.lat_count; iy++)
    {
        for (int ix = 0; ix < O.lon_count; ix++)
        {
            double v = O.value(ix, iy);
            if (std::isnan(v))
                continue;

            found = true;
            if (v < vmin) vmin = v;
            if (v > vmax) vmax = v;
        }
    }

    if (!found)
        return false;

    outMin = vmin;
    outMax = vmax;
    return true;
}


// ============================================================================
// NormalizeOverlay()
// Normalize overlay values to 0–1 range using provided min/max.
// ============================================================================
bool ClimatologyOverlayFactory::NormalizeOverlay(ClimatologyOverlay& O,
                                                 double vmin,
                                                 double vmax)
{
    if (!O.m_valid || vmax <= vmin)
        return false;

    double scale = 1.0 / (vmax - vmin);

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
// ============================================================================
void ClimatologyOverlayFactory::NormalizeAllOverlays()
{
    for (int month = 0; month < 12; month++)
    {
        for (int type = 0; type < OVERLAY_COUNT; type++)
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

    double sum = 0.0;
    double sumSq = 0.0;
    int count = 0;

    for (int iy = 0; iy < O.lat_count; iy++)
    {
        for (int ix = 0; ix < O.lon_count; ix++)
        {
            double v = O.value(ix, iy);
            if (std::isnan(v))
                continue;

            sum += v;
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
        for (int type = 0; type < OVERLAY_COUNT; type++)
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
        for (int type = 0; type < OVERLAY_COUNT; type++)
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
// GetOverlayColor()
// Map a normalized overlay value (0–1) to an RGB color using the active palette.
// ============================================================================
wxColour ClimatologyOverlayFactory::GetOverlayColor(double v)
{
    if (std::isnan(v))
        return wxColour(0, 0, 0);

    if (v < 0.0) v = 0.0;
    if (v > 1.0) v = 1.0;

    int idx = (int)(v * (m_colorPalette.size() - 1));
    if (idx < 0) idx = 0;
    if (idx >= (int)m_colorPalette.size())
        idx = (int)m_colorPalette.size() - 1;

    return m_colorPalette[idx];
}

// ============================================================================
// GetOverlayColorScaled()
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

    double v = (value - vmin) / (vmax - vmin);
    if (v < 0.0) v = 0.0;
    if (v > 1.0) v = 1.0;

    int idx = (int)(v * (m_colorPalette.size() - 1));
    if (idx < 0) idx = 0;
    if (idx >= (int)m_colorPalette.size())
        idx = (int)m_colorPalette.size() - 1;

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
// GenerateUnifiedColorGrid()
// Produce a color grid from multiple overlays blended together.
// Assumes all overlays are normalized (0–1).
// ============================================================================
void ClimatologyOverlayFactory::GenerateUnifiedColorGrid(const std::vector<const ClimatologyOverlay*>& overlays,
                                                         ClimatologyColorGrid& outGrid)
{
    if (overlays.empty())
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
            int count = 0;

            for (auto* O : overlays)
            {
                if (!O || !O->m_valid)
                    continue;

                double v = O->value(ix, iy);
                if (std::isnan(v))
                    continue;

                sum += v;
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
// GenerateUnifiedColorGridScaled()
// Blend multiple overlays using min/max scaling, then map to color.
// ============================================================================
void ClimatologyOverlayFactory::GenerateUnifiedColorGridScaled(
        const std::vector<const ClimatologyOverlay*>& overlays,
        ClimatologyColorGrid& outGrid,
        double vmin,
        double vmax)
{
    if (overlays.empty())
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
            int count = 0;

            for (auto* O : overlays)
            {
                if (!O || !O->m_valid)
                    continue;

                double v = O->value(ix, iy);
                if (std::isnan(v))
                    continue;

                sum += v;
                count++;
            }

            if (count == 0)
            {
                outGrid.set(ix, iy, wxColour(0, 0, 0));
                continue;
            }

            double blended = sum / count;
            outGrid.set(ix, iy, GetOverlayColorScaled(blended, vmin, vmax));
        }
    }
}

// ============================================================================
// GenerateUnifiedColorGridWeighted()
// Blend multiple overlays using per‑overlay weights, then map to color.
// Assumes all overlays are normalized (0–1).
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
// GenerateUnifiedColorGridWeightedScaled()
// Blend multiple overlays using per‑overlay weights, apply min/max scaling,
// then map to color.
// ============================================================================
void ClimatologyOverlayFactory::GenerateUnifiedColorGridWeightedScaled(
        const std::vector<const ClimatologyOverlay*>& overlays,
        const std::vector<double>& weights,
        ClimatologyColorGrid& outGrid,
        double vmin,
        double vmax)
{
    if (overlays.empty() || overlays.size() != weights.size())
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
            outGrid.set(ix, iy, GetOverlayColorScaled(blended, vmin, vmax));
        }
    }
}


// ============================================================================
// GenerateUnifiedColorGridMultiRange()
// Blend multiple overlays, each with its own (vmin, vmax) range.
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

    outGrid.allocate(base->lon_count, base->lat_count);

    for (int iy = 0; iy < base->lat_count; iy++)
    {
        for (int ix = 0; ix < base->lon_count; ix++)
        {
            double sum = 0.0;
            int count = 0;

            for (size_t i = 0; i < overlays.size(); i++)
            {
                const ClimatologyOverlay* O = overlays[i];
                double vmin = vmins[i];
                double vmax = vmaxs[i];

                if (!O || !O->m_valid || vmax <= vmin)
                    continue;

                double v = O->value(ix, iy);
                if (std::isnan(v))
                    continue;

                // Normalize per-overlay using its own range
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
// Blend multiple overlays, each with its own (vmin, vmax) range, then apply
// a final global scaling (global_vmin, global_vmax) before mapping to color.
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

    outGrid.allocate(base->lon_count, base->lat_count);

    for (int iy = 0; iy < base->lat_count; iy++)
    {
        for (int ix = 0; ix < base->lon_count; ix++)
        {
            double sum = 0.0;
            int count = 0;

            for (size_t i = 0; i < overlays.size(); i++)
            {
                const ClimatologyOverlay* O = overlays[i];
                double vmin = vmins[i];
                double vmax = vmaxs[i];

                if (!O || !O->m_valid || vmax <= vmin)
                    continue;

                double v = O->value(ix, iy);
                if (std::isnan(v))
                    continue;

                // Normalize per-overlay using its own range
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
// GenerateUnifiedColorGridMultiRangeWeightedGlobal()
// Blend multiple overlays using per‑overlay weights, each overlay having its
// own (vmin, vmax) range, then apply a final global normalization (0–1) and
// map to color.
// ============================================================================
void ClimatologyOverlayFactory::GenerateUnifiedColorGridMultiRangeWeightedGlobal(
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

    // First pass: compute global min/max of blended values
    double global_min = std::numeric_limits<double>::infinity();
    double global_max = -std::numeric_limits<double>::infinity();

    std::vector<double> blended_cache(base->lon_count * base->lat_count, 0.0);

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

            double blended = (wsum > 0.0) ? (sum / wsum) : std::numeric_limits<double>::quiet_NaN();
            blended_cache[iy * base->lon_count + ix] = blended;

            if (!std::isnan(blended))
            {
                if (blended < global_min) global_min = blended;
                if (blended > global_max) global_max = blended;
            }
        }
    }

    // Second pass: apply global normalization and map to color
    if (global_max <= global_min)
    {
        // Degenerate case: no valid data
        for (int iy = 0; iy < base->lat_count; iy++)
            for (int ix = 0; ix < base->lon_count; ix++)
                outGrid.set(ix, iy, wxColour(0, 0, 0));
        return;
    }

    for (int iy = 0; iy < base->lat_count; iy++)
    {
        for (int ix = 0; ix < base->lon_count; ix++)
        {
            double blended = blended_cache[iy * base->lon_count + ix];

            if (std::isnan(blended))
            {
                outGrid.set(ix, iy, wxColour(0, 0, 0));
                continue;
            }

            double gv = (blended - global_min) / (global_max - global_min);
            if (gv < 0.0) gv = 0.0;
            if (gv > 1.0) gv = 1.0;

            outGrid.set(ix, iy, GetOverlayColor(gv));
        }
    }
}

// ============================================================================
// GenerateUnifiedColorGridMultiRangeWeightedGlobalScaled()
// Blend overlays using per‑overlay weights and per‑overlay (vmin, vmax) ranges.
// First compute the global blended min/max, then apply a final global scaling
// (global_vmin, global_vmax) before mapping to color.
// ============================================================================
void ClimatologyOverlayFactory::GenerateUnifiedColorGridMultiRangeWeightedGlobalScaled(
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

    const int NX = base->lon_count;
    const int NY = base->lat_count;

    outGrid.allocate(NX, NY);

    // ------------------------------------------------------------------------
    // First pass: compute blended values + global blended min/max
    // ------------------------------------------------------------------------
    double blended_min =  std::numeric_limits<double>::infinity();
    double blended_max = -std::numeric_limits<double>::infinity();

    std::vector<double> blended_cache(NX * NY, std::numeric_limits<double>::quiet_NaN());

    for (int iy = 0; iy < NY; iy++)
    {
        for (int ix = 0; ix < NX; ix++)
        {
            double sum  = 0.0;
            double wsum = 0.0;

            for (size_t i = 0; i < overlays.size(); i++)
            {
                const ClimatologyOverlay* O = overlays[i];
                double w    = weights[i];
                double vmin = vmins[i];
                double vmax = vmaxs[i];

                if (!O || !O->m_valid || w <= 0.0 || vmax <= vmin)
                    continue;

                double v = O->value(ix, iy);
                if (std::isnan(v))
                    continue;

                // Normalize per-overlay
                double nv = (v - vmin) / (vmax - vmin);
                if (nv < 0.0) nv = 0.0;
                if (nv > 1.0) nv = 1.0;

                sum  += nv * w;
                wsum += w;
            }

            double blended = (wsum > 0.0)
                ? (sum / wsum)
                : std::numeric_limits<double>::quiet_NaN();

            blended_cache[iy * NX + ix] = blended;

            if (!std::isnan(blended))
            {
                if (blended < blended_min) blended_min = blended;
                if (blended > blended_max) blended_max = blended;
            }
        }
    }

    // ------------------------------------------------------------------------
    // Second pass: normalize using global blended min/max, then apply
    // final global scaling (global_vmin/global_vmax)
    // ------------------------------------------------------------------------
    const double range_blended = blended_max - blended_min;

    for (int iy = 0; iy < NY; iy++)
    {
        for (int ix = 0; ix < NX; ix++)
        {
            double blended = blended_cache[iy * NX + ix];

            if (std::isnan(blended) || range_blended <= 0.0)
            {
                outGrid.set(ix, iy, wxColour(0, 0, 0));
                continue;
            }

            // Normalize to [0,1] using global blended min/max
            double gv = (blended - blended_min) / range_blended;
            if (gv < 0.0) gv = 0.0;
            if (gv > 1.0) gv = 1.0;

            // Apply final global scaling
            if (global_vmax > global_vmin)
            {
                double sv = (gv - global_vmin) / (global_vmax - global_vmin);
                if (sv < 0.0) sv = 0.0;
                if (sv > 1.0) sv = 1.0;
                outGrid.set(ix, iy, GetOverlayColor(sv));
            }
            else
            {
                outGrid.set(ix, iy, GetOverlayColor(gv));
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
// GenerateUnifiedColorGridFromSingleOverlayScaled()
// Take a single overlay, normalize it using its own (vmin, vmax), compute the
// global min/max of the normalized field, then apply a final global scaling
// (global_vmin/global_vmax) before mapping to color.
// This mirrors the multi-overlay pipeline but for a single source.
// ============================================================================
void ClimatologyOverlayFactory::GenerateUnifiedColorGridFromSingleOverlayScaled(
        const ClimatologyOverlay* O,
        double vmin,
        double vmax,
        ClimatologyColorGrid& outGrid,
        double global_vmin,
        double global_vmax)
{
    if (!O || !O->m_valid || vmax <= vmin)
        return;

    const int NX = O->lon_count;
    const int NY = O->lat_count;

    outGrid.allocate(NX, NY);

    // ------------------------------------------------------------------------
    // First pass: normalize per-overlay and compute global min/max
    // ------------------------------------------------------------------------
    double blended_min =  std::numeric_limits<double>::infinity();
    double blended_max = -std::numeric_limits<double>::infinity();

    std::vector<double> cache(NX * NY, std::numeric_limits<double>::quiet_NaN());

    for (int iy = 0; iy < NY; iy++)
    {
        for (int ix = 0; ix < NX; ix++)
        {
            double v = O->value(ix, iy);
            if (std::isnan(v))
                continue;

            // Normalize using per-overlay range
            double nv = (v - vmin) / (vmax - vmin);
            if (nv < 0.0) nv = 0.0;
            if (nv > 1.0) nv = 1.0;

            cache[iy * NX + ix] = nv;

            if (nv < blended_min) blended_min = nv;
            if (nv > blended_max) blended_max = nv;
        }
    }

    const double range_blended = blended_max - blended_min;

    // ------------------------------------------------------------------------
    // Second pass: global normalization + final global scaling
    // ------------------------------------------------------------------------
    for (int iy = 0; iy < NY; iy++)
    {
        for (int ix = 0; ix < NX; ix++)
        {
            double nv = cache[iy * NX + ix];

            if (std::isnan(nv) || range_blended <= 0.0)
            {
                outGrid.set(ix, iy, wxColour(0, 0, 0));
                continue;
            }

            // Normalize to [0,1] using global blended min/max
            double gv = (nv - blended_min) / range_blended;
            if (gv < 0.0) gv = 0.0;
            if (gv > 1.0) gv = 1.0;

            // Apply final global scaling
            if (global_vmax > global_vmin)
            {
                double sv = (gv - global_vmin) / (global_vmax - global_vmin);
                if (sv < 0.0) sv = 0.0;
                if (sv > 1.0) sv = 1.0;
                outGrid.set(ix, iy, GetOverlayColor(sv));
            }
            else
            {
                outGrid.set(ix, iy, GetOverlayColor(gv));
            }
        }
    }
}

// ============================================================================
// GenerateUnifiedColorGridMultiOverlayAdditive()
// Add multiple overlays together directly (no weights, no per-overlay ranges).
// After summation, compute global min/max of the additive field, then apply
// final global scaling (global_vmin/global_vmax) before mapping to color.
// This is the simplest multi-overlay combiner in the unified-grid pipeline.
// ============================================================================
void ClimatologyOverlayFactory::GenerateUnifiedColorGridMultiOverlayAdditive(
        const std::vector<const ClimatologyOverlay*>& overlays,
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
    // First pass: additive sum + compute global min/max
    // ------------------------------------------------------------------------
    double vmin =  std::numeric_limits<double>::infinity();
    double vmax = -std::numeric_limits<double>::infinity();

    std::vector<double> cache(NX * NY, std::numeric_limits<double>::quiet_NaN());

    for (int iy = 0; iy < NY; iy++)
    {
        for (int ix = 0; ix < NX; ix++)
        {
            double sum = 0.0;


// ============================================================================
// GenerateUnifiedColorGridMultiOverlayMax()
// For each (ix, iy), take the maximum value across all overlays.
// After computing the max field, compute global min/max, then apply final
// global scaling (global_vmin/global_vmax) before mapping to color.
// ============================================================================
void ClimatologyOverlayFactory::GenerateUnifiedColorGridMultiOverlayMax(
        const std::vector<const ClimatologyOverlay*>& overlays,
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
    // First pass: compute max composite + global min/max
    // ------------------------------------------------------------------------
    double vmin =  std::numeric_limits<double>::infinity();
    double vmax = -std::numeric_limits<double>::infinity();

    std::vector<double> cache(NX * NY, std::numeric_limits<double>::quiet_NaN());

    for (int iy = 0; iy < NY; iy++)
    {
        for (int ix = 0; ix < NX; ix++)
        {
            double best = -std::numeric_limits<double>::infinity();
            bool valid = false;

            for (const ClimatologyOverlay* O : overlays)
            {
                if (!O || !O->m_valid)
                    continue;

                double v = O->value(ix, iy);
                if (std::isnan(v))
                    continue;

                if (v > best)
                    best = v;

                valid = true;
            }

            if (!valid)
                continue;

            cache[iy * NX + ix] = best;

            if (best < vmin) vmin = best;
            if (best > vmax) vmax = best;
        }
    }

    const double range = vmax - vmin;

    // ------------------------------------------------------------------------
    // Second pass: normalize + final global scaling
    // ------------------------------------------------------------------------
    for (int iy = 0; iy < NY; iy++)
    {
        for (int ix = 0; ix < NX; ix++)
        {
            double v = cache[iy * NX + ix];

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
// GenerateUnifiedColorGridMultiOverlayMin()
// For each (ix, iy), take the minimum value across all overlays.
// After computing the min field, compute global min/max, then apply final
// global scaling (global_vmin/global_vmax) before mapping to color.
// ============================================================================
void ClimatologyOverlayFactory::GenerateUnifiedColorGridMultiOverlayMin(
        const std::vector<const ClimatologyOverlay*>& overlays,
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
    // First pass: compute min composite + global min/max
    // ------------------------------------------------------------------------
    double vmin =  std::numeric_limits<double>::infinity();
    double vmax = -std::numeric_limits<double>::infinity();

    std::vector<double> cache(NX * NY, std::numeric_limits<double>::quiet_NaN());

    for (int iy = 0; iy < NY; iy++)
    {
        for (int ix = 0; ix < NX; ix++)
        {
            double best =  std::numeric_limits<double>::infinity();
            bool valid = false;

            for (const ClimatologyOverlay* O : overlays)
            {
                if (!O || !O->m_valid)
                    continue;

                double v = O->value(ix, iy);
                if (std::isnan(v))
                    continue;

                if (v < best)
                    best = v;

                valid = true;
            }

            if (!valid)
                continue;

            cache[iy * NX + ix] = best;

            if (best < vmin) vmin = best;
            if (best > vmax) vmax = best;
        }
    }

    const double range = vmax - vmin;

    // ------------------------------------------------------------------------
    // Second pass: normalize + final global scaling
    // ------------------------------------------------------------------------
    for (int iy = 0; iy < NY; iy++)
    {
        for (int ix = 0; ix < NX; ix++)
        {
            double v = cache[iy * NX + ix];

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

// ============================================================================
// GenerateUnifiedColorGridMultiOverlayMean()
// For each (ix, iy), compute the arithmetic mean across all overlays.
// After computing the mean field, compute global min/max, then apply final
// global scaling (global_vmin/global_vmax) before mapping to color.
// ============================================================================
void ClimatologyOverlayFactory::GenerateUnifiedColorGridMultiOverlayMean(
        const std::vector<const ClimatologyOverlay*>& overlays,
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
    // First pass: compute mean composite + global min/max
    // ------------------------------------------------------------------------
    double vmin =  std::numeric_limits<double>::infinity();
    double vmax = -std::numeric_limits<double>::infinity();

    std::vector<double> cache(NX * NY, std::numeric_limits<double>::quiet_NaN());

    for (int iy = 0; iy < NY; iy++)
    {
        for (int ix = 0; ix < NX; ix++)
        {
            double sum = 0.0;
            int count = 0;

            for (const ClimatologyOverlay* O : overlays)
            {
                if (!O || !O->m_valid)
                    continue;

                double v = O->value(ix, iy);
                if (std::isnan(v))
                    continue;

                sum += v;
                count++;
            }

            if (count == 0)
                continue;

            double mean = sum / count;
            cache[iy * NX + ix] = mean;

            if (mean < vmin) vmin = mean;
            if (mean > vmax) vmax = mean;
        }
    }

    const double range = vmax - vmin;

    // ------------------------------------------------------------------------
    // Second pass: normalize + final global scaling
    // ------------------------------------------------------------------------
    for (int iy = 0; iy < NY; iy++)
    {
        for (int ix = 0; ix < NX; ix++)
        {
            double v = cache[iy * NX + ix];

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
// GenerateUnifiedColorGridMultiOverlayMedian()
// For each (ix, iy), compute the median value across all overlays.
// After computing the median field, compute global min/max, then apply final
// global scaling (global_vmin/global_vmax) before mapping to color.
// ============================================================================
void ClimatologyOverlayFactory::GenerateUnifiedColorGridMultiOverlayMedian(
        const std::vector<const ClimatologyOverlay*>& overlays,
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
    // First pass: compute median composite + global min/max
    // ------------------------------------------------------------------------
    double vmin =  std::numeric_limits<double>::infinity();
    double vmax = -std::numeric_limits<double>::infinity();

    std::vector<double> cache(NX * NY, std::numeric_limits<double>::quiet_NaN());

    for (int iy = 0; iy < NY; iy++)
    {
        for (int ix = 0; ix < NX; ix++)
        {
            std::vector<double> vals;
            vals.reserve(overlays.size());

            for (const ClimatologyOverlay* O : overlays)
            {
                if (!O || !O->m_valid)
                    continue;

                double v = O->value(ix, iy);
                if (!std::isnan(v))
                    vals.push_back(v);
            }

            if (vals.empty())
                continue;

            std::sort(vals.begin(), vals.end());

            double median;
            const size_t n = vals.size();

            if (n % 2 == 1)
                median = vals[n / 2];
            else
                median = 0.5 * (vals[n / 2 - 1] + vals[n / 2]);

            cache[iy * NX + ix] = median;

            if (median < vmin) vmin = median;
            if (median > vmax) vmax = median;
        }
    }

    const double range = vmax - vmin;

    // ------------------------------------------------------------------------
    // Second pass: normalize + final global scaling
    // ------------------------------------------------------------------------
    for (int iy = 0; iy < NY; iy++)
    {
        for (int ix = 0; ix < NX; ix++)
        {
            double v = cache[iy * NX + ix];

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

    switch (mode)
    {
        // ------------------------------------------------------------
        // Single overlay, scaled
        // ------------------------------------------------------------
        case DisplayMode::SingleScaled:
        {
            const ClimatologyOverlay* O = overlays[0];
            double vmin = vmins.empty() ? 0.0 : vmins[0];
            double vmax = vmaxs.empty() ? 1.0 : vmaxs[0];

            GenerateUnifiedColorGridFromSingleOverlayScaled(
                O, vmin, vmax, outGrid, global_vmin, global_vmax);
            return;
        }

        // ------------------------------------------------------------
        // Weighted multi‑overlay blend (Chunk 67)
        // ------------------------------------------------------------
        case DisplayMode::WeightedBlend:
        {
            GenerateUnifiedColorGridMultiRangeWeightedGlobalScaled(
                overlays, weights, vmins, vmaxs,
                outGrid, global_vmin, global_vmax);
            return;
        }

        // ------------------------------------------------------------
        // Additive composite (Chunk 70)
        // ------------------------------------------------------------
        case DisplayMode::Additive:
        {
            GenerateUnifiedColorGridMultiOverlayAdditive(
                overlays, outGrid, global_vmin, global_vmax);
            return;
        }

        // ------------------------------------------------------------
        // Max composite (Chunk 71)
        // ------------------------------------------------------------
        case DisplayMode::MaxComposite:
        {
            GenerateUnifiedColorGridMultiOverlayMax(
                overlays, outGrid, global_vmin, global_vmax);
            return;
        }

        // ------------------------------------------------------------
        // Min composite (Chunk 72)
        // ------------------------------------------------------------
        case DisplayMode::MinComposite:
        {
            GenerateUnifiedColorGridMultiOverlayMin(
                overlays, outGrid, global_vmin, global_vmax);
            return;
        }

        // ------------------------------------------------------------
        // Mean composite (Chunk 73)
        // ------------------------------------------------------------
        case DisplayMode::MeanComposite:
        {
            GenerateUnifiedColorGridMultiOverlayMean(
                overlays, outGrid, global_vmin, global_vmax);
            return;
        }

        // ------------------------------------------------------------
        // Median composite (Chunk 74)
        // ------------------------------------------------------------
        case DisplayMode::MedianComposite:
        {
            GenerateUnifiedColorGridMultiOverlayMedian(
                overlays, outGrid, global_vmin, global_vmax);
            return;
        }

        // ------------------------------------------------------------
        // Default: fallback to single overlay
        // ------------------------------------------------------------
        default:
        {
            const ClimatologyOverlay* O = overlays[0];
            double vmin = vmins.empty() ? 0.0 : vmins[0];
            double vmax = vmaxs.empty() ? 1.0 : vmaxs[0];

            GenerateUnifiedColorGridFromSingleOverlayScaled(
                O, vmin, vmax, outGrid, global_vmin, global_vmax);
            return;
        }
    }
}

// ============================================================================
// GatherOverlaysForMode()
// Selects which overlays should be combined based on the display mode,
// the selected climatology type, and the current month/season.
// This is called BEFORE GenerateUnifiedColorGrid().
// ============================================================================
std::vector<const ClimatologyOverlay*>
ClimatologyOverlayFactory::GatherOverlaysForMode(
        DisplayMode mode,
        OverlayType type,
        int month_index)
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
        const ClimatologyOverlay* O = GetOverlay(type, month_index);
        if (O && O->m_valid)
            result.push_back(O);
        return result;
    }

    // ------------------------------------------------------------------------
    // 2. Multi-overlay modes (weighted, mean, median, max, min, additive)
    // These modes require multiple overlays for the same climatology type.
    // Example: seasonal composites, ensemble models, multi-source blends.
    // ------------------------------------------------------------------------
	const auto vec_const = MakeConstVector(m_overlay_map[type]);  // e.g., all wind overlays

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


// ============================================================================
// GatherOverlayMetadata()
// Produces weights, vmins, vmaxs, and global scaling parameters for the
// overlays selected by GatherOverlaysForMode().
// This is called BEFORE GenerateUnifiedColorGrid().
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

    // ------------------------------------------------------------------------
    // 1. Default global scaling
    // These may be overridden by user settings or mode-specific logic.
    // ------------------------------------------------------------------------
    global_vmin = 0.0;
    global_vmax = 1.0;

    // ------------------------------------------------------------------------
    // 2. Single overlay mode
    // ------------------------------------------------------------------------
    if (mode == DisplayMode::SingleScaled)
    {
        if (N == 1 && overlays[0])
        {
            const ClimatologyOverlay* O = overlays[0];

            // Use overlay metadata if available
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
    // 3. Weighted blend mode
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

            // Weight from overlay metadata or default
            double w = (O->weight > 0.0) ? O->weight : 1.0;
            weights.push_back(w);

            // Per-overlay range
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
    // 4. Mean / Median / Additive / Max / Min composites
    // These modes do not use per-overlay weights.
    // They DO use per-overlay ranges if available.
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

