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

#include <memory>
#include <cmath>
#include <algorithm>
#include <functional>
#include <map>

#include "ClimatologyOverlayFactory.h"
#include "ClimatologyDataModel.h"
#include "IsoBarMap.h"
#include "ocpn_plugin.h"
#include "climatology_shaders.h"
#include "ClimatologyCoord.h"
#include "CycloneFilterParams.h"
#include "CycloneTypes.h"
#include "defs.h"


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


// ==============================================================
// Modern unified IsoBarMap subclass
// ===============================================================

class ClimatologyIsoBarMap : public IsoBarMap
{
public:
    ClimatologyIsoBarMap(const wxString& name,
                         double spacing,
                         double step,
                         ClimatologyOverlayFactory& factory,
                         int overlayType,                 // <- any OVERLAY_*
                         int units,
                         int month,
                         int day,
                         const ClimatologyRenderParams& renderParams)
        : IsoBarMap(name,
                    spacing,
                    step,
                    factory,
                    overlayType,                        // <- pass overlayType to base
                    renderParams),
          m_factory(factory),
          m_setting(overlayType),                      // <- store overlayType
          m_units(units),
          m_month(month),
          m_day(day),
          m_renderParams(renderParams)
    {}

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
    double CalcParameter(double lat, double lon) override
    {
        // Unified-grid lookup (month-aware, any scalar overlay)
        return m_factory.getCurValue(
            SCALAR,            // scalar field (pressure, temp, etc.)
            m_setting,         // overlay type (OVERLAY_PRESSURE, OVERLAY_AIRTEMP, ...)
            lat,
            lon,
            m_renderParams
        );
    }

    ClimatologyOverlayFactory&      m_factory;
    int                             m_setting;       // overlay id
    int                             m_units;
    int                             m_month;
    int                             m_day;
    const ClimatologyRenderParams&  m_renderParams;
};


// External GL shader + matrix utilities
extern GLuint pi_texture_2DA_shader_program;
typedef float mat4x4[16];
extern void mat4x4_identity(mat4x4 M);


// =============================================================
// Construction / Destruction / Load
// =============================================================

// OpenCPN passes the data directory - modern plugin loading.
// Constructor A (with dataDir) Used in modern plugin loading.
ClimatologyOverlayFactory::ClimatologyOverlayFactory(wxWindow* parent, const wxString& dataDir)
    : wxEvtHandler(),
      m_parent_window(parent),
      m_dataDir(dataDir),
      m_bCompletedLoading(false),
      m_hasCycloneData(false),
      m_hasENSOData(false),
      m_shaders_loaded(false)
{
    memset(m_pIsobars, 0, sizeof(m_pIsobars));

    // Unified NOAA Grid Model
    m_dataModel = std::make_unique<ClimatologyDataModel>();

    std::vector<GridInfo> meta = BuildGridInfo(m_dataDir);
    m_dataModel->InitGridMetadata(meta);
}


// Factory discovers the directory itself. 
// Used for older OpenCPN or test harnesses.
// Constructor B (auto‑discover dataDir) This is normal and intentional.
ClimatologyOverlayFactory::ClimatologyOverlayFactory(wxWindow* parent)
    : wxEvtHandler(),
      m_parent_window(parent),
      m_bCompletedLoading(false),
      m_hasCycloneData(false),
      m_hasENSOData(false),
      m_shaders_loaded(false)
{
    m_dataDir = GetPluginDataDir("climatology_pi");

    // Unified NOAA Grid Model
    m_dataModel = std::make_unique<ClimatologyDataModel>();

    std::vector<GridInfo> meta = BuildGridInfo(m_dataDir);
    m_dataModel->InitGridMetadata(meta);
}


// ==============================================================
// Factory destructor
// ==============================================================
ClimatologyOverlayFactory::~ClimatologyOverlayFactory()
{
    for (int m = 0; m < 13; ++m)
    {
        for (int s = 0; s < NUM_OVERLAYS; ++s)
            delete m_pIsobars[s][m];

        for (int s = 0; s < NUM_OVERLAYS; ++s)
            delete[] m_pOverlay[m][s].m_data;
    }
}



bool ClimatologyOverlayFactory::Load()
{
    wxGenericProgressDialog* progress = nullptr; // if you use one
    m_bCompletedLoading = LoadInternal(progress);
    return m_bCompletedLoading;
}


bool ClimatologyOverlayFactory::LoadInternal(wxGenericProgressDialog* progress)
{
    wxLogMessage("Climatology: LoadInternal() starting");

    bool ok = true;
    wxString dataDir = m_dataDir;

// ------------------------------------------------------------
// 1. Load scalar overlays directly into unified model
// ------------------------------------------------------------

	if (progress) progress->Pulse(_("Loading scalar climatology datasets…"));

	struct ScalarSpec {
		int setting;
		const char* name;
	};

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
							 S.name,
							 m,
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

	// Sea depth (non-monthly)
	{
		int rows, cols;
		double lat0, lon0, latStep, lonStep;

		float* buf = ReadNOAAFileUnified(
						 "seadepth",
						 0,
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

	// ------------------------------------------------------------
    // 2. Load vector overlays directly into unified model (wind/current)
    // ------------------------------------------------------------
	
	if (progress) progress->Pulse(_("Loading wind/current datasets…"));

	struct VectorSpec {
		int setting;
		const char* prefix;
	};

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
	
	// ------------------------------------------------------------
    // 3. Average monthly data (unified)
    // ------------------------------------------------------------
    if (progress) progress->Pulse(_("Averaging monthly datasets…"));

    ok &= AverageWindData();
    ok &= AverageCurrentData();
    if (!ok) return false;
	
	// ------------------------------------------------------------
    // 4. Load cyclone basins + ENSO
    // --------------------------------v----------------------------
    if (progress) progress->Pulse(_("Loading cyclone + ENSO datasets…"));


	m_hasCycloneData = !m_cyclones.empty();

    m_hasENSOData = LoadENSODataFromCSV("enso.csv");

    // ------------------------------------------------------------
    // 5. Done
    // ------------------------------------------------------------
    m_bCompletedLoading = ok;
    wxLogMessage("Climatology: LoadInternal() completed");

    return ok;
} 


// ============================================================================
// Params / Viewport
// ============================================================================

void ClimatologyOverlayFactory::SetParams(const StandardDisplayParams& p)
{
    m_params = p;

    // Nothing to do here for cyclone filters.
    // CycloneFilterParams is pushed separately via SetCycloneFilter().
}


void ClimatologyOverlayFactory::SetCycloneFilter(const CycloneFilterParams& params)
{
    m_cycloneParams = params;
}


void ClimatologyOverlayFactory::SetViewPort(PlugIn_ViewPort* vp)
{
    if (vp)
        m_vp = *vp;
}

// ============================================================
// Modern unified Render() entry point — all features preserved
// ============================================================
bool ClimatologyOverlayFactory::Render(const ClimatologyRenderParams& p)
{
    if (!p.vp || !p.vp->bValid)
        return false;

    SetViewPort(p.vp);

    const int setting = p.overlayType;

    // Wind Atlas
    if (p.windAtlas.enabled &&
        p.showOverlayType[OVERLAY_WIND_ATLAS])
    {
        RenderWindAtlas(p);
    }

    // Cyclones
    if (p.showCyclones)
    {
        RenderCyclones(p);
    }

    // GL path
    if (!p.dc->GetDC())
    {
        // Scalar overlay map
        if (p.showOverlayType[setting])
            RenderOverlayMap(p);

        // Wind arrows
        if (p.showArrows[OVERLAY_WIND])
            RenderDirectionArrows(p);

        // Current arrows
        if (p.showArrows[OVERLAY_CURRENT] &&
            p.showOverlayType[OVERLAY_CURRENT])
        {
            RenderDirectionArrows(p);
        }

        // Isobars
        if (p.showIsoBars)
            RenderIsoBars(p);

        // Numbers
        if (p.showNumbers[setting])
            RenderNumbers(p);

        return true;
    }

    // DC path: overlay map unsupported, but arrows/isobars/numbers still work
    if (p.showArrows[OVERLAY_WIND])
        RenderDirectionArrows(p);

    if (p.showArrows[OVERLAY_CURRENT] &&
        p.showOverlayType[OVERLAY_CURRENT])
    {
        RenderDirectionArrows(p);
    }

    if (p.showOverlayType[OVERLAY_PRESSURE])
        RenderIsoBars(p);

    if (p.showNumbers[setting])
        RenderNumbers(p);

    return true;
}


// ============================================================
// Modern RenderOverlayMap — simplified signature, full features
// ============================================================
void ClimatologyOverlayFactory::RenderOverlayMap(const ClimatologyRenderParams& p)
{
    if (!IsCompletedLoading())
        return;

    PlugIn_ViewPort& vp = *p.vp;
    const int setting = m_params.overlayType;

    if (!m_params.showOverlayType[setting])
        return;

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

    // DC fallback
    if (p.dc->GetDC())
    {
        wxDC* dc = p.dc->GetDC();
        dc->SetTextForeground(*wxRED);
        dc->DrawText(
            _("Climatology overlay map unsupported unless OpenGL is enabled"),
            10, 10);
        return;
    }

	// GL path
	O1.m_width  = vp.pix_width;
	O1.m_height = vp.pix_height;

	O2.m_width  = vp.pix_width;
	O2.m_height = vp.pix_height;

	if (!O1.m_data)
		BuildOverlayData(O1, setting, month);
	if (!O1.m_iTexture)
		CreateGLTexture(O1);

	if (!O2.m_data)
		BuildOverlayData(O2, setting, nextMonth);
	if (!O2.m_iTexture)
		CreateGLTexture(O2);

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    double alpha = m_params.transparency / 100.0;

    DrawGLTexture(O1.m_iTexture,
                  O2.m_iTexture,
                  dpos,
                  vp,
                  alpha);

    glDisable(GL_BLEND);
}



// =========================================
// UNIFIED DATA LOOKUP  (scalar, U and V
// ================================================
// this lookup logic is in ClimatologyDataModel / UnifiedGrid, 
// double UnifiedGrid::valueAt(double lat, double lon) const;
// double UnifiedGrid::uAt(double lat, double lon) const;
// double UnifiedGrid::vAt(double lat, double lon) const;

// ======================================================================
//  UNIFIED LOADERS  - ONLY ONES TO USE
// ======================================================================

// --------------------------------------------------
// Unified Scalar Loader (final version)  -- ONLY ONES TO USE
// --------------------------------------------------
// loader takes a contiguous scalar buffer (float array or vector) and //populates the unified grid:
// m_dataModel->LoadAllNOAAData(m_dataDir);
// OUTDATED


// ---------------------------------------------------------------------
// Unified Vector Loader (final version) --ONLY ONES TO USE
// ----------------------------------------------------------------------
// loader takes contiguous U/V buffers and populates the unified grid:

void ClimatologyOverlayFactory::LoadVectorFieldUnified(
        int setting,
        int month,
        const float* srcU,
        const float* srcV,
        int latitudes,
        int longitudes,
        double lat0,
        double lon0,
        double latStep,
        double lonStep)
{
    // Unified model does not accept plugin-side vector loading.
    // NOAA wind/current grids are loaded internally by ClimatologyDataModel.
    // This function is now a no-op.
}

bool ClimatologyOverlayFactory::AverageWindData()
{
    // Unified NOAA grid model handles NaN cleanup internally.
    // OverlayFactory no longer averages wind data.
    return true;
}

bool ClimatologyOverlayFactory::AverageCurrentData()
{
    // Unified NOAA grid model handles NaN cleanup internally.
    // OverlayFactory no longer averages current data.
    return true;
}

// -----------------------------------------------------------------------
// ReadNOAAFileUnified - UNIFIED A minimal helper stub used by all loaders.
// -----------------------------------------------------------------------

float* ClimatologyOverlayFactory::ReadNOAAFileUnified(
        const wxString& name,
        int month,
        int& rows,
        int& cols,
        double& lat0,
        double& lon0,
        double& latStep,
        double& lonStep)
{
    wxString dataDir = m_dataDir;

    // NOAA naming convention: <name><month>.gz
    wxString gzFile  = wxString::Format("%s/%s%02d.gz", dataDir, name, month+1);
    wxString datFile = wxString::Format("%s/%s/%02d.dat", dataDir, name, month+1);

    wxString file;
    if (wxFileExists(gzFile)) file = gzFile;
    else if (wxFileExists(datFile)) file = datFile;
    else {
        wxLogWarning("Climatology: Missing NOAA file: %s", gzFile);
        return nullptr;
    }

    // ------------------------------------------------------------
    // Load metadata from companion .meta file
    // ------------------------------------------------------------
    wxString metaFile = file + ".meta";
    if (!wxFileExists(metaFile)) {
        wxLogWarning("Climatology: Missing metadata file: %s", metaFile);
        return nullptr;
    }

    wxFileInputStream meta(metaFile);
    meta.Read(&rows,    sizeof(int));
    meta.Read(&cols,    sizeof(int));
    meta.Read(&lat0,    sizeof(double));
    meta.Read(&lon0,    sizeof(double));
    meta.Read(&latStep, sizeof(double));
    meta.Read(&lonStep, sizeof(double));

    // ------------------------------------------------------------
    // Allocate unified buffer
    // ------------------------------------------------------------
    float* buf = new float[rows * cols];

    // ------------------------------------------------------------
    // Use your existing decompression logic
    // ------------------------------------------------------------
    wxFileInputStream in(file);
    if (!in.IsOk()) {
        delete[] buf;
        return nullptr;
    }

    wxZlibInputStream zin(in);
    wxInputStream* stream = file.EndsWith(".gz") ? (wxInputStream*)&zin : (wxInputStream*)&in;

    size_t total = rows * cols * sizeof(float);
    stream->Read(buf, total);

    if (!stream->IsOk()) {
        wxLogWarning("Climatology: Failed reading NOAA file: %s", file);
        delete[] buf;
        return nullptr;
    }

    return buf;
}


// ============================================================================
// Overlay Map Pipeline  - Scalar Overlay Map (GL textures)
// ============================================================================

// ------------------------------------------------------------
// HASDATAFOR — Check presence of data arrays per overlay/month
// ------------------------------------------------------------

bool ClimatologyOverlayFactory::HasDataFor(int setting, int month)
{
    if (!m_dataModel)
        return false;
    return m_dataModel->HasMonth(setting, month);
}

wxColour ClimatologyOverlayFactory::MapCyclonePointToColorENSO(const CyclonePoint& pt) const
{
    // Simple placeholder: color by basin or ENSO state
    return *wxRED;
}





bool ClimatologyDataModel::HasMonth(int setting, int month) const
{
    if (setting < 0 || setting >= NUM_OVERLAYS)
        return false;
    if (month < 0 || month >= 12)
        return false;

    return m_grids[setting][month].isValid();
}


// --------------------------------------------------------------
// GET VALUE MONTH   -Raw monthly value lookup from data arrays
// --------------------------------------------------------------
// Returns the value for ONE month, for ONE overlay, at ONE lat/lon, 
// for ONE coordinate type. An atomic data accessor. 
//  m_data[setting][month].lookup(lat, lon)

double ClimatologyOverlayFactory::getValueMonth(enum Coord coord,
                                                int setting,
                                                double lat, double lon,
                                                int month) const
{
    if (!m_dataModel || !m_dataModel->HasMonth(setting, month))
        return NAN;

    const UnifiedGrid& grid = m_dataModel->GetGrid(setting, month);

    // Vector overlays (wind, current)
    if (setting == OVERLAY_WIND || setting == OVERLAY_CURRENT)
    {
        double u = grid.uAt(lat, lon);
        double v = grid.vAt(lat, lon);

        if (std::isnan(u) || std::isnan(v))
            return NAN;

        switch (coord)
        {
            case U:     return u;
            case V:     return v;
            case MAG:   return std::sqrt(u*u + v*v);
            case DIR:   return std::atan2(u, v) * 180.0 / M_PI;
            default:    return NAN;
        }
    }

    // Scalar overlays
    double s = grid.valueAt(lat, lon);
    return std::isnan(s) ? NAN : s;
}



// ------------------------------------------------------------
// getCurValue  - Time interpolation + spatial interpolation
// ------------------------------------------------------------
// returns the interpolated (blended) value between two months, 
// based on the current animation / slider position. 
// temporal interpolation layer.

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

// ------------------------------------------------------------
// BuildOverlayData - Fill O.m_data with RGBA based on getValueMonth + ColorMap
// ------------------------------------------------------------
// Returns correct values for all overlays. Works with RenderOverlayMap()
// Textures are created only once per month/setting, exactly as you designed.
// Preserves:  wind arrows, current arrows, scalar overlays, magnitude overlays,
// direction overlays, // interpolation, GL rendering

bool ClimatologyOverlayFactory::BuildOverlayData(ClimatologyOverlay& O,
                                                 int setting,
                                                 int month)
{
    if (O.m_data)
        return true;

    const int w = O.m_width;
    const int h = O.m_height;

    O.m_data = new unsigned char[w * h * 4];

    // MAG for vector overlays, SCALAR for everything else
    Coord coord = (setting == OVERLAY_WIND || setting == OVERLAY_CURRENT)
                  ? MAG
                  : SCALAR;

    for (int y = 0; y < h; y++)
    {
        // Mercator → latitude
        double merc = 1.0 - 2.0 * (double)y / (double)(h - 1);
        double lat  = rad2deg(atan(sinh(merc * M_PI)));

		for (int x = 0; x < w; x++)
		{
			// Linear → longitude
			double lon = 360.0 * (double)x / (double)(w - 1);

			// Unified-grid lookup (month-aware)
			double v = getCurValue(coord, setting, lat, lon, p);

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


// ------------------------------------------------------------
// CreateGLTexture — unchanged, used by RenderOverlayMap
// ------------------------------------------------------------
// Create GL texture from O.m_data upload pixel buffer to GPU
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


// -------------------------------------------------------------------- 
// Draw GL Texture -  Bind textures, draw quad, blend tex1/tex2 by dpos
//----------------------------------------------------------------------
void ClimatologyOverlayFactory::DrawGLTexture(GLuint tex1, GLuint tex2,
                                              double dpos,
                                              PlugIn_ViewPort &vp,
                                              double transparency)
{
    // Screen quad
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

    // Texture samplers
    GLint texUni1 = glGetUniformLocation(pi_texture_2DA_shader_program, "uTex1");
    GLint texUni2 = glGetUniformLocation(pi_texture_2DA_shader_program, "uTex2");
    glUniform1i(texUni1, 0);
    glUniform1i(texUni2, 1);

    // Blend factor
    GLint blendLoc = glGetUniformLocation(pi_texture_2DA_shader_program, "blendFactor");
    glUniform1f(blendLoc, (float)dpos);

    // Transparency
    float colorv[4] = {0, 0, 0, -(float)transparency};
    GLint colloc = glGetUniformLocation(pi_texture_2DA_shader_program, "color");
    glUniform4fv(colloc, 1, colorv);

    // No VBOs
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);

    glVertexAttribPointer(mPosAttrib, 2, GL_FLOAT, GL_FALSE, 0, coords);
    glEnableVertexAttribArray(mPosAttrib);

    glVertexAttribPointer(mUvAttrib, 2, GL_FLOAT, GL_FALSE, 0, uv);
    glEnableVertexAttribArray(mUvAttrib);

    // Identity transform
    mat4x4 I;
    mat4x4_identity(I);

    GLint matloc = glGetUniformLocation(pi_texture_2DA_shader_program, "TransformMatrix");
    if (matloc != -1)
        glUniformMatrix4fv(matloc, 1, GL_FALSE, (const GLfloat*)I);

    // Bind textures
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, tex1);

    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, tex2);

    // Draw
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
}


//=======================================================================
//  RenderOverlayMap
//=======================================================================
//  See Method up near line 390


// ============================================================================
// DIRECTION ARROWS RENDER (Wind + Current)(GL + DC unified)
// ============================================================================

void ClimatologyOverlayFactory::RenderDirectionArrows(const ClimatologyRenderParams& p)
{
    if (!IsCompletedLoading())
        return;
    if (!p.vp || !p.vp->bValid)
        return;

    const int windSetting    = OVERLAY_WIND;
    const int currentSetting = OVERLAY_CURRENT;

    // Wind arrows require:
    //   - overlay visible
    //   - arrows enabled
    //   - user wants direction arrows
	bool drawWind =
		m_params.showOverlayType[windSetting] &&
		m_params.showArrows[windSetting];

    // Current arrows require:
    //   - overlay visible
    //   - arrows enabled
	bool drawCurrent =
		m_params.showOverlayType[currentSetting] &&
		m_params.showArrows[currentSetting];

    if (!drawWind && !drawCurrent)
        return;

    if (p.useGL)
        RenderDirectionArrowsGL(p);
    else
        RenderDirectionArrowsDC(p);
}



// ============================================================================
// RENDER DC Direction Arrows -modern for sure
// ============================================================================

void ClimatologyOverlayFactory::RenderDirectionArrowsDC(const ClimatologyRenderParams& p)
{
    wxDC* dc = p.dc->GetDC();
    if (!dc) return;

    PlugIn_ViewPort& vp = *p.vp;

    auto drawForSetting = [&](int setting)
    {
        if (!m_params.showArrows[setting])
            return;

        // Arrow appearance
        double size      = m_params.arrowSize[setting];
        int    width     = m_params.arrowWidth[setting];
        wxColour color   = m_params.color[setting];
        bool lengthMode  = m_params.arrowMode[setting];

        // Native NOAA grid resolution (provided by unified loader)
        double latStep = p.latStep[setting];
        double lonStep = p.lonStep[setting];

        // Unified spacing model — arrows follow NOAA grid
        double step = std::max(latStep, lonStep);

        for (double lat = std::floor(vp.lat_min / step) * step;
             lat <= vp.lat_max;
             lat += step)
        {
            for (double lon = std::floor(vp.lon_min / step) * step;
                 lon <= vp.lon_max;
                 lon += step)
            {
                // Sample vector field
                double u = getCurValue(U, setting, lat, lon, p);
                double v = getCurValue(V, setting, lat, lon, p);

                if (std::isnan(u) || std::isnan(v))
                    continue;

                if (setting == OVERLAY_WIND)
                    u = -u, v = -v;

                double mag = hypot(u, v);
                if (mag <= 0 || std::isnan(mag))
                    continue;

                // Barb/length mode scaling
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

                // Apply viewport rotation
                double t = vp.rotation;
                double x = -size * (u * cos(t) + v * sin(t));
                double y =  size * (v * cos(t) - u * sin(t));

                wxPoint p1;
                GetCanvasPixLL(&vp, &p1, lat, lon);

                // Main shaft
                DrawArrowDC(dc,
                            p1.x + x, p1.y + y,
                            p1.x - x, p1.y - y,
                            color, width);

                // Barbs (wind only)
                if (!lengthMode)
                    DrawBarbsDC(dc, p1.x, p1.y, x, y, mag, cstep, color, width);

                // Arrowhead
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
// RENDER GL Direction Arrows -modern  for sure
// ============================================================================

void ClimatologyOverlayFactory::RenderDirectionArrowsGL(const ClimatologyRenderParams& p)
{
    if (!p.vp || !p.vp->bValid)
        return;

    PlugIn_ViewPort& vp = *p.vp;

    auto drawForSetting = [&](int setting)
    {
        if (!m_params.showArrows[setting])
            return;

        // Arrow appearance
        double size      = m_params.arrowSize[setting];
        int    width     = m_params.arrowWidth[setting];
        wxColour color   = m_params.color[setting];
        bool lengthMode  = m_params.arrowMode[setting];

        // Native NOAA grid resolution
        double latStep = p.latStep[setting];
        double lonStep = p.lonStep[setting];

        // Unified spacing model — arrows follow NOAA grid
        double step = std::max(latStep, lonStep);

        for (double lat = std::floor(vp.lat_min / step) * step;
             lat <= vp.lat_max;
             lat += step)
        {
            for (double lon = std::floor(vp.lon_min / step) * step;
                 lon <= vp.lon_max;
                 lon += step)
            {
                // Unified U/V lookup
                double u = getCurValue(U, setting, lat, lon, p);
                double v = getCurValue(V, setting, lat, lon, p);

                if (std::isnan(u) || std::isnan(v))
                    continue;

                // Wind direction reversal (NOAA convention)
                if (setting == OVERLAY_WIND)
                    u = -u, v = -v;

                double mag = hypot(u, v);
                if (mag <= 0 || std::isnan(mag))
                    continue;

                // Barb/length mode scaling
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

                // Apply viewport rotation
                double t = vp.rotation;
                double x = -size * (u * cos(t) + v * sin(t));
                double y =  size * (v * cos(t) - u * sin(t));

                // Convert lat/lon → screen pixel
                wxPoint p1;
                GetCanvasPixLL(&vp, &p1, lat, lon);

                // Main shaft
                DrawArrowGL(p1.x + x, p1.y + y,
                            p1.x - x, p1.y - y,
                            color, width);

                // Barbs (only in direction mode)
                if (!lengthMode)
                    DrawBarbsGL(p1.x, p1.y, x, y, mag, cstep, color, width);

                // Arrowhead
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
// DRAW ARROWS + BARBS -Shared GL/DC helpers  - Modern too 
//============================================================================

void ClimatologyOverlayFactory::DrawArrowGL(double x1, double y1,
                                            double x2, double y2,
                                            const wxColour& c,
                                            int width)
{
    glColor4ub(c.Red(), c.Green(), c.Blue(), 255);
    glLineWidth((float)width);

    glBegin(GL_LINES);
    glVertex2f((GLfloat)x1, (GLfloat)y1);
    glVertex2f((GLfloat)x2, (GLfloat)y2);
    glEnd();
}

void ClimatologyOverlayFactory::DrawArrowDC(wxDC* dc,
                                            double x1, double y1,
                                            double x2, double y2,
                                            const wxColour& c,
                                            int width)
{
    dc->SetPen(wxPen(c, width));
    dc->DrawLine(x1, y1, x2, y2);
}

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
// NUMBERS Rendering (GL + DC unified)
// ============================================================================

void ClimatologyOverlayFactory::RenderNumbers(const ClimatologyRenderParams& p)
{
    if (!IsCompletedLoading())
        return;
    if (!p.vp || !p.vp->bValid)
        return;

    const int setting = m_params.overlayType;

    if (!m_params.showNumbers[setting])
        return;

    if (p.useGL)
        RenderNumbersGL(p);
    else
        RenderNumbersDC(p);
}


// ============================================================================
// IsobarsNumbers  Rendering (DC + optional GL)
// ============================================================================

void ClimatologyOverlayFactory::RenderNumbersDC(const ClimatologyRenderParams& p)
{
    wxDC* dc = p.dc->GetDC();
    if (!dc)
        return;

    PlugIn_ViewPort& vp = *p.vp;

    double spacing = m_params.numberSpacing;
    wxColour color = m_params.color[m_params.overlayType];

    dc->SetTextForeground(color);

    const int setting = m_params.overlayType;

    // Choose correct coordinate type
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


void ClimatologyOverlayFactory::RenderNumbersGL(const ClimatologyRenderParams& p)
{
    if (!IsCompletedLoading() || !p.vp)
        return;

    PlugIn_ViewPort& vp = *p.vp;

    const int setting  = m_params.overlayType;
    const double spacing = m_params.numberSpacing;

    // Choose coordinate type
    Coord coord = (setting == OVERLAY_WIND || setting == OVERLAY_CURRENT)
                  ? MAG
                  : SCALAR;

    // Color for numbers
    wxColour color = m_params.color[setting];

    // If we have a DC, render numbers using wxDC/piDC text
	if (p.dc && p.dc->dc) {
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
// IsoBarMap creation / reuse
// ============================================================================

ClimatologyIsoBarMap*
ClimatologyOverlayFactory::GetOrCreateIsoBarMap(int overlayType,
                                                int month,
                                                double spacing,
                                                double step,
                                                int units)
{
    ClimatologyIsoBarMap*& iso = m_pIsobars[overlayType][month];

    // If exists and settings match, reuse it
    if (iso && iso->SameSettings(spacing, step, units, month, 15))
        return iso;

    // If exists but settings changed, destroy it
    if (iso)
    {
        delete iso;
        iso = nullptr;
    }

    // Create new map
    wxString name = wxString::Format("Overlay %d", overlayType);

	iso = new ClimatologyIsoBarMap(name,
								   spacing,
								   step,
								   *this,
								   overlayType,
								   units,
								   month,
								   15,
								   m_renderParams);

    // Compute the map
    if (!iso->Recompute(nullptr))
    {
        delete iso;
        iso = nullptr;
        return nullptr;
    }

    return iso;
}


// ============================================================================
// Destroy IsoBarMap (if needed)
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




//============================================================
// RENDER WIND ATLAS (WIND ROSE) WRAPPER  (GL + DC Unified)
//============================================================

void ClimatologyOverlayFactory::RenderWindAtlas(const ClimatologyRenderParams& p)
{
    if (!IsCompletedLoading())
        return;

    if (!p.vp || !p.vp->bValid)
        return;

    // Feature disabled in UI
    if (!p.windAtlas.enabled)
        return;

    // Overlay visibility (Wind Atlas overlay type)
    if (!p.showOverlayType[OVERLAY_WIND_ATLAS])
        return;

    // Dispatch to GL/DC
    if (p.useGL)
        RenderWindAtlasGL(p);
    else
        RenderWindAtlasDC(p);
}

//------------------------------------------------------------
// Helper: accumulate wind statistics into direction bins
//------------------------------------------------------------
void ClimatologyOverlayFactory::AccumulateWindBins(
    const ClimatologyRenderParams& p,
    double lat, double lon,
    int dirBins,
    std::vector<double>& freqBins,
    std::vector<double>& speedBins,
    double& calmFrac)
{
    const int samples = 8;
    int calmCount = 0;
    int totalCount = 0;

    for (int i = 0; i < samples; i++)
    {
        // Unified-grid wind components
        double u = getCurValue(U,   OVERLAY_WIND, lat, lon, p);
        double v = getCurValue(V,   OVERLAY_WIND, lat, lon, p);

        if (std::isnan(u) || std::isnan(v))
            continue;

        // NOAA convention: reverse vector
        u = -u;
        v = -v;

        double speed = std::hypot(u, v);
        if (speed <= 0.0 || std::isnan(speed))
        {
            calmCount++;
            totalCount++;
            continue;
        }

        // Direction in degrees
        double dirRad = std::atan2(u, v);
        double dirDeg = dirRad * 180.0 / M_PI;
        if (dirDeg < 0.0)
            dirDeg += 360.0;

        // Bin index
        double binWidth = 360.0 / dirBins;
        int bin = (int)std::floor(dirDeg / binWidth);
        if (bin < 0) bin = 0;
        if (bin >= dirBins) bin = dirBins - 1;

        freqBins[bin]  += 1.0;
        speedBins[bin] += speed;
        totalCount++;
    }

    if (totalCount > 0)
    {
        calmFrac = (double)calmCount / (double)totalCount;

        // Normalize bins
        for (int d = 0; d < dirBins; d++)
        {
            if (freqBins[d] > 0.0)
                speedBins[d] /= freqBins[d];   // average speed

            freqBins[d] /= (double)totalCount; // fraction 0..1
        }
    }
    else
    {
        calmFrac = 0.0;
    }
}


//------------------------------------------------------------
// RENDER WIND ATLAS GL (modern, unified)
//------------------------------------------------------------
void ClimatologyOverlayFactory::RenderWindAtlasGL(const ClimatologyRenderParams& p)
{
    if (!IsCompletedLoading())
        return;
    if (!p.vp || !p.vp->bValid)
        return;
    if (!m_params.showOverlayType[OVERLAY_WIND_ATLAS])
        return;

    PlugIn_ViewPort& vp = *p.vp;

    const int dirBins = 16;          // 16‑direction rose
    double spacing    = m_params.windAtlas.spacing;
    double size       = m_params.windAtlas.size;
    float  alpha      = (float)m_params.windAtlas.opacity;
    const double r    = 12.0;

    // Use spacing as the atlas sampling step
    double step = spacing;
    while ((vp.lat_max - vp.lat_min) / step > vp.pix_height / spacing ||
           (vp.lon_max - vp.lon_min) / step > vp.pix_width  / spacing)
    {
        step *= 2.0;
    }

    wxColour c = m_params.windAtlas.color;

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glLineWidth(2.0f);

    for (double lat = std::round(vp.lat_min / step) * step - 1.0;
         lat <= vp.lat_max + 1.0;
         lat += step)
    {
        for (double lon = std::round(vp.lon_min / step) * step - 1.0;
             lon <= vp.lon_max + 1.0;
             lon += step)
        {
            std::vector<double> freqBins(dirBins, 0.0);
            std::vector<double> speedBins(dirBins, 0.0);
            double calmFrac = 0.0;

            AccumulateWindBins(p, lat, lon, dirBins,
                               freqBins, speedBins, calmFrac);

            double totalFreq = 0.0;
            for (double f : freqBins) totalFreq += f;
            if (totalFreq <= 0.0 && calmFrac <= 0.0)
                continue;

            wxPoint p1;
            GetCanvasPixLL(&vp, &p1, lat, lon);

            glColor4f(c.Red()/255.0f,
                      c.Green()/255.0f,
                      c.Blue()/255.0f,
                      alpha);

            // Base circle
            glBegin(GL_LINE_LOOP);
            for (int i = 0; i < 64; i++)
            {
                double theta = 2.0 * M_PI * i / 64.0;
                double x = p1.x + r * std::sin(theta);
                double y = p1.y - r * std::cos(theta);
                glVertex2f((GLfloat)x, (GLfloat)y);
            }
            glEnd();

            // Center mode: filled circle only (no text in GL path)
            if (m_params.windAtlas.centerMode != 0)
            {
                double avg = 0.0, maxv = 0.0, minv = 1e9;
                int count = 0;

                for (int d = 0; d < dirBins; d++)
                {
                    double f = freqBins[d];
                    double v = speedBins[d];
                    if (f <= 0.0)
                        continue;
                    avg += v;
                    maxv = std::max(maxv, v);
                    minv = std::min(minv, v);
                    count++;
                }

                if (count > 0)
                    avg /= count;

                double centerValue = 0.0;
                switch (m_params.windAtlas.centerMode)
                {
                case 1: centerValue = avg;  break;
                case 2: centerValue = maxv; break;
                case 3: centerValue = minv; break;
                }

                float cr = 4.0f * m_params.windAtlas.numberSize;

                glColor4f(c.Red()/255.0f,
                          c.Green()/255.0f,
                          c.Blue()/255.0f,
                          alpha);

                glBegin(GL_TRIANGLE_FAN);
                glVertex2f((GLfloat)p1.x, (GLfloat)p1.y);
                for (int i = 0; i <= 64; i++)
                {
                    double theta = 2.0 * M_PI * i / 64.0;
                    float x = p1.x + cr * std::sin(theta);
                    float y = p1.y - cr * std::cos(theta);
                    glVertex2f(x, y);
                }
                glEnd();

                // numeric center label is DC‑only; GL path skips text
                (void)centerValue;
            }

            // Calm / frequency text: GL path skips text, DC path handles it
            if (m_params.windAtlas.showCalm || m_params.windAtlas.showFreq)
            {
                double calmPct = calmFrac * 100.0;

                int active = 0;
                for (int d = 0; d < dirBins; d++)
                    if (freqBins[d] > 0.0)
                        active++;

                double freqPct = (active * 100.0) / dirBins;

                (void)calmPct;
                (void)freqPct;
            }

            // Direction segments + barbs
            for (int d = 0; d < dirBins; d++)
            {
                double freq  = freqBins[d];
                double speed = speedBins[d];

                if (freq <= 0.0)
                    continue;

                double theta = 2.0 * M_PI * d / dirBins + vp.rotation;

                // Label helper (GL/DC‑agnostic)
                DrawWindAtlasLabelGLorDC(p1, r, d, dirBins, freq,
                                         vp.rotation, p);

                double x1 = p1.x + r * std::sin(theta);
                double y1 = p1.y - r * std::cos(theta);

                const double maxdirection = 0.29;
                bool split = freq > maxdirection;

                double u = r + size * (split ? maxdirection : freq);

                double x2 = p1.x + u * std::sin(theta);
                double y2 = p1.y - u * std::cos(theta);

                // Segment helper (GL/DC‑agnostic)
                DrawSegmentGLorDC(x1, y1, x2, y2,
                                  m_params.windAtlas.color, p);

                // Barb helper (GL/DC‑agnostic)
				DrawNOAABarbsGLorDC(x2, y2, theta, speed,
									m_params.windAtlas.numberSize,
									p);
            }
        }
    }

    glLineWidth(1.0f);
    glDisable(GL_BLEND);
}

//------------------------------------------------------------
// RENDER WIND ATLAS DC (modern, unified)
//------------------------------------------------------------
void ClimatologyOverlayFactory::RenderWindAtlasDC(const ClimatologyRenderParams& p)
{
    if (!IsCompletedLoading())
        return;
    if (!p.dc->GetDC() || !p.vp || !p.vp->bValid)
        return;
    if (!m_params.showOverlayType[OVERLAY_WIND_ATLAS])
        return;

    PlugIn_ViewPort& vp = *p.vp;
    wxDC* dc = p.dc->GetDC();
    wxColour c = m_params.windAtlas.color;

    const int dirBins = 16;
    const double r    = 12.0;

    // spacing from unified params
    double spacing = m_params.windAtlas.spacing;

    // dynamic sampling step (lat/lon grid spacing)
    double step = spacing;
    while ((vp.lat_max - vp.lat_min) / step > vp.pix_height / spacing ||
           (vp.lon_max - vp.lon_min) / step > vp.pix_width  / spacing)
    {
        step *= 2.0;
    }

    for (double lat = round(vp.lat_min / step) * step - 1.0;
         lat <= vp.lat_max + 1.0;
         lat += step)
    {
        for (double lon = round(vp.lon_min / step) * step - 1.0;
             lon <= vp.lon_max + 1.0;
             lon += step)
        {
            // unified wind bins
            std::vector<double> freqBins(dirBins, 0.0);
            std::vector<double> speedBins(dirBins, 0.0);
            double calmFrac = 0.0;

            AccumulateWindBins(p, lat, lon, dirBins,
                               freqBins, speedBins, calmFrac);

            double totalFreq = 0.0;
            for (double f : freqBins) totalFreq += f;

            if (totalFreq <= 0.0 && calmFrac <= 0.0)
                continue;

            wxPoint p1;
            GetCanvasPixLL(&vp, &p1, lat, lon);

            dc->SetPen(wxPen(c, 1));
            dc->SetBrush(*wxTRANSPARENT_BRUSH);
            dc->DrawCircle(p1.x, p1.y, r);

            // center-mode numeric label
            if (m_params.windAtlas.centerMode != 0)
            {
                double avg = 0.0, maxv = 0.0, minv = 1e9;
                int count = 0;

                for (int d = 0; d < dirBins; d++)
                {
                    double f = freqBins[d];
                    double v = speedBins[d];
                    if (f <= 0.0)
                        continue;

                    avg += v;
                    maxv = std::max(maxv, v);
                    minv = std::min(minv, v);
                    count++;
                }

                if (count > 0)
                    avg /= count;

                double centerValue = 0.0;
                switch (m_params.windAtlas.centerMode)
                {
                case 1: centerValue = avg;  break;
                case 2: centerValue = maxv; break;
                case 3: centerValue = minv; break;
                }

                float cr = 4.0f * m_params.windAtlas.numberSize;

                dc->SetBrush(wxBrush(c));
                dc->DrawCircle(p1.x, p1.y, cr);

                wxString txt = wxString::Format("%.0f", 100.0 * centerValue);
                dc->DrawText(txt, p1.x - cr/2, p1.y + cr/2);
            }

            // calm/frequency text
            if (m_params.windAtlas.showCalm || m_params.windAtlas.showFreq)
            {
                double calmPct = calmFrac * 100.0;

                int active = 0;
                for (int d = 0; d < dirBins; d++)
                    if (freqBins[d] > 0.0)
                        active++;

                double freqPct = (active * 100.0) / dirBins;

                wxString txt;
                if (m_params.windAtlas.showCalm)
                    txt = wxString::Format("C%.0f", calmPct);
                else
                    txt = wxString::Format("F%.0f", freqPct);

                dc->DrawText(txt, p1.x, p1.y - (r + 10.0));
            }

            // segments + barbs
            for (int d = 0; d < dirBins; d++)
            {
                double freq  = freqBins[d];
                double speed = speedBins[d];

                if (freq <= 0.0)
                    continue;

                double theta = 2.0 * M_PI * d / dirBins + vp.rotation;

                double x1 = p1.x + r * sin(theta);
                double y1 = p1.y - r * cos(theta);

                const double maxdirection = 0.29;
                bool split = freq > maxdirection;

                double u = r + m_params.windAtlas.size *
                                (split ? maxdirection : freq);

                double x2 = p1.x + u * sin(theta);
                double y2 = p1.y - u * cos(theta);

                // draw segment
                dc->DrawLine(x1, y1, x2, y2);

                // draw barbs
				DrawNOAABarbsGLorDC(x2, y2, theta, speed,
									m_params.windAtlas.numberSize,
									p);
            }
        }
    }
}



//------------------------------------------------------------
// DRAW WIND ATLAS -Shared helpers (already used by both GL/DC paths)
//------------------------------------------------------------

// Draw Wind Atlas Label GL or DC
void ClimatologyOverlayFactory::DrawWindAtlasLabelGLorDC(
    const wxPoint& p1,
    double r,
    int d,
    int dir_cnt,
    double freq,
    double rotation,
    const ClimatologyRenderParams& p)
{
    double theta = 2.0 * M_PI * d / dir_cnt + rotation;

    double x = p1.x + (r * 0.5) * std::sin(theta);
    double y = p1.y - (r * 0.5) * std::cos(theta);

    wxString txt = wxString::Format("%.0f", freq * 100.0);

    // GL path: skip text entirely (OpenCPN does not support GL text)
    if (p.useGL)
        return;

    // DC path: draw text normally
    if (p.dc->GetDC())
        p.dc->GetDC()->DrawText(txt, x, y);
}


// Draw Segment GL or DC
void ClimatologyOverlayFactory::DrawSegmentGLorDC(
    double x1, double y1,
    double x2, double y2,
    const wxColour& c,
    const ClimatologyRenderParams& p)
{
    if (p.useGL)
    {
        glColor4f(c.Red()/255.0f,
                  c.Green()/255.0f,
                  c.Blue()/255.0f,
                  m_params.windAtlas.opacity);

        glBegin(GL_LINES);
        glVertex2f((GLfloat)x1, (GLfloat)y1);
        glVertex2f((GLfloat)x2, (GLfloat)y2);
        glEnd();
    }
    else if (p.dc->GetDC())
    {
        wxDC* dc = p.dc->GetDC();
        dc->SetPen(wxPen(c, 1));
        dc->DrawLine(x1, y1, x2, y2);
    }
}



// ----------------------------------------------------------
// Draw NOAA Barbs GL or DC
// ----------------------------------------------------------
void ClimatologyOverlayFactory::DrawNOAABarbsGLorDC(
    double x2, double y2,
    double theta,
    double speed,
    double scale,
    const ClimatologyRenderParams& p)
{
    double windKt = speed;   // already in unified units (knots or m/s)

    float barbLen    = 10.0f * scale;
    float pennantLen = 14.0f * scale;
    float spacing    = 4.0f  * scale;

    float bx = (float)x2;
    float by = (float)y2;

    auto drawLine = [&](float x1, float y1, float x2, float y2)
    {
        if (p.useGL)
        {
            glBegin(GL_LINES);
            glVertex2f(x1, y1);
            glVertex2f(x2, y2);
            glEnd();
        }
        else if (p.dc->GetDC())
        {
            p.dc->GetDC()->DrawLine(x1, y1, x2, y2);
        }
    };

    // ------------------------------------------------------------
    // Pennants (50 kt)
    // ------------------------------------------------------------
    while (windKt >= 50.0)
    {
        float px = bx - pennantLen * std::sin(theta);
        float py = by + pennantLen * std::cos(theta);

        if (p.useGL)
        {
            glBegin(GL_TRIANGLES);
            glVertex2f(bx, by);
            glVertex2f(px, py);
            glVertex2f(bx - spacing * std::sin(theta),
                       by + spacing * std::cos(theta));
            glEnd();
        }
        else if (p.dc->GetDC())
        {
            wxPoint p1(bx, by);
            wxPoint p2(px, py);
            wxPoint p3(bx - spacing * std::sin(theta),
                       by + spacing * std::cos(theta));

            wxPoint tri[3] = { p1, p2, p3 };
            p.dc->GetDC()->DrawPolygon(3, tri);
        }

        bx -= spacing * std::sin(theta);
        by += spacing * std::cos(theta);

        windKt -= 50.0;
    }

    // ------------------------------------------------------------
    // Full barbs (10 kt)
    // ------------------------------------------------------------
    while (windKt >= 10.0)
    {
        float fx = bx - barbLen * std::sin(theta + M_PI/3);
        float fy = by + barbLen * std::cos(theta + M_PI/3);

        drawLine(bx, by, fx, fy);

        bx -= spacing * std::sin(theta);
        by += spacing * std::cos(theta);

        windKt -= 10.0;
    }

    // ------------------------------------------------------------
    // Half barb (5 kt)
    // ------------------------------------------------------------
    if (windKt >= 5.0)
    {
        float hx = bx - (barbLen * 0.5f) * std::sin(theta + M_PI/3);
        float hy = by + (barbLen * 0.5f) * std::cos(theta + M_PI/3);

        drawLine(bx, by, hx, hy);
    }
}


// ============================================================================
// Cyclones (GL + DC)
// ============================================================================

// Helper: 

ENSOPeriod ClimatologyOverlayFactory::ENSOFromDate(int year, int month)
{
    auto itYear = m_ensoData.find(year);
    if (itYear == m_ensoData.end())
        return ENSOPeriod::NA;

    auto itMonth = itYear->second.find(month);
    if (itMonth == itYear->second.end())
        return ENSOPeriod::NA;

    return itMonth->second;
}



bool ClimatologyOverlayFactory::LoadENSODataFromCSV(const wxString& filename)
{
    wxTextFile f(filename);
    if (!f.Open())
        return false;

    m_ensoData.clear();

    for (wxString line = f.GetFirstLine(); !f.Eof(); line = f.GetNextLine())
    {
        if (line.IsEmpty())
            continue;

        wxStringTokenizer tok(line, ", \t");
        if (tok.CountTokens() < 3)
            continue;

        long year, month;
        double value;

        tok.GetNextToken().ToLong(&year);
        tok.GetNextToken().ToLong(&month);
        tok.GetNextToken().ToDouble(&value);

        // Convert numeric ENSO index to ENSOPeriod
        ENSOPeriod p;
        if (value >= 0.5)
            p = ENSOPeriod::EL_NINO;
        else if (value <= -0.5)
            p = ENSOPeriod::LA_NINA;
        else
            p = ENSOPeriod::NEUTRAL;

        m_ensoData[year][month] = p;
    }

    return !m_ensoData.empty();
}



// ------------------------------------------------------------
// Cyclone Filters 
// ------------------------------------------------------------

bool ClimatologyOverlayFactory::PassesCycloneFilters(
    const Cyclone& cyc,
    const CyclonePoint& pt,
    const ClimatologyRenderParams& p) const
{
    const CycloneFilterParams& f = m_cycloneParams;

    // ------------------------------------------------------------
    // ENSO filter
    // ------------------------------------------------------------
    switch (cyc.enso)
    {
        case ENSOPeriod::EL_NINO:
            if (!f.allowElNino) return false;
            break;

        case ENSOPeriod::LA_NINA:
            if (!f.allowLaNina) return false;
            break;

        case ENSOPeriod::NEUTRAL:
            if (!f.allowNeutral) return false;
            break;

        case ENSOPeriod::NA:
            if (!f.allowNotAvailable) return false;
            break;
    }

    // ------------------------------------------------------------
    // Basin filter (unified CycloneBasin enum)
    // ------------------------------------------------------------
    switch (cyc.basin)
    {
        case CycloneBasin::EPA: if (!p.showEPA) return false; break;
        case CycloneBasin::WPA: if (!p.showWPA) return false; break;
        case CycloneBasin::SPA: if (!p.showSPA) return false; break;
        case CycloneBasin::ATL: if (!p.showATL) return false; break;
        case CycloneBasin::NIO: if (!p.showNIO) return false; break;
        case CycloneBasin::SHE: if (!p.showSHE) return false; break;
        default: return false;
    }

    // ------------------------------------------------------------
    // Storm type mask
    // ------------------------------------------------------------
    if (!(f.statemask & (1 << static_cast<int>(pt.state))))
        return false;

    // ------------------------------------------------------------
    // Intensity filters
    // ------------------------------------------------------------
    if (pt.windspeed < f.minwindspeed)
        return false;

    if (pt.pressure > f.maxpressure)
        return false;

    // ------------------------------------------------------------
    // Date range filter
    // ------------------------------------------------------------
    wxDateTime dt(pt.day, (wxDateTime::Month)(pt.month - 1), pt.year);

    if (dt.IsValid())
    {
        if (dt < f.startDate || dt > f.endDate)
            return false;
    }

    return true;
}


// ------------------------------------------------------------
// RENDER CYCLONES GL 
// ------------------------------------------------------------

void ClimatologyOverlayFactory::RenderCyclonesGL(const ClimatologyRenderParams& p)
{
    if (!IsCompletedLoading() || !m_hasCycloneData || !p.showCyclones)
        return;

    const PlugIn_ViewPort& vp = *p.vp;

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glLineWidth(2.0f);

	for (const Cyclone& cyc : m_cyclones)
    {
        const auto& pts = track.points;
        if (pts.size() < 2)
            continue;

        for (size_t i = 1; i < pts.size(); i++)
        {
            const CyclonePoint& a = pts[i - 1];
            const CyclonePoint& b = pts[i];

			if (!PassesCycloneFilters(track, a, p) ||
				!PassesCycloneFilters(track, b, p))
				continue;


            double ax, ay, bx, by;
			LatLonToPixel(vp, a.lat, a.lon, ax, ay);
			LatLonToPixel(vp, b.lat, b.lon, bx, by);

            wxColour c = MapCyclonePointColor(a, p);

            glColor4ub(c.Red(), c.Green(), c.Blue(),
                       (unsigned char)(p.cycloneOpacity * 255.0));

            // Segment
            glBegin(GL_LINES);
            glVertex2f((GLfloat)ax, (GLfloat)ay);
            glVertex2f((GLfloat)bx, (GLfloat)by);
            glEnd();

            // Arrowhead
            double mx = (ax + bx) * 0.5;
            double my = (ay + by) * 0.5;
            double dx = bx - ax;
            double dy = by - ay;

            double scale = 0.2;
            double lx = mx + ( dx + dy) * scale;
            double ly = my + ( dy - dx) * scale;
            double rx = mx + ( dx - dy) * scale;
            double ry = my + ( dy + dx) * scale;

            glBegin(GL_LINES);
            glVertex2f((GLfloat)mx, (GLfloat)my);
            glVertex2f((GLfloat)lx, (GLfloat)ly);
            glVertex2f((GLfloat)mx, (GLfloat)my);
            glVertex2f((GLfloat)rx, (GLfloat)ry);
            glEnd();
        }
    }

    glLineWidth(1.0f);
    glDisable(GL_BLEND);
}

// ------------------------------------------------------------
// RENDER CYCLONES (DC)
// ------------------------------------------------------------
void ClimatologyOverlayFactory::RenderCyclonesDC(const ClimatologyRenderParams& p)
{
    if (!IsCompletedLoading() || !m_hasCycloneData || !p.showCyclones)
        return;
    if (!p.dc->GetDC() || !p.vp)
        return;

    wxDC* dc = p.dc->GetDC();
    const PlugIn_ViewPort& vp = *p.vp;

    for (const Cyclone& cyc : m_cyclones)
    {
        const auto& pts = cyc.points;
        if (pts.size() < 2)
            continue;

        for (size_t i = 1; i < pts.size(); i++)
        {
            const CyclonePoint& a = pts[i - 1];
            const CyclonePoint& b = pts[i];

            // Unified filtering (correct signature)
            if (!PassesCycloneFilters(cyc, a, p) ||
                !PassesCycloneFilters(cyc, b, p))
                continue;

            // Convert lat/lon → pixel
            wxPoint pa, pb;
            LatLonToPixel(vp, a.lat, a.lon, pa);
            LatLonToPixel(vp, b.lat, b.lon, pb);

            // Unified color mapping (ENSO + intensity + basin)
            wxColour c = MapCyclonePointColor(b, p);
            dc->SetPen(wxPen(c, 1));

            // Draw segment
            dc->DrawLine(pa.x, pa.y, pb.x, pb.y);

            // Arrowhead
            double mx = (pa.x + pb.x) * 0.5;
            double my = (pa.y + pb.y) * 0.5;
            double dx = pb.x - pa.x;
            double dy = pb.y - pa.y;

            double scale = 0.2;
            double lx = mx + ( dx + dy) * scale;
            double ly = my + ( dy - dx) * scale;
            double rx = mx + ( dx - dy) * scale;
            double ry = my + ( dy + dx) * scale;

            dc->DrawLine(mx, my, lx, ly);
            dc->DrawLine(mx, my, rx, ry);
        }
    }
}



// ====================================================
// Cyclone Segment Renderer - Shared (DC and GL)
// ====================================================
void ClimatologyOverlayFactory::RenderCycloneSegment(
    const Cyclone& cyc,
    const CyclonePoint& a,
    const CyclonePoint& b,
    const ClimatologyRenderParams& p,
    const PlugIn_ViewPort& vp,
    wxDC* dc)
{
    // Unified cyclone filtering
    if (!PassesCycloneFilters(cyc, a, p) ||
        !PassesCycloneFilters(cyc, b, p))
        return;

    // Project to screen
    double ax, ay, bx, by;
    LatLonToPixel(vp, a.lat, a.lon, ax, ay);
    LatLonToPixel(vp, b.lat, b.lon, bx, by);

    // Unified cyclone color
    wxColour c = MapCyclonePointColor(b, p);

    // Main segment
    if (p.useGL)
    {
        glColor4ub(c.Red(), c.Green(), c.Blue(),
                   (unsigned char)(p.cycloneOpacity * 255.0));

        glBegin(GL_LINES);
        glVertex2f(ax, ay);
        glVertex2f(bx, by);
        glEnd();
    }
    else if (dc)
    {
        dc->SetPen(wxPen(c, 1));
        dc->DrawLine(ax, ay, bx, by);
    }

    // Arrowhead geometry
    const double mx = (ax + bx) * 0.5;
    const double my = (ay + by) * 0.5;
    const double dx = bx - ax;
    const double dy = by - ay;

    const double scale = 0.20; // unified arrow scale

    const double lx = mx + ( dx + dy) * scale;
    const double ly = my + ( dy - dx) * scale;
    const double rx = mx + ( dx - dy) * scale;
    const double ry = my + ( dy + dx) * scale;

    // Arrowhead render
    if (p.useGL)
    {
        glBegin(GL_LINES);
        glVertex2f(mx, my);
        glVertex2f(lx, ly);
        glVertex2f(mx, my);
        glVertex2f(rx, ry);
        glEnd();
    }
    else if (dc)
    {
        dc->DrawLine(mx, my, lx, ly);
        dc->DrawLine(mx, my, rx, ry);
    }
}



// ------------------------------------------------------------
// Cyclone helpers 
// ------------------------------------------------------------
// LatLonToPixel    GetCyclonePointsInSpan
// CountCycloneTrackCrossings    DoesCycloneTrackCrossRoute
// MapCyclonePointColor


wxColour ClimatologyOverlayFactory::MapCyclonePointColor(
        const CyclonePoint& pt,
        const ClimatologyRenderParams& p) const
{
    // 1. ENSO-based coloring (if enabled)
    if (p.useENSOColoring)
    {
        switch (pt.enso)
        {
            case ENSOPeriod::EL_NINO:   return wxColour(255, 80, 80);   // red
            case ENSOPeriod::LA_NINA:   return wxColour(80, 80, 255);   // blue
            case ENSOPeriod::NEUTRAL:   return wxColour(80, 255, 80);   // green
            default:                    return wxColour(180, 180, 180); // gray
        }
    }

    // 2. Basin-based coloring (if enabled)
    if (p.useBasinColoring)
    {
        switch (pt.basin)
        {
            case CycloneBasin::EPA: return wxColour(255, 160, 160);
            case CycloneBasin::WPA: return wxColour(160, 160, 255);
            case CycloneBasin::SPA: return wxColour(160, 255, 160);
            case CycloneBasin::ATL: return wxColour(255, 200, 80);
            case CycloneBasin::NIO: return wxColour(200, 80, 255);
            case CycloneBasin::SHE: return wxColour(80, 200, 200);
            default:                return wxColour(180, 180, 180);
        }
    }

    // 3. Wind-intensity coloring (fallback)
    double w = pt.maxWind;

    if (w < 20)  return wxColour(180, 180, 180); // tropical disturbance
    if (w < 34)  return wxColour(120, 200, 120); // tropical depression
    if (w < 64)  return wxColour(255, 200, 80);  // tropical storm
    if (w < 83)  return wxColour(255, 160, 80);  // Cat 1
    if (w < 96)  return wxColour(255, 120, 80);  // Cat 2
    if (w < 113) return wxColour(255, 80, 80);   // Cat 3
    if (w < 137) return wxColour(200, 40, 40);   // Cat 4

    return wxColour(160, 0, 0);                  // Cat 5+
}


// =================================
// CyclonePoint   
// ================================

std::vector<const CyclonePoint*>
ClimatologyOverlayFactory::GetCyclonePointsInSpan(
        const ClimatologyRenderParams& p,
        int /*centerDay*/,     // no longer used
        double lat,
        double lon)
{
    std::vector<const CyclonePoint*> out;

    if (!m_hasCycloneData)
        return out;

    // Unified cyclone storage: six basin vectors of Cyclone
    const std::vector<std::vector<Cyclone>*> basins = {
        &m_epa,
        &m_wpa,
        &m_spa,
        &m_atl,
        &m_nio,
        &m_she
    };

    for (const auto* basin : basins)
    {
        for (const Cyclone& cyc : *basin)
        {
            for (const CyclonePoint& pt : cyc.points)
            {
                if (!PassesCycloneFilters(cyc, pt, p))
                    continue;

                out.push_back(&pt);
            }
        }
    }

    return out;
}


// =======================================================
// DoesCycloneTrackCrossRoute
// =======================================================

bool ClimatologyOverlayFactory::DoesCycloneTrackCrossRoute(
        const Cyclone& track,
        double lat1, double lon1,
        double lat2, double lon2,
        const ClimatologyRenderParams& p)
{
    const auto& pts = track.points;
    if (pts.size() < 2)
        return false;

    auto segIntersects = [](double ax, double ay,
                            double bx, double by,
                            double cx, double cy,
                            double dx, double dy) -> bool
    {
        auto cross = [](double x1,double y1,double x2,double y2) {
            return x1*y2 - y1*x2;
        };

        double r1 = cross(bx-ax, by-ay, cx-ax, cy-ay);
        double r2 = cross(bx-ax, by-ay, dx-ax, dy-ay);
        double r3 = cross(dx-cx, dy-cy, ax-cx, ay-cy);
        double r4 = cross(dx-cx, dy-cy, bx-cx, by-cy);

        return (r1*r2 < 0 && r3*r4 < 0);
    };

    for (size_t i = 1; i < pts.size(); i++)
    {
        const CyclonePoint& a = pts[i-1];
        const CyclonePoint& b = pts[i];

        if (!PassesCycloneFilters(track, a, p) ||
            !PassesCycloneFilters(track, b, p))
            continue;

        double ax = a.lat;
        double ay = a.lon;
        double bx = b.lat;
        double by = b.lon;

        if (segIntersects(ax, ay, bx, by, lat1, lon1, lat2, lon2))
            return true;
    }

    return false;
}


// CountCycloneTrackCrossings
int ClimatologyOverlayFactory::CountCycloneTrackCrossings(
        double lat1, double lon1,
        double lat2, double lon2,
        const ClimatologyRenderParams& p)
{
    if (!m_hasCycloneData)
        return 0;

    int crossings = 0;

	for (const Cyclone& cyc : m_cyclones)
	{
		if (DoesCycloneTrackCrossRoute(cyc, lat1, lon1, lat2, lon2, p))
			crossings++;
	}

    return crossings;
}

// =========================================================
// READ CYCLONE DATA
// ===========================================================

// Helper: map filename to basinId
static CycloneBasin BasinFromFilename(const wxString& filename)
{
    wxString name = wxFileName(filename).GetName().Lower();

    if (name.Contains("epa")) return CycloneBasin::EPA;
    if (name.Contains("wpa")) return CycloneBasin::WPA;
    if (name.Contains("spa")) return CycloneBasin::SPA;
    if (name.Contains("atl")) return CycloneBasin::ATL;
    if (name.Contains("nio")) return CycloneBasin::NIO;
    if (name.Contains("she")) return CycloneBasin::SHE;

    return CycloneBasin::UNKNOWN;
}


// Helper: compute day-of-year (simple, wxDateTime-based)
static int DayOfYear(int year, int month, int day)
{
    wxDateTime dt(day, (wxDateTime::Month)(month - 1), year);
    return dt.IsValid() ? dt.GetDayOfYear() + 1 : 0;
}


// Helper: map some legacy state code to CycloneState
static CycloneState CycloneStateFromCode(int code)
{
    switch (code)
    {
        case 0:  return CycloneState::TROPICAL;
        case 1:  return CycloneState::SUBTROPICAL;
        case 2:  return CycloneState::EXTRATROPICAL;
        case 3:  return CycloneState::WAVE;
        case 4:  return CycloneState::REMANENT;
        default: return CycloneState::UNKNOWN;
    }
}


// Helper: point-level ENSO classification (you can refine this later)
static CycloneENSO PointENSOFromPeriod(ENSOPeriod p)
{
    switch (p)
    {
    case ENSOPeriod::EL_NINO:  return ElNino;
    case ENSOPeriod::LA_NINA:  return LaNina;
    case ENSOPeriod::NEUTRAL:  return Neutral;
    default:                   return Neutral;
    }
}


// Helper: track-level ENSO from year (placeholder; you can wire to ENSO CSV)
static ENSOPeriod ENSOFromYear(int year)
{
    // TODO: integrate with m_ensoData / LoadENSODataFromCSV
    // For now, treat everything as NEUTRAL.
    return ENSOPeriod::NEUTRAL;
}

					 
// Helper:  ClimatologyOverlayFactory::ENSOFromDate  for LoadCycloneDataBackground
ENSOPeriod ClimatologyOverlayFactory::ENSOFromDate(int year, int month)
{
    // Example structure: m_ensoData[year][month] = ENSOPeriod::EL_NINO
    auto itYear = m_ensoData.find(year);
    if (itYear == m_ensoData.end())
        return ENSOPeriod::NA;

    auto itMonth = itYear->second.find(month);
    if (itMonth == itYear->second.end())
        return ENSOPeriod::NA;

    return itMonth->second;
}
					 
//  =======================================================
//  READ CYCLONE DATA
//  =====================================================
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

    // Map from track ID → index in basinTracks
    std::map<int, size_t> trackIndexById;

    // Unified basin enum
    CycloneBasin basin = BasinFromFilename(filename);

    for (wxString line = file.GetFirstLine(); !file.Eof(); line = file.GetNextLine())
    {
        if (line.IsEmpty())
            continue;

        // Expected format:
        // id year month day lat lon wind pressure stateCode
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

        // ENSO lookup (track-level)
        ENSOPeriod trackENSO = ENSOFromDate(year, month);

        // Build point
        CyclonePoint pt;
        pt.lat       = lat;
        pt.lon       = lon;
        pt.year      = year;
        pt.month     = month;
        pt.day       = day;
        pt.dayOfYear = DayOfYear(pt.year, pt.month, pt.day);

        // FIX: use stateCode (not rawStateCode)
        pt.state = CycloneStateFromCode(stateCode);

        // Intensity normalization
        pt.windspeed = NormalizeWind(wind, basin);
        pt.pressure  = NormalizePressure(pressure);
        pt.maxWind   = pt.windspeed;

        // ENSO + basin assignment
        pt.enso  = PointENSOFromPeriod(trackENSO);
        pt.basin = basin;

        // Insert into track
        auto it = trackIndexById.find(id);
        if (it == trackIndexById.end())
        {
            Cyclone cyc;
            cyc.basin = basin;          // FIX: use unified basin enum
            cyc.enso  = trackENSO;
            cyc.points.push_back(pt);

            basinTracks.push_back(cyc);
            trackIndexById[id] = basinTracks.size() - 1;
        }
        else
        {
            Cyclone& cyc = basinTracks[it->second];
            cyc.points.push_back(pt);
        }
    }

    file.Close();
    return !basinTracks.empty();
}



// ------------------------------------------------------------
// LOAD CYCLONE DATA BACKGROUND - Basin Loading and Data
// ------------------------------------------------------------
//  Data Ingestion - Independent of CycloneFilterParams, unified model, or GL/DC


// Helper: for ClimatologyOverlayFactory::LoadCycloneDataBackground
bool ReadCycloneData(const wxString& filename,
                     std::vector<Cyclone>& basinTracks,
                     bool southernHemisphere);
					 


void ClimatologyOverlayFactory::LoadCycloneDataBackground(
    std::function<void(const wxString&)> sendProgress)
{
    bool allcyclone = true;

    m_epa.clear();
    m_wpa.clear();
    m_spa.clear();
    m_atl.clear();
    m_nio.clear();
    m_she.clear();

    sendProgress("Loading cyclone basin: East Pacific…");
    if (!ReadCycloneData("cyclone-epa", m_epa, false))
        allcyclone = false;

    sendProgress("Loading cyclone basin: West Pacific…");
    if (!ReadCycloneData("cyclone-wpa", m_wpa, false))
        allcyclone = false;

    sendProgress("Loading cyclone basin: South Pacific…");
    if (!ReadCycloneData("cyclone-spa", m_spa, true))
        allcyclone = false;

    sendProgress("Loading cyclone basin: Atlantic…");
    if (!ReadCycloneData("cyclone-atl", m_atl, false))
        allcyclone = false;

    sendProgress("Loading cyclone basin: North Indian…");
    if (!ReadCycloneData("cyclone-nio", m_nio, false))
        allcyclone = false;

    sendProgress("Loading cyclone basin: South Indian…");
    if (!ReadCycloneData("cyclone-she", m_she, true))
        allcyclone = false;

    sendProgress("Loading ENSO dataset…");
    m_hasENSOData = LoadENSODataFromCSV("enso.csv");

    if (!m_hasENSOData)
        wxLogWarning("Climatology: ENSO dataset unavailable.");

    m_hasCycloneData = allcyclone;

    if (!m_hasCycloneData)
        wxLogWarning("Climatology: Cyclone data unavailable.");
	
	// Merge unified basins into combined cyclone list
	m_cyclones.clear();

	m_cyclones.insert(m_cyclones.end(), m_epa.begin(), m_epa.end());
	m_cyclones.insert(m_cyclones.end(), m_wpa.begin(), m_wpa.end());
	m_cyclones.insert(m_cyclones.end(), m_spa.begin(), m_spa.end());
	m_cyclones.insert(m_cyclones.end(), m_atl.begin(), m_atl.end());
	m_cyclones.insert(m_cyclones.end(), m_nio.begin(), m_nio.end());
	m_cyclones.insert(m_cyclones.end(), m_she.begin(), m_she.end());
}


// ============================================================================
// Coordinate Transforms
// ============================================================================

void ClimatologyOverlayFactory::LatLonToPixel(const PlugIn_ViewPort& vp,
                                              double lat,
                                              double lon,
                                              wxPoint& r) const
{
    GetCanvasPixLL(&vp, &r, lat, lon);
}

void ClimatologyOverlayFactory::LatLonToPixel(const PlugIn_ViewPort& vp,
                                              double lat,
                                              double lon,
                                              double& x,
                                              double& y) const
{
    // Clamp latitude to avoid Mercator singularities
    if (lat >  85.0) lat =  85.0;
    if (lat < -85.0) lat = -85.0;

    double lonNorm = lon;
    if (vp.lon_max - vp.lon_min > 180.0)
    {
        if (lon < vp.lon_min)
            lonNorm += 360.0;
        else if (lon > vp.lon_max)
            lonNorm -= 360.0;
    }

    double latRad = lat * M_PI / 180.0;
    double yMerc = log(tan(M_PI/4.0 + latRad/2.0));

    double vpLatRadMin = vp.lat_min * M_PI / 180.0;
    double vpLatRadMax = vp.lat_max * M_PI / 180.0;

    double yMercMin = log(tan(M_PI/4.0 + vpLatRadMin/2.0));
    double yMercMax = log(tan(M_PI/4.0 + vpLatRadMax/2.0));

    double nx = (lonNorm - vp.lon_min) / (vp.lon_max - vp.lon_min);
    double ny = (yMerc - yMercMin) / (yMercMax - yMercMin);

    x = vp.pix_width  * nx;
    y = vp.pix_height * (1.0 - ny);
}


// ============================================================================
// Color Mapping
// ============================================================================

void ClimatologyOverlayFactory::ColorMap(int setting, double v,
                                         unsigned char& r,
                                         unsigned char& g,
                                         unsigned char& b)
{
    if (std::isnan(v)) {
        r = g = b = 0;
        return;
    }

    // Normalize 0–1
    double t = std::clamp(v, 0.0, 1.0);

    // Simple blue→red gradient
    r = (unsigned char)(255 * t);
    g = 0;
    b = (unsigned char)(255 * (1.0 - t));
}



