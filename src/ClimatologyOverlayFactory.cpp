/******************************************************************************
 *
 * Project:  OpenCPN
 * Purpose:  Climatology Plugin
 * Author:   Sean D'Epagnier
 *
 ***************************************************************************
 *   Copyright (C) 2018 by Sean D'Epagnier                                 *
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 3 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 *   This program is distributed in the hope that it will be useful,       *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 *   GNU General Public License for more details.                          *
 *                                                                         *
 *   You should have received a copy of the GNU General Public License     *
 *   along with this program; if not, write to the                         *
 *   Free Software Foundation, Inc.,                                       *
 *   51 Franklin Street, Fifth Floor, Boston, MA 02110-1301,  USA.         *
 ***************************************************************************
 *
 */

#include <wx/wx.h>
#include <wx/glcanvas.h>


#ifdef __WXOSX__
# include <OpenGL/OpenGL.h>
# include <OpenGL/gl3.h>
#endif

#ifdef __ANDROID__
#include <qopengl.h>
#include "GL/gl_private.h"
#endif

#ifdef FLATPAK
#include "GL/gl_private.h"
#endif

#ifdef __WXGTK__
#include <GL/glx.h>
#endif

#ifdef USE_GLES2
#include "GLES2/gl2.h"
#endif

#include "climatology_pi.h"
#include "ClimatologyMath.h"
#include "ClimatologyRenderPreparation.h"
//#include "gldefs.h"
#include "icons.h"

#include <atomic>
#include <limits>
#include <sstream>
#include <thread>

#define FAILED_FILELIST_MSG_LEN 150

static int s_multitexturing = 0;

#if !defined(__ANDROID__) && !defined(__APPLE__)
static PFNGLACTIVETEXTUREARBPROC s_glActiveTextureARB = 0;
static PFNGLMULTITEXCOORD2DARBPROC s_glMultiTexCoord2dARB = 0;
#endif

static int texture_format;
static bool glQueried = false;

static GLboolean QueryExtension( const char *extName )
{
    /*
     ** Search for extName in the extensions string. Use of strstr()
     ** is not sufficient because extension names can be prefixes of
     ** other extension names. Could use strtok() but the constant
     ** string returned by glGetString might be in read-only memory.
     */
    char *p;
    char *end;
    int extNameLen;

    extNameLen = strlen( extName );

    p = (char *) glGetString( GL_EXTENSIONS );
    if( NULL == p ) {
        return GL_FALSE;
    }

    end = p + strlen( p );

    while( p < end ) {
        int n = strcspn( p, " " );
        if( ( extNameLen == n ) && ( strncmp( extName, p, n ) == 0 ) ) {
            return GL_TRUE;
        }
        p += ( n + 1 );
    }
    return GL_FALSE;
}

#if defined(__WXMSW__)
#define systemGetProcAddress(ADDR) wglGetProcAddress(ADDR)
#elif defined(__WXOSX__)
#include <dlfcn.h>
#define systemGetProcAddress(ADDR) dlsym( RTLD_DEFAULT, ADDR)
#elif defined(__OCPN__ANDROID__)
#define systemGetProcAddress(ADDR) eglGetProcAddress(ADDR)
#else
#define systemGetProcAddress(ADDR) glXGetProcAddress((const GLubyte*)ADDR)
#endif

double deg2rad(double degrees)
{
  return M_PI * degrees / 180.0;
}

ClimatologyOverlay::~ClimatologyOverlay()
{
    if(m_iTexture)
        glDeleteTextures( 1, &m_iTexture );
    delete m_pDCBitmap, delete[] m_pRGBA;
}

static const wxString climatology_pi = "climatology_pi: ";
static bool s_bnoglrepeat = true;

double ClimatologyIsoBarMap::CalcParameter(double lat, double lon)
{
    return m_calibration.Apply(
        m_factory.getValueMonth(MAG, m_setting, lat, lon, m_month));
}

#define CYCLONE_CACHE_SEMAPHORE_COUNT 4

struct ClimatologyIsobarJob {
    int setting = -1;
    int month = 0;
    int units = 0;
    double spacing = 0.0;
    double step = 0.0;
    std::unique_ptr<ClimatologyIsoBarMap> map;
    std::thread worker;
    std::atomic<bool> done{false};
    bool succeeded = false;
};

struct ClimatologyTextureJob {
    int setting = -1;
    int month = 0;
    int width = 0;
    int height = 0;
    double latoff = 0.0;
    double lonoff = 0.0;
    std::vector<unsigned char> pixels;
    std::thread worker;
    std::atomic<bool> cancel{false};
    std::atomic<bool> done{false};
    bool succeeded = false;
};

struct ClimatologyCycloneJob {
    climatology::CycloneFilterState filters;
    climatology::CycloneSpatialIndex cache;
    std::thread worker;
    std::atomic<bool> cancel{false};
    std::atomic<bool> done{false};
    bool succeeded = false;
};

static bool SameCycloneFilters(const climatology::CycloneFilterState &a,
                               const climatology::CycloneFilterState &b)
{
    return a.tropical == b.tropical && a.subtropical == b.subtropical &&
        a.extratropical == b.extratropical && a.remnant == b.remnant &&
        a.minimum_wind_knots == b.minimum_wind_knots &&
        a.maximum_pressure_hpa == b.maximum_pressure_hpa &&
        a.start_utc == b.start_utc && a.end_utc == b.end_utc &&
        a.include_enso_unavailable == b.include_enso_unavailable &&
        a.include_el_nino == b.include_el_nino &&
        a.include_la_nina == b.include_la_nina &&
        a.include_neutral == b.include_neutral && a.day_span == b.day_span;
}

static double IsobarStep(int selection)
{
    switch(selection) {
    default: return 4.0;
    case 1: return 2.0;
    case 2: return 1.0;
    case 3: return .5;
    case 4: return .25;
    }
}

ClimatologyOverlayFactory::ClimatologyOverlayFactory( ClimatologyDialog &dlg )
    : //m_bUpdateCyclones(true),
    m_cyclone_cache_semaphore(CYCLONE_CACHE_SEMAPHORE_COUNT),
    m_bCompletedLoading(false),
    m_dlg(dlg), m_Settings(dlg.m_cfgdlg->m_Settings),
    m_cyclonesDisplayList(0),
    m_cycloneRequestPending(false), m_disableCyclonesForPerformance(false)
{
    // make sure the user data directory exists
    wxFileName::Mkdir(ClimatologyDataDirectory(), wxS_DIR_DEFAULT, wxPATH_MKDIR_FULL);
    for(int m=0; m<13; m++) {
        m_WindData[m] = NULL;
        m_CurrentData[m] = NULL;
    }

    m_CurrentTimeline = wxDateTime::Now();
    /* use a year without a leap year */
    if(m_CurrentTimeline.IsLeapYear() &&
       m_CurrentTimeline.GetMonth() == wxDateTime::Feb &&
       m_CurrentTimeline.GetDay() == 29)
        m_CurrentTimeline.SetDay(28);

    m_CurrentTimeline.SetYear(1999);

    m_bAllTimes = false;

    StartDatasetLoad();
}

ClimatologyOverlayFactory::~ClimatologyOverlayFactory()
{
    m_dataService.CancelAndWait();
    CancelBackgroundWork();
    Free();
}

void ClimatologyOverlayFactory::CancelBackgroundWork()
{
    if(m_isobarJob) {
        m_isobarJob->map->m_bNeedsRecompute.store(true,
                                                  std::memory_order_release);
        if(m_isobarJob->worker.joinable()) m_isobarJob->worker.join();
        m_isobarJob.reset();
    }
    if(m_textureJob) {
        m_textureJob->cancel.store(true, std::memory_order_release);
        if(m_textureJob->worker.joinable()) m_textureJob->worker.join();
        m_textureJob.reset();
    }
    m_readyTexture.reset();
    if(m_cycloneJob) {
        m_cycloneJob->cancel.store(true, std::memory_order_release);
        if(m_cycloneJob->worker.joinable()) m_cycloneJob->worker.join();
        m_cycloneJob.reset();
    }
    m_cycloneRequestPending = false;
}

void ClimatologyOverlayFactory::StartDatasetLoad()
{
    const wxScopedCharBuffer path = ClimatologyDataDirectory().utf8_str();
    if(!m_dataService.Start(path.data() ? path.data() : ""))
        wxLogError(climatology_pi + _("could not start dataset loader"));
}

bool ClimatologyOverlayFactory::PollDatasetLoad(bool &succeeded, wxString &error)
{
    climatology::DatasetLoadResult result;
    if(!m_dataService.TakeCompletion(result))
        return false;

    succeeded = result.Succeeded();
    if(succeeded) {
        PublishDataset(result.snapshot);
        return true;
    }

    for(std::vector<std::string>::const_iterator it = result.errors.begin();
        it != result.errors.end(); ++it) {
        if(!error.empty()) error += "\n";
        error += wxString::FromUTF8(it->c_str());
    }
    if(result.cancelled && error.empty())
        error = _("Dataset loading was cancelled.");
    if(error.empty())
        error = _("Dataset loading failed without diagnostic details.");
    return true;
}

void ClimatologyOverlayFactory::PollBackgroundWork(
    bool &needs_refresh, bool &api_changed, int &disable_isobars,
    bool &disable_cyclones_for_performance)
{
    needs_refresh = false;
    api_changed = false;
    disable_isobars = -1;
    disable_cyclones_for_performance = false;

    if(m_isobarJob &&
       m_isobarJob->done.load(std::memory_order_acquire)) {
        if(m_isobarJob->worker.joinable()) m_isobarJob->worker.join();
        const bool cancelled = m_isobarJob->map->m_bNeedsRecompute.load(
            std::memory_order_acquire);
        const climatology::ClimatologyRenderState state =
            m_dlg.CaptureRenderState();
        const climatology::FieldRenderState &field =
            state.fields[m_isobarJob->setting];
        const bool still_requested = field.visible && field.enabled &&
            field.isobars &&
            field.units == m_isobarJob->units &&
            field.isobar_spacing == m_isobarJob->spacing &&
            IsobarStep(field.isobar_step) == m_isobarJob->step;
        if(m_isobarJob->succeeded && !cancelled && still_requested) {
            ClimatologyIsoBarMap *&slot =
                m_Settings.Settings[m_isobarJob->setting]
                    .m_pIsobars[m_isobarJob->month];
            delete slot;
            slot = m_isobarJob->map.release();
            needs_refresh = true;
        } else if(!cancelled && !m_isobarJob->succeeded) {
            disable_isobars = m_isobarJob->setting;
        }
        m_isobarJob.reset();
        needs_refresh = true;
    }

    if(m_textureJob &&
       m_textureJob->done.load(std::memory_order_acquire)) {
        if(m_textureJob->worker.joinable()) m_textureJob->worker.join();
        if(m_textureJob->succeeded &&
           !m_textureJob->cancel.load(std::memory_order_acquire)) {
            m_readyTexture = std::move(m_textureJob);
            needs_refresh = true;
        } else {
            m_textureJob.reset();
            needs_refresh = true;
        }
    }

    if(m_cycloneJob &&
       m_cycloneJob->done.load(std::memory_order_acquire)) {
        if(m_cycloneJob->worker.joinable()) m_cycloneJob->worker.join();
        const bool publish = m_cycloneJob->succeeded &&
            !m_cycloneJob->cancel.load(std::memory_order_acquire);
        if(publish) {
            for(int i = 0; i < CYCLONE_CACHE_SEMAPHORE_COUNT; ++i)
                m_cyclone_cache_semaphore.Wait();
            m_cyclone_cache.swap(m_cycloneJob->cache);
            for(int i = 0; i < CYCLONE_CACHE_SEMAPHORE_COUNT; ++i)
                m_cyclone_cache_semaphore.Post();
            needs_refresh = true;
            api_changed = true;
        }
        m_cycloneJob.reset();
        if(m_cycloneRequestPending) {
            const climatology::CycloneFilterState requested =
                m_requestedCycloneFilters;
            m_cycloneRequestPending = false;
            StartCycloneCacheJob(requested);
        }
    }

    if(m_disableCyclonesForPerformance) {
        m_disableCyclonesForPerformance = false;
        disable_cyclones_for_performance = true;
    }
}

void ClimatologyOverlayFactory::PublishDataset(
    std::shared_ptr<const climatology::ClimatologyDatasetSnapshot> snapshot)
{
    m_snapshot = snapshot;
    m_query.reset(new climatology::ClimatologyQueryEngine(snapshot));
    const climatology::DatasetAvailability &available = snapshot->availability;
    m_dlg.ApplyDatasetAvailability(available);

    m_bCompletedLoading.store(true, std::memory_order_release);
    if(available.cyclones)
        BuildCycloneCache();
    wxLogMessage(climatology_pi + _("dataset ready: ") +
                 wxString::FromUTF8(snapshot->metadata.version.c_str()));
}

void ClimatologyOverlayFactory::GetDateInterpolation(const wxDateTime *cdate,
                                                     int &month, int &nmonth, double &dpos)
{
    if(!cdate) {
        if(m_bAllTimes) {
            month = nmonth = 12;
            dpos = 1;
            return;
        }
        cdate = &m_CurrentTimeline;
    }
    
    month = cdate->GetMonth();
    const double day_fraction =
        (cdate->GetHour() * 3600.0 + cdate->GetMinute() * 60.0 +
         cdate->GetSecond()) / 86400.0;
    const int daysinmonth = wxDateTime::GetNumberOfDays(
        cdate->GetMonth(), cdate->GetYear());
    const climatology::MonthInterpolationResult interpolation =
        climatology::MonthInterpolation(month, cdate->GetDay(), day_fraction,
                                        daysinmonth);
    nmonth = interpolation.neighbour;
    dpos = interpolation.current_weight;
}

bool ClimatologyOverlayFactory::InterpolateWindAtlasTime(int month, int nmonth, double dpos,
                                                         double lat, double lon,
                                                         double *directions, double *speeds,
                                                         double &gale, double &calm)
{
    if(!m_query)
        return false;
    climatology::MonthPosition position;
    position.month = month;
    position.next_month = nmonth;
    position.current_weight = dpos;
    climatology::WindDistributionResult result;
    if(!m_query->WindDistribution(lat, lon, position, result))
        return false;
    for(std::size_t sector = 0; sector < climatology::kWindSectors; ++sector) {
        directions[sector] = result.frequency[sector];
        speeds[sector] = result.speed_knots[sector];
    }
    gale = result.gale_probability;
    calm = result.calm_probability;
    return true;
}

static double interpquad(double v1, double v2, double v3, double v4, double d1, double d2)
{
    double w1 = d1*v3 + (1-d1)*v1;
    double w2 = d1*v4 + (1-d1)*v2;
    return      d2*w2 + (1-d2)*w1;
}

bool ClimatologyOverlayFactory::InterpolateWindAtlas(const wxDateTime &date,
                                                     double lat, double lon,
                                                     double *directions, double *speeds,
                                                     double &gale, double &calm)
{
    int month, nmonth;
    double dpos;
    GetDateInterpolation(&date, month, nmonth, dpos);
    return InterpolateWindAtlasTime(month, nmonth, dpos, lat, lon,
                                    directions, speeds, gale, calm);
}

void ClimatologyOverlayFactory::DrawLine( double x1, double y1, double x2, double y2,
                                          const wxColour &color, double width )
{
    m_dc->SetPen( wxPen(color, width ) );
    m_dc->SetBrush( *wxTRANSPARENT_BRUSH);
    m_dc->DrawLine(x1, y1, x2, y2);
}

void ClimatologyOverlayFactory::DrawCircle( double x, double y, double r,
                                            const wxColour &color, double width )
{
    m_dc->SetPen( wxPen(color, width ) );
    m_dc->SetBrush( *wxTRANSPARENT_BRUSH);
    m_dc->DrawCircle(x, y, r);
}

struct ColorMap {
    double val;
    const char *text;
    wxUint8 transp;
};

static ColorMap WindMap[] =
{{0,  "#ffffff", 0}, {2, "#00ffff", 0},  {4,  "#00e4e4", 0}, {5,  "#00d9e4", 0},
 {6,  "#00d9d4", 0}, {7, "#00d9b2", 0},  {8,  "#00d96e", 0}, {9,  "#00d92a", 0},
 {10, "#00d900", 0}, {11, "#2ad900", 0}, {12, "#6ed900", 0}, {13, "#b2d900", 0},
 {14, "#d4d400", 0}, {15, "#d9a600", 0}, {16, "#d90000", 0}, {17, "#d90040", 0},
 {18, "#d90060", 0}, {19, "#ae0080", 0}, {20, "#8300a0", 0}, {21, "#5700c0", 0},
 {25, "#0000d0", 0}, {30, "#0000f0", 0}, {35, "#00a0ff", 0}, {40, "#a0a0ff", 0},
 {50, "#ffffff", 0}};

static ColorMap CurrentMap[] =
{{0,   "#0000d9", 255}, {.1,  "#002ad9", 196},  {.2, "#006ed9", 128},  {.3, "#00b2d9", 64},
 {.4,  "#04d4d4", 0}, {.5,  "#06d9a6", 0},  {.7, "#a0d906", 0},  {.9, "#b0d900", 0},
 {1.2, "#c0d900", 0}, {1.5, "#d0ae00", 0}, {1.8, "#e08300", 0}, {2.1, "#e05700", 0},
 {2.4, "#f00000", 0}, {2.7, "#f00004", 0}, {3.0, "#f0001c", 0}, {3.6, "#f00048", 0},
 {4.2, "#f00069", 0}, {4.8, "#f000a0", 0}, {5.6, "#f000f0", 0}};

static ColorMap PressureMap[] =
{{900,  "#283282", 0}, {980,  "#273c8c", 0}, {990,  "#264696", 0}, {1000,  "#2350a0", 0},
 {1001, "#1f5aaa", 0}, {1002, "#1a64b4", 0}, {1003, "#136ec8", 0}, {1004, "#0c78e1", 0},
 {1005, "#0382e6", 0}, {1006, "#0091e6", 0}, {1007, "#009ee1", 0}, {1008, "#00a6dc", 0},
 {1009, "#00b2d7", 0}, {1010, "#00bed2", 0}, {1011, "#28c8c8", 0}, {1012, "#78d2aa", 0},
 {1013, "#8cdc78", 0}, {1014, "#a0eb5f", 0}, {1015, "#c8f550", 0}, {1016, "#f3fb02", 0},
 {1017, "#ffed00", 0}, {1018, "#ffdd00", 0}, {1019, "#ffc900", 0}, {1020, "#ffab00", 0},
 {1021, "#ff8100", 0}, {1022, "#f1780c", 0}, {1024, "#e26a23", 0}, {1028, "#d5453c", 0},
 {1040, "#b53c59", 0}};

static ColorMap SeaTempMap[] =
{{0, "#0000d9", 0},  {3, "#002ad9", 0},  {6, "#006ed9", 0},  {9, "#00b2d9", 0},
 {12, "#00d4d4", 0}, {15, "#00d9a6", 0}, {18, "#00d900", 0}, {20, "#95d900", 0},
 {22, "#d9d900", 0}, {23, "#d9ae00", 0}, {24, "#d98300", 0}, {25, "#d95700", 0},
 {26, "#d90000", 0}, {27, "#ae0000", 0}, {28, "#8c0000", 0}, {29, "#870000", 0},
 {30, "#690000", 0}, {32, "#550000", 0}, {35, "#410000", 0}};

static ColorMap AirTempMap[] =
{{-50, "#0000d9", 0},  {-40, "#002ad9", 0},  {-30, "#006ed9", 0},  {-20, "#00b2d9", 0},
 {-10, "#00d4d4", 0}, {0, "#00d9a6", 0}, {5, "#00d900", 0}, {8, "#95d900", 0},
 {12, "#d9d900", 0}, {15, "#d9ae00", 0}, {18, "#d98300", 0}, {21, "#d95700", 0},
 {24, "#d90000", 0}, {27, "#ae0000", 0}, {30, "#8c0000", 0}, {35, "#870000", 0},
 {40, "#690000", 0}, {45, "#550000", 0}, {50, "#410000", 0}};

static ColorMap CloudMap[] =
{{0, "#f0f0e6", 255},  {10, "#e6e6dc", 224}, {20, "#dcdcd2", 192},
 {30, "#c8c8b4", 160}, {40, "#aaaa8c", 128}, {50, "#969678", 96}, {60, "#787864", 64},
 {70, "#646450", 32},  {80, "#5a5a46", 0},   {90, "#505036", 0}, {100, "#404036", 0}};

static ColorMap PrecipitationMap[] =
{{0, "#000080", 255}, {1, "#0000a0", 208}, {2, "#4040f0", 164},
 {3, "#8080f0", 128}, {4, "#f0f0f0", 64}, {5, "#f0a0f0", 32}, {8, "#f080f0", 0},
 {10, "#f04080", 0}, {13, "#f00040", 0}, {16, "#f00000", 0}, {19, "#800000", 0}};

static ColorMap RelativeHumidityMap[] =
{{0,  "#000000", 0}, {30, "#303030", 0}, {60, "#606060", 0},
 {65, "#a06060", 0}, {70, "#ff6060", 0}, {75, "#ffc080", 0},
 {80, "#a0f0a0", 0}, {85, "#60f0f0", 0}, {95, "#40a0f0", 0},
 {100, "#2080f0", 0}};

static ColorMap LightningMap[] =
{{0, "#490000", 255},  {64, "#890000", 128}, {128, "#a98900", 64},
 {192, "#ffd900", 0}, {255, "#ffff00", 0}};

ColorMap SeaDepthMap[] =
{{0, "#0000d9", 255},  {20, "#002ad9", 0},   {50, "#006ed9", 0},   {100, "#00b2d9", 0},
 {150, "#00d4d4", 0},  {250, "#00d9a6", 0},  {400, "#00d900", 0},  {600, "#95d900", 0},
 {800, "#a9d900", 0},  {1000, "#d9d900", 0}, {1200, "#d9f000", 0}, {1400, "#d9c040", 0},
 {2000, "#a9c040", 0}, {3000, "#a0a040", 0}, {4000, "#808060", 0}, {5000, "#606060", 0},
 {6000, "#404040", 0}, {8000, "#202020", 0}, {10000, "#000000", 0}};

static ColorMap CycloneMap[] =
{{0, "#0000d9", 255}, {10, "#002ad9", 200}, {20, "#006ed9", 128}, {30, "#00b2d9", 96},
 {40, "#00d4d4", 64},  {50, "#00d9a6", 64}, {60, "#00d900", 64}, {70, "#75d900", 64},
 {80, "#a9d900", 32},  {90, "#a9ae00", 32}, {100, "#d98000", 32}, {110, "#d94000", 32},
 {120, "#d90000", 0}, {130, "#ff0000", 0}, {140, "#ff0080", 0}, {150, "#ff00ff", 0},
 {160, "#ff40ff", 0}, {180, "#ff80ff", 0}, {200, "#ffffff", 0}};

static int color255(char a, char b)
{
    char str[3] = {a, b, 0};
    return strtol(str, 0, 16);
}

static climatology::RgbaPixel TextColor(const char *text)
{
    climatology::RgbaPixel pixel;
    pixel.red = color255(text[1], text[2]);
    pixel.green = color255(text[3], text[4]);
    pixel.blue = color255(text[5], text[6]);
    pixel.alpha = 255;
    return pixel;
}                        

ColorMap *ColorMaps[] = {WindMap, CurrentMap, PressureMap, SeaTempMap, AirTempMap,
                         CloudMap, PrecipitationMap, RelativeHumidityMap, LightningMap,
                         SeaDepthMap, CycloneMap};

static const int ColorMapLens[] = { (sizeof WindMap) / (sizeof *WindMap),
                             (sizeof CurrentMap) / (sizeof *CurrentMap),
                             (sizeof PressureMap) / (sizeof *PressureMap),
                             (sizeof SeaTempMap) / (sizeof *SeaTempMap),
                             (sizeof AirTempMap) / (sizeof *AirTempMap),
                             (sizeof CloudMap) / (sizeof *CloudMap),
                             (sizeof PrecipitationMap) / (sizeof *PrecipitationMap),
                             (sizeof RelativeHumidityMap) / (sizeof *RelativeHumidityMap),
                             (sizeof LightningMap) / (sizeof *LightningMap),
                             (sizeof SeaDepthMap) / (sizeof *SeaDepthMap),
                             (sizeof CycloneMap) / (sizeof *CycloneMap)};


static climatology::RgbaPixel GraphicPixel(int setting, double val_in)
{
    if(isnan(val_in))
        return climatology::RgbaPixel(); /* transparent */

    int colormap_index = setting;
    ColorMap *map = ColorMaps[colormap_index];
    int maplen = ColorMapLens[colormap_index];

    double cmax = 1;
    for(int i=1; i<maplen; i++) {
        double nmapvala = map[i-1].val/cmax;
        double nmapvalb = map[i].val/cmax;
        if(nmapvalb > val_in || i==maplen-1) {
            climatology::RgbaPixel b, c;
            c = TextColor(map[i].text);
            b = TextColor(map[i-1].text);
            double d = (val_in-nmapvala)/(nmapvalb-nmapvala);
            c.red = (1-d)*b.red + d*c.red;
            c.green = (1-d)*b.green + d*c.green;
            c.blue = (1-d)*b.blue + d*c.blue;
            c.alpha = 255 -
                ((1-d)*map[i-1].transp + d*map[i].transp);
            return c;
        }
    }
    climatology::RgbaPixel black;
    black.alpha = 255;
    return black; /* unreachable */
}

wxColour ClimatologyOverlayFactory::GetGraphicColor(int setting, double val_in)
{
    const climatology::RgbaPixel pixel = GraphicPixel(setting, val_in);
    return wxColour(pixel.red, pixel.green, pixel.blue, pixel.alpha);
}

void ClimatologyOverlayFactory::LoadInternal(wxGenericProgressDialog *progressdialog)
{
    wxString fmt = "%02d";

    /* load wind */
    for(int month = 0; month < 12; month++) {
        wxString filename = wxString::Format("wind"+fmt, month+1);
        if(progressdialog && !progressdialog->Update(month, filename))
            return;
        ReadWindData(month, filename);
        if(m_WindData[month])
            ReadWindExtras(month, wxString::Format("wind-extras"+fmt, month+1));
    }

    if(progressdialog && !progressdialog->Update(12, _("averaging wind")))
        return;
    AverageWindData();

    /* load current */
    for(int month = 0; month < 12; month++) {
        wxString filename = wxString::Format("current"+fmt, month+1);
        if(progressdialog && !progressdialog->Update(month+13, filename))
            return;
        ReadCurrentData(month, filename);
    }

    if(progressdialog && !progressdialog->Update(25, _("averaging current")))
        return;

    AverageCurrentData();

    /* load sea level pressure and sea surface temperature */
    if(progressdialog && !progressdialog->Update(26, _("sea level presure")))
        return;
    ReadSeaLevelPressureData("sealevelpressure");
    
    if(progressdialog && !progressdialog->Update(27, _("sea surface tempertature")))
        return;
    ReadSeaSurfaceTemperatureData("seasurfacetemperature");

    if(progressdialog && !progressdialog->Update(28, _("air tempertature")))
        return;
    ReadAirTemperatureData("airtemperature");

    if(progressdialog && !progressdialog->Update(28, _("cloud cover")))
        return;
    ReadCloudData("cloud");

    if(progressdialog && !progressdialog->Update(29, _("precipitation")))
        return;
    ReadPrecipitationData("precipitation");

    if(progressdialog && !progressdialog->Update(30, _("relative humidity")))
        return;
    ReadRelativeHumidityData("relativehumidity");

    /* load lightning */
    if(progressdialog && !progressdialog->Update(30, _("lightning")))
        return;
    ReadLightningData("lightning");

    /* load sea depth */
    if(progressdialog && !progressdialog->Update(30, _("sea depth")))
        return;
    ReadSeaDepthData("seadepth");

    /* load cyclone tracks */
    bool allcyclone = true;
    if(progressdialog && !progressdialog->Update(30, _("cyclone (east pacific)")))
        return;
    if(!ReadCycloneData("cyclone-epa", m_epa))
        allcyclone = false;

    if(progressdialog && !progressdialog->Update(31, _("cyclone (west pacific)")))
        return;
    if(!ReadCycloneData("cyclone-wpa", m_wpa))
        allcyclone = false;

    if(progressdialog && !progressdialog->Update(32, _("cyclone (south pacific)")))
        return;
    if(!ReadCycloneData("cyclone-spa", m_spa, true))
        allcyclone = false;

    if(progressdialog && !progressdialog->Update(33, _("cyclone (atlantic)")))
        return;
    if(!ReadCycloneData("cyclone-atl", m_atl))
        allcyclone = false;

    if(progressdialog && !progressdialog->Update(34, _("cyclone (north indian)")))
        return;
    if(!ReadCycloneData("cyclone-nio", m_nio))
        allcyclone = false;

    if(progressdialog && !progressdialog->Update(35, _("cyclone (south indian)")))
        return;
    if(!ReadCycloneData("cyclone-she", m_she, true))
        allcyclone = false;

    if(allcyclone)
        m_dlg.m_cbCyclones->Enable();

    if(progressdialog && !progressdialog->Update(36, _("el nino years")))
        return;

    if(!ReadElNinoYears("elnino_years.txt")) {
        m_dlg.m_cfgdlg->m_cbElNino->Disable();
        m_dlg.m_cfgdlg->m_cbLaNina->Disable();
        m_dlg.m_cfgdlg->m_cbNeutral->Disable();
    }

    if(progressdialog && !progressdialog->Update(37, _("cyclone cache")))
        return;
    BuildCycloneCache();
}

void ClimatologyOverlayFactory::Load()
{
    Free();
    m_sFailedMessage = "";
    m_FailedFiles.clear();
    
    wxGenericProgressDialog *progressdialog = nullptr;
    progressdialog = new wxGenericProgressDialog( _("Climatology"), wxString(), 38, &m_dlg,
                                     wxPD_CAN_ABORT | wxPD_ELAPSED_TIME );
    LoadInternal(progressdialog);
    progressdialog->Destroy();
}

void ClimatologyOverlayFactory::Free()
{
    for(int i=0; i<ClimatologyOverlaySettings::SETTINGS_COUNT; i++)
        for(int m=0; m<13; m++) {
            delete m_Settings.Settings[i].m_pIsobars[m];
            m_Settings.Settings[i].m_pIsobars[m] = NULL;
        }

    // free wind data
    for(int m=0; m<13; m++) {
        delete m_WindData[m];
        m_WindData[m] = NULL;
        delete m_CurrentData[m];
        m_CurrentData[m] = NULL;
    }
    
    // free cyclones
    std::list<Cyclone*> *cyclones[6] = {&m_epa, &m_wpa, &m_spa, &m_atl, &m_nio, &m_she};
    for(int i=0; i < 6; i++) {
        for(std::list<Cyclone*>::iterator it = cyclones[i]->begin(); it != cyclones[i]->end(); it++) {
            Cyclone *s = *it;
            delete s;
        }
        cyclones[i]->clear();
    }
    m_cyclone_cache.clear();
}

static wxUint16 DecodeLE16(const unsigned char *bytes)
{
    return static_cast<wxUint16>(bytes[0]) |
           static_cast<wxUint16>(bytes[1]) << 8;
}

static bool ReadLE16(ZUFILE *file, wxUint16 &value)
{
    unsigned char bytes[2];
    if(zu_read(file, bytes, sizeof bytes) != sizeof bytes)
        return false;
    value = DecodeLE16(bytes);
    return true;
}

static bool ReadLEI16(ZUFILE *file, wxInt16 &value)
{
    wxUint16 raw;
    if(!ReadLE16(file, raw))
        return false;
    value = static_cast<wxInt16>(raw);
    return true;
}

void ClimatologyOverlayFactory::ReadWindData(int month, wxString filename)
{
    ZUFILE *f;
#ifdef __OCPN__ANDROID__
    int div = 1; // less ram usage half resolution ?
#else
    int div = 1;
#endif
    wxString path = ClimatologyDataDirectory();
    if(!(f = TryOpenFile(path + filename))) {
        path = ClimatologyDataDirectory();
        if(!(f = TryOpenFile(path + filename)))
            goto missing;
    }

    m_dlg.m_cbWind->Enable();
    
    wxUint16 header[7];
    int dirs;
    for(size_t index = 0; index < sizeof header / sizeof *header; ++index)
        if(!ReadLE16(f, header[index]))
            goto corrupt;

    if(header[0] != 0xfefe ||
       header[1] == 0 || header[1] > 180*16 ||
       header[2] == 0 || header[2] > 360*16 ||
       header[3] != 8 || header[4] == 0 || header[5] == 0 || header[6] == 0)
        goto corrupt;

    try {
        m_WindData[month] = new WindData(header[1]/div, header[2]/div,
            header[3], header[4], (float)header[5] / header[6]);
    } catch(const std::bad_alloc &) {
        goto corrupt;
    }

    dirs = m_WindData[month]->dir_cnt;

    for(int pass=0; pass<2*dirs+1; pass++)
        for(int lati = 0; lati < header[1]; lati++) {
            for(int loni = 0; loni < header[2]; loni++) {
                WindData::WindPolar &wp = m_WindData[month]->data[lati*m_WindData[month]->longitudes/div + loni/div];
                wxUint8 value;

                if(pass == 0) {
                    if(zu_read(f, &value, 1) != 1)
                        goto corrupt;

                    if(value > 200)
                        wp.gale = 255;
                    else if(value >= 100) {
                        wp.gale = value - 100;
                        wp.calm = 0;
                    } else {
                        wp.gale = 0;
                        wp.calm = value;
                    }
                } else if(wp.gale != 255) {
                    if(pass < dirs + 1) {
                        if(zu_read(f, &value, 1) != 1)
                            goto corrupt;

                        wp.directions[pass - 1] = value;
                    } else {
                        if(wp.directions[pass-dirs-1] == 0) {
                            value = 0;
                        }
                        else if(zu_read(f, &value, 1) != 1)
                            goto corrupt;

                        wp.speeds[pass-dirs-1] = value;
                    }
                }
            }
        }

    zu_close(f);
    return;

corrupt:
    zu_close(f);
    delete m_WindData[month];
    m_WindData[month] = NULL;
    m_sFailedMessage += _("corrupt file: ") + filename + "\n";
missing:
    m_FailedFiles.push_back(filename);
    wxLogMessage(climatology_pi + _("wind data file corrupt: ") + filename);
}

void ClimatologyOverlayFactory::AverageWindData()
{
    int fmonth;
    for(fmonth=0; fmonth<12; fmonth++)
        if(m_WindData[fmonth])
            goto havedata;
    return;

havedata:
    int latitudes = m_WindData[fmonth]->latitudes;
    int longitudes = m_WindData[fmonth]->longitudes;
    int dir_cnt = m_WindData[fmonth]->dir_cnt;
    float dir_res = m_WindData[fmonth]->direction_resolution;
    float spd_mul = m_WindData[fmonth]->speed_multiplier;
    m_WindData[12] = new WindData(latitudes, longitudes, dir_cnt, dir_res, spd_mul);

    float latoff = 90.0/m_WindData[fmonth]->latitudes, lonoff = 180.0/m_WindData[fmonth]->longitudes;

    float *directions = new float[dir_cnt];
    float *speeds = new float[dir_cnt];

    for(int lati = 0; lati < latitudes; lati++)
        for(int loni = 0; loni < longitudes; loni++) {
            double lat = 180.0*((double)lati/latitudes-.5) + latoff;
            double lon = 360.0*loni/longitudes + lonoff;

            double gale = 0, calm = 0;
            for(int i=0; i<dir_cnt; i++)
                directions[i] = speeds[i] = 0;

            int mcount = 0;
            for(int month=0; month<12; month++) {
                if(!m_WindData[month])
                    continue;
                WindData::WindPolar *polar = m_WindData[month]->GetPolar(lat, lon);
                if(!polar) {
                    mcount = 0; /* average invalid if missing any month */
                    break;
                }

                int mdir_cnt = m_WindData[month]->dir_cnt;
                gale += polar->gale;
                calm +=  polar->calm;
                for(int i=0; i<dir_cnt; i++) {
                    directions[i] += polar->directions[i*mdir_cnt/dir_cnt];
                    speeds[i] += polar->speeds[i*mdir_cnt/dir_cnt];
                }
                mcount++;
            }

            WindData::WindPolar &wp = m_WindData[12]->data[lati*longitudes + loni];
            if(mcount == 0) {
                wp.gale = 255;
                goto done;
            }

            wp.gale = gale / mcount;
            wp.calm = calm / mcount;

            for(int i=0; i<dir_cnt; i++) {
                wp.directions[i] = directions[i] / mcount;
                wp.speeds[i] = speeds[i] / mcount;
            }

        done:;
        }

    delete [] directions;
    delete [] speeds;
}

bool ClimatologyOverlayFactory::ReadWindExtras(int month, wxString filename)
{
    if(month < 0 || month >= 12 || !m_WindData[month])
        return false;
    const wxString path = ClimatologyDataDirectory() + filename;
    ZUFILE *f = zu_open(path.mb_str(), "rb", ZU_COMPRESS_AUTO);
    if(!f)
        f = zu_open((path + ".gz").mb_str(), "rb", ZU_COMPRESS_AUTO);
    if(!f)
        return false;  // Optional additive file; historical datasets omit it.

    unsigned char magic[4];
    wxUint16 latitudes, longitudes;
    if(zu_read(f, magic, sizeof magic) != sizeof magic ||
       memcmp(magic, "WEX1", sizeof magic) != 0 ||
       !ReadLE16(f, latitudes) || !ReadLE16(f, longitudes) ||
       latitudes != m_WindData[month]->latitudes ||
       longitudes != m_WindData[month]->longitudes) {
        zu_close(f);
        wxLogMessage(climatology_pi + _("ignoring corrupt optional wind extras: ") + filename);
        return false;
    }

    const size_t cells = static_cast<size_t>(latitudes) * longitudes;
    std::vector<wxUint8> calm(cells), gale(cells);
    if(zu_read(f, &calm[0], cells) != static_cast<int>(cells) ||
       zu_read(f, &gale[0], cells) != static_cast<int>(cells)) {
        zu_close(f);
        wxLogMessage(climatology_pi + _("ignoring truncated optional wind extras: ") + filename);
        return false;
    }
    zu_close(f);

    for(size_t index = 0; index < cells; ++index) {
        WindData::WindPolar &polar = m_WindData[month]->data[index];
        if(calm[index] == 255 || gale[index] == 255) {
            if(polar.gale != 255) {
                wxLogMessage(climatology_pi + _("wind extras missing mask disagrees with atlas: ") + filename);
                return false;
            }
        } else if(calm[index] > 100 || gale[index] > 100 || polar.gale == 255) {
            wxLogMessage(climatology_pi + _("wind extras values disagree with atlas: ") + filename);
            return false;
        }
    }
    for(size_t index = 0; index < cells; ++index)
        if(m_WindData[month]->data[index].gale != 255) {
            m_WindData[month]->data[index].calm = calm[index];
            m_WindData[month]->data[index].gale = gale[index];
        }
    wxLogMessage(climatology_pi + _("loaded calm/gale wind extras: ") + filename);
    return true;
}

void ClimatologyOverlayFactory::ReadCurrentData(int month, wxString filename)
{
    ZUFILE *f;
    wxString path = ClimatologyDataDirectory();
    if(!(f = TryOpenFile(path + filename))) {
        path = ClimatologyDataDirectory();
        if(!(f = TryOpenFile(path + filename)))
            goto missing;
    }

    m_dlg.m_cbCurrent->Enable();

    wxUint16 header[3];
    for(size_t index = 0; index < sizeof header / sizeof *header; ++index)
        if(!ReadLE16(f, header[index]))
            goto corrupt;
    if(header[0] == 0 || header[0] > 4096 || header[1] == 0 ||
       header[1] > 8192 || header[2] == 0)
        goto corrupt;

    try {
        m_CurrentData[month] = new CurrentData(header[0], header[1], header[2]);
    } catch(const std::bad_alloc &) {
        goto corrupt;
    }
    for(int dim = 0; dim<2; dim++)
        for(int lati = 0; lati < m_CurrentData[month]->latitudes; lati++)
            for(int loni = 0; loni < m_CurrentData[month]->longitudes; loni++) {
                int ind = m_CurrentData[month]->longitudes * lati + loni;
                wxInt8 v;
                if (zu_read(f, &v, 1) != 1)
                    goto corrupt;

                if(v == -128)
                    m_CurrentData[month]->data[dim][ind] = NAN;
                else
                    m_CurrentData[month]->data[dim][ind] = (float)v / m_CurrentData[month]->multiplier;
            }
    zu_close(f);
    return;
corrupt:
    delete m_CurrentData[month];
    m_CurrentData[month] = NULL;
    zu_close(f);
    m_sFailedMessage += _("corrupt file: ") + filename + "\n";
missing:
    m_FailedFiles.push_back(filename);
    wxLogMessage(climatology_pi + _("current data file corrupt: ") + filename);
}

void ClimatologyOverlayFactory::AverageCurrentData()
{
    int fmonth;
    for(fmonth=0; fmonth<12; fmonth++)
        if(m_CurrentData[fmonth])
            goto havedata;
    return;

havedata:
    int latitudes = m_CurrentData[fmonth]->latitudes;
    int longitudes = m_CurrentData[fmonth]->longitudes;
    m_CurrentData[12] = new CurrentData(latitudes, longitudes, 1);

    for(int lati = 0; lati < latitudes; lati++)
        for(int loni = 0; loni < longitudes; loni++) {
            double u = 0, v = 0;
            int mcount = 0;
            for(int month=0; month<12; month++) {
                if(!m_CurrentData[month]
                   || m_CurrentData[month]->latitudes != latitudes
                   || m_CurrentData[month]->longitudes != longitudes)
                    continue;
                
                const double month_u = m_CurrentData[month]->Value(U, lati, loni);
                const double month_v = m_CurrentData[month]->Value(V, lati, loni);
                if(isnan(month_u) || isnan(month_v))
                    continue;
                u += month_u;
                v += month_v;
                mcount++;
            }

            static bool nwarned = true;
            if(nwarned && mcount < 12) {
                wxString fmt = " %d ";
                wxLogMessage(climatology_pi + wxString::Format(_("Average Current includes only")
                                                               + fmt + _("months"), mcount));

                nwarned = false;
            }
            if (mcount == 0) {
                m_CurrentData[12]->data[0][lati*longitudes + loni] = NAN;
                m_CurrentData[12]->data[1][lati*longitudes + loni] = NAN;
            } else {
                m_CurrentData[12]->data[0][lati*longitudes + loni] = u / mcount;
                m_CurrentData[12]->data[1][lati*longitudes + loni] = v / mcount;
            }
        }
}

ZUFILE *ClimatologyOverlayFactory::OpenClimatologyDataFile(wxString filename)
{
    ZUFILE *f = NULL;
    wxString path = ClimatologyDataDirectory();
    if(!(f = TryOpenFile(path + filename))) {
        path = ClimatologyDataDirectory();
        if(!(f = TryOpenFile(path + filename)))
            m_FailedFiles.push_back(filename);
    }
    return f;
}

void ClimatologyOverlayFactory::ReadSeaLevelPressureData(wxString filename)
{
    ZUFILE *f = OpenClimatologyDataFile(filename);
    if(!f)
        return;

    const size_t count = 12u*90u*180u;
    std::vector<unsigned char> raw(count * 2u);
    if(zu_read(f, &raw[0], raw.size()) != static_cast<int>(raw.size())) {
        m_FailedFiles.push_back(filename);
        m_sFailedMessage += _("corrupt file: ") + filename + "\n";
        wxLogMessage(climatology_pi + _("slp file truncated"));
    } else {
        for(int j=0; j<90; j++)
            for(int k=0; k<180; k++) {
                long total = 0, totalcount = 0;
                for(int i=0; i<12; i++) {
                    const size_t index = (i*90u + j)*180u + k;
                    const wxInt16 value = static_cast<wxInt16>(DecodeLE16(&raw[index*2]));
                    m_slp[i][j][k] = value;
                    if(value != 32767) {
                        total += value;
                        totalcount++;
                    }
                    if(totalcount)
                        m_slp[12][j][k] = total / totalcount;
                    else
                        m_slp[12][j][k] = 32767;
                }
            }
        m_dlg.m_cbPressure->Enable();
    }
    zu_close(f);
}

void ClimatologyOverlayFactory::ReadSeaSurfaceTemperatureData(wxString filename)
{
    ZUFILE *f = OpenClimatologyDataFile(filename);
    if(!f)
        return;

    std::vector<wxInt8> sst(12u*180u*360u);
    if(zu_read(f, &sst[0], sst.size()) != static_cast<int>(sst.size())) {
        m_FailedFiles.push_back(filename);
        m_sFailedMessage += _("corrupt file: ") + filename + "\n";
        wxLogMessage(climatology_pi + _("sst file truncated"));
    } else {
        for(int j=0; j<180; j++)
            for(int k=0; k<360; k++) {
                long total = 0, totalcount = 0;
                for(int i=0; i<12; i++) {
                    const wxInt8 value = sst[(i*180u + j)*360u + k];
                    if(value == -128)
                        m_sst[i][j][k] = 32767;
                    else {
                        m_sst[i][j][k] = value*200;
                        total += m_sst[i][j][k];
                        totalcount++;
                    }
                    if(totalcount)
                        m_sst[12][j][k] = total / totalcount;
                    else
                        m_sst[12][j][k] = 32767;
                }
            }
        m_dlg.m_cbSeaTemperature->Enable();
    }
    zu_close(f);
}

void ClimatologyOverlayFactory::ReadAirTemperatureData(wxString filename)
{
    ZUFILE *f = OpenClimatologyDataFile(filename);
    if(!f)
        return;

    std::vector<wxInt8> at(12u*90u*180u);
    if(zu_read(f, &at[0], at.size()) != static_cast<int>(at.size())) {
        m_FailedFiles.push_back(filename);
        m_sFailedMessage += _("corrupt file: ") + filename + "\n";
        wxLogMessage(climatology_pi + _("at file truncated"));
    } else {
        for(int j=0; j<90; j++)
            for(int k=0; k<180; k++) {
                long total = 0, totalcount = 0;
                for(int i=0; i<12; i++) {
                    const wxInt8 value = at[(i*90u + j)*180u + k];
                    if(value == -128)
                        m_at[i][j][k] = 32767;
                    else {
                        m_at[i][j][k] = value;
                        total += m_at[i][j][k];
                        totalcount++;
                    }
                    if(totalcount)
                        m_at[12][j][k] = total / totalcount;
                    else
                        m_at[12][j][k] = 32767;
                }
            }
        m_dlg.m_cbAirTemperature->Enable();
    }
    zu_close(f);
}

void ClimatologyOverlayFactory::ReadCloudData(wxString filename)
{
    ZUFILE *f = OpenClimatologyDataFile(filename);
    if(!f)
        return;

    std::vector<wxUint8> cld(12u*90u*180u);
    if(zu_read(f, &cld[0], cld.size()) != static_cast<int>(cld.size())) {
        m_FailedFiles.push_back(filename);
        m_sFailedMessage += _("corrupt file: ") + filename + "\n";
        wxLogMessage(climatology_pi + _("cld file truncated"));
    } else {
        for(int j=0; j<90; j++)
            for(int k=0; k<180; k++) {
                long total = 0, totalcount = 0;
                for(int i=0; i<12; i++) {
                    const wxUint8 value = cld[(i*90u + j)*180u + k];
                    if(value == 255)
                        m_cld[i][j][k] = 32767;
                    else {
                        m_cld[i][j][k] = value*40;
                        total += m_cld[i][j][k];
                        totalcount++;
                    }
                    if(totalcount)
                        m_cld[12][j][k] = total / totalcount;
                    else
                        m_cld[12][j][k] = 32767;
                }
            }
        m_dlg.m_cbCloudCover->Enable();
    }
    zu_close(f);
}

void ClimatologyOverlayFactory::ReadPrecipitationData(wxString filename)
{
    ZUFILE *f = OpenClimatologyDataFile(filename);
    if(!f)
        return;

    std::vector<wxUint8> precip(12u*72u*144u);
    if(zu_read(f, &precip[0], precip.size()) != static_cast<int>(precip.size())) {
        m_FailedFiles.push_back(filename);
        m_sFailedMessage += _("corrupt file: ") + filename + "\n";
        wxLogMessage(climatology_pi + _("precip file truncated"));
    } else {
        for(int j=0; j<72; j++)
            for(int k=0; k<144; k++) {
                long total = 0, totalcount = 0;
                for(int i=0; i<12; i++) {
                    const wxUint8 value = precip[(i*72u + j)*144u + k];
                    if(value == 255)
                        m_precip[i][j][k] = 32767;
                    else {
                        m_precip[i][j][k] = value*100;
                        total += m_precip[i][j][k];
                        totalcount++;
                    }
                    if(totalcount)
                        m_precip[12][j][k] = total / totalcount;
                    else
                        m_precip[12][j][k] = 32767;
                }
            }
        m_dlg.m_cbPrecipitation->Enable();
    }
    zu_close(f);
}

void ClimatologyOverlayFactory::ReadRelativeHumidityData(wxString filename)
{
    ZUFILE *f = OpenClimatologyDataFile(filename);
    if(!f)
        return;

    std::vector<wxUint8> rhum(12u*180u*360u);
    if(zu_read(f, &rhum[0], rhum.size()) != static_cast<int>(rhum.size())) {
        m_FailedFiles.push_back(filename);
        m_sFailedMessage += _("corrupt file: ") + filename + "\n";
        wxLogMessage(climatology_pi + _("relative humidity file truncated"));
    } else {
        for(int j=0; j<180; j++)
            for(int k=0; k<360; k++) {
                long total = 0, totalcount = 0;
                for(int i=0; i<12; i++) {
                    const wxUint8 value = rhum[(i*180u + j)*360u + k];
                    if(value == 255)
                        m_rhum[i][j][k] = 32767;
                    else {
                        m_rhum[i][j][k] = value;
                        total += m_rhum[i][j][k];
                        totalcount++;
                    }
                    if(totalcount)
                        m_rhum[12][j][k] = total / totalcount;
                    else
                        m_rhum[12][j][k] = 32767;
                }
            }
        m_dlg.m_cbRelativeHumidity->Enable();
    }
    zu_close(f);
}

void ClimatologyOverlayFactory::ReadLightningData(wxString filename)
{
    ZUFILE *f = OpenClimatologyDataFile(filename);
    if(!f)
        return;

    std::vector<wxUint8> lightn(12u*180u*360u);
    if(zu_read(f, &lightn[0], lightn.size()) != static_cast<int>(lightn.size())) {
        m_FailedFiles.push_back(filename);
        m_sFailedMessage += _("corrupt file: ") + filename + "\n";
        wxLogMessage(climatology_pi + _("lightning file truncated"));
    } else {
        for(int j=0; j<180; j++)
            for(int k=0; k<360; k++) {
                long total = 0, totalcount = 0;
                for(int i=0; i<12; i++) {
                    m_lightn[i][j][k] = lightn[(i*180u + j)*360u + k];
                    total += m_lightn[i][j][k];
                    totalcount++;
                }
                m_lightn[12][j][k] = total / totalcount;
            }
        m_dlg.m_cbLightning->Enable();
    }
    zu_close(f);
}

void ClimatologyOverlayFactory::ReadSeaDepthData(wxString filename)
{
    ZUFILE *f = OpenClimatologyDataFile(filename);
    if(!f)
        return;

    std::vector<wxInt8> seadepth(180u*360u);
    if(zu_read(f, &seadepth[0], seadepth.size()) != static_cast<int>(seadepth.size())) {
        m_FailedFiles.push_back(filename);
        m_sFailedMessage += _("corrupt file: ") + filename + "\n";
        wxLogMessage(climatology_pi + _("seadepth file truncated"));
    } else {
        for(int j=0; j<180; j++)
            for(int k=0; k<360; k++)
                if(seadepth[j*360u+k] == -128)
                    m_seadepth[j][k] = 32767;
                else
                    m_seadepth[j][k] = seadepth[j*360u+k];

        m_dlg.m_cbSeaDepth->Enable();
    }
    zu_close(f);
}

bool ClimatologyOverlayFactory::ReadCycloneData(wxString filename, std::list<Cyclone*> &cyclones, bool south)
{
    ZUFILE *f;
    wxString path = ClimatologyDataDirectory();
    if(!(f = TryOpenFile(path + filename))) {
        path = ClimatologyDataDirectory();
        if(!(f = TryOpenFile(path + filename)))
            goto missing;
    }

    wxUint16 lyear, llastmonth;
    Cyclone *cyclone;
    while(ReadLE16(f, lyear)) {
#ifdef __MSVC__
        if(lyear < 1972)
            lyear = 1972;
#endif
        cyclone = new Cyclone;
        llastmonth = 0;

        wxUint8 wk;
        wxUint16 press;
        CycloneState::State lastcyclonestate = CycloneState::UNKNOWN;
        CycloneDateTime lastdatetime(1, 1, 1900, 0);
        wxInt16 lastlat=-10000, lastlon=-10000;
        for(;;) {
            signed char state;
            if(zu_read(f, &state, sizeof state) != sizeof state)
                goto corrupted;

            if(state == -128)
                break;

            CycloneState::State cyclonestate;
            switch(state) {
            case '*': cyclonestate = CycloneState::TROPICAL; break;
            case 'S': cyclonestate = CycloneState::SUBTROPICAL; break;
            case 'E': cyclonestate = CycloneState::EXTRATROPICAL; break;
            case 'W': cyclonestate = CycloneState::WAVE; break;
            case 'L': cyclonestate = CycloneState::REMANENT; break;
            case 'D': case 'X': cyclonestate = CycloneState::UNKNOWN; break;
            default: goto corrupted;
            }

            char lday, lmonth;
            if(zu_read(f, &lday, sizeof lday) != sizeof lday ||
               zu_read(f, &lmonth, sizeof lmonth) != sizeof lmonth)
                goto corrupted;

            if(lmonth < llastmonth)
                lyear++;
            llastmonth = lmonth;

            wxDateTime::Month month = (wxDateTime::Month)(lmonth-1);
            int day = lday/4, hour = (lday%4)*6;
            if(lmonth < 1 || lmonth > 12 ||
               day < 1 || day > wxDateTime::GetNumberOfDays(month, lyear) ||
               hour < 0 || hour >= 24)
                goto corrupted;

            wxInt16 lat, lon;
            if(!ReadLEI16(f, lat) || !ReadLEI16(f, lon))
                goto corrupted;

            // make sure it's in range
            if(std::abs((double)lat/10) >= 90 || (double)lon/10 > 15 || (double)lon/10 < -360)
                goto corrupted;

            if(lastlat != -10000) {
                cyclone->states.push_back
                    (new CycloneState(lastcyclonestate, lastdatetime,
                                      (south ? -1 : 1) * (double)lastlat/10, (double)lastlon/10,
                                      (south ? -1 : 1) * (double)lat/10, (double)lon/10,
                                      wk, press));
            }

            lastcyclonestate = cyclonestate;
            lastdatetime = CycloneDateTime(day, month, lyear, hour);
            lastlat = lat, lastlon = lon;
                
            if(zu_read(f, &wk, sizeof wk) != sizeof wk || !ReadLE16(f, press))
                goto corrupted;
        }
        cyclones.push_back(cyclone);
    }

    zu_close(f);
    return true;

corrupted:
    delete cyclone;
    m_sFailedMessage += _("corrupt file: ") + filename + "\n";
    wxLogMessage(climatology_pi + _("cyclone data corrupt: ") + filename
                 + wxString::Format(" at %ld", zu_tell(f)));
    zu_close(f);
missing:
    m_FailedFiles.push_back(filename);
    return false;
}

void ClimatologyOverlayFactory::BuildCycloneCache()
{
    const climatology::CycloneFilterState filters =
        m_dlg.CaptureRenderState().cyclone_filter;
    if(m_cycloneJob) {
        if(SameCycloneFilters(m_cycloneJob->filters, filters) &&
           !m_cycloneJob->cancel.load(std::memory_order_acquire))
            return;
        m_cycloneJob->cancel.store(true, std::memory_order_release);
        m_requestedCycloneFilters = filters;
        m_cycloneRequestPending = true;
        return;
    }
    StartCycloneCacheJob(filters);
}

void ClimatologyOverlayFactory::StartCycloneCacheJob(
    const climatology::CycloneFilterState &filters)
{
    if(!m_snapshot || !m_snapshot->availability.cyclones)
        return;

    m_cycloneJob.reset(new ClimatologyCycloneJob);
    ClimatologyCycloneJob *job = m_cycloneJob.get();
    job->filters = filters;
    const std::shared_ptr<const climatology::ClimatologyDatasetSnapshot>
        snapshot = m_snapshot;
    try {
      job->worker = std::thread([snapshot, job]() {
        try {
            climatology::CycloneFilterState filters = job->filters;
#ifdef __OCPN__ANDROID__
            filters.start_utc = std::numeric_limits<std::int64_t>::min();
            filters.end_utc = std::numeric_limits<std::int64_t>::max();
#endif
            climatology::CycloneIndexResult result =
                climatology::BuildCycloneIndex(
                    snapshot, filters, &job->cancel);
            job->cache.swap(result.index);
            job->succeeded = result.completed;
        } catch(...) {
            job->succeeded = false;
        }
        job->done.store(true, std::memory_order_release);
      });
    } catch(...) {
        m_cycloneJob.reset();
    }
}

static double strtod_nan(const char *str)
{
    if(!str)
        return NAN;

    double ret;
    if(wxString::FromUTF8(str).ToDouble(&ret))
        return ret;

    return NAN;
}

bool ClimatologyOverlayFactory::ReadElNinoYears(wxString filename)
{
    char line[128];
    int header = 1;
    wxString path = ClimatologyDataDirectory();
    FILE *f = fopen((path + filename).mb_str(), "r");
    if(!f) {
        wxLogMessage(climatology_pi + _("failed to open file: ") + filename);
        m_FailedFiles.push_back(filename);
        return false;
    }

    std::map<int, ElNinoYear> parsed;
    bool valid = true;
    while(fgets(line, sizeof line, f)) {
        if(header)
            header--;
        else {
            std::istringstream row(line);
            int year;
            ElNinoYear elninoyear;
            if(!(row >> year)) {
                valid = false;
                break;
            }
            for(int i=0; i<12; i++) {
                std::string value;
                if(!(row >> value)) {
                    valid = false;
                    break;
                }
                elninoyear.months[i] = strtod_nan(value.c_str());
                if(isnan(elninoyear.months[i])) {
                    valid = false;
                    break;
                }
            }
            if(!valid)
                break;
            parsed[year] = elninoyear;
        }
    }
    fclose(f);
    if(!valid) {
        m_sFailedMessage += _("corrupt file: ") + filename + "\n";
        wxLogMessage(climatology_pi + _("El Nino data corrupt: ") + filename);
        m_FailedFiles.push_back(filename);
        return false;
    }
    m_ElNinoYears.swap(parsed);
    return true;
}

bool ClimatologyOverlayFactory::CreateGLTexture(ClimatologyOverlay &O,
                                                int setting, int month,
                                                PlugIn_ViewPort &vp)
{
    (void)vp;
    if(!texture_format)
        return false;

    double s;
    double latoff = 0, lonoff = 0;
    switch(setting) {
    case ClimatologyOverlaySettings::WIND:
        if(!m_snapshot || !m_snapshot->wind.available[month]) return false;
        s = m_snapshot->wind.geometry.columns / 360.0;
        latoff = 90.0/m_snapshot->wind.geometry.rows;
        lonoff = 180.0/m_snapshot->wind.geometry.columns;
        break;
    case ClimatologyOverlaySettings::CURRENT: s = 3;  break;
    case ClimatologyOverlaySettings::SLP:     s = .5; break;
    case ClimatologyOverlaySettings::AT:      s = .5; break;
    case ClimatologyOverlaySettings::CLOUD:   s = .5; break;
    default: s=1;
    }

    int width = s*360+1;
    int height = s*360;

    if(m_readyTexture) {
        if(m_readyTexture->setting == setting &&
           m_readyTexture->month == month &&
           m_readyTexture->width == width &&
           m_readyTexture->height == height) {
            GLuint texture;
            glGenTextures(1, &texture);
            glBindTexture(texture_format, texture);
            glTexParameteri(texture_format, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glTexParameteri(texture_format, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glTexParameteri(texture_format, GL_TEXTURE_WRAP_S,
                s_bnoglrepeat ? GL_CLAMP_TO_EDGE : GL_REPEAT);
            glTexParameteri(texture_format, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
            glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
            glTexImage2D(texture_format, 0, GL_RGBA, width, height, 0,
                         GL_RGBA, GL_UNSIGNED_BYTE,
                         m_readyTexture->pixels.data());
            O.m_iTexture = texture;
            O.m_width = width - 1;
            O.m_height = height;
            O.m_latoff = m_readyTexture->latoff;
            O.m_lonoff = m_readyTexture->lonoff;
            m_readyTexture.reset();
            return true;
        }
        m_readyTexture.reset();
    }

    if(m_textureJob) {
        return false;
    }

    m_textureJob.reset(new ClimatologyTextureJob);
    ClimatologyTextureJob *job = m_textureJob.get();
    job->setting = setting;
    job->month = month;
    job->width = width;
    job->height = height;
    job->latoff = latoff;
    job->lonoff = lonoff;
    const std::shared_ptr<const climatology::ClimatologyDatasetSnapshot>
        snapshot = m_snapshot;
    try {
      job->worker = std::thread([job, snapshot, s]() {
        try {
            climatology::TextureRasterRequest request;
            request.field = static_cast<climatology::DatasetField>(
                job->setting);
            request.color_field = job->setting;
            request.month = job->month;
            request.width = job->width;
            request.height = job->height;
            request.samples_per_degree = s;
            request.latitude_offset = job->latoff;
            request.longitude_offset = job->lonoff;
            climatology::TextureRasterResult result =
                climatology::BuildTextureRaster(
                    snapshot, request, GraphicPixel, &job->cancel);
            job->pixels.swap(result.pixels);
            job->succeeded = result.completed;
        } catch(...) {
            job->succeeded = false;
        }
        job->done.store(true, std::memory_order_release);
      });
    } catch(...) {
        m_textureJob.reset();
    }
    return false;
}

static inline void glTexCoord2d_2(int multitexturing, double x, double y)
{
#ifndef __ANDROID__
#ifndef __APPLE__
    if(multitexturing) {
        s_glMultiTexCoord2dARB(GL_TEXTURE0_ARB, x, y);
        s_glMultiTexCoord2dARB(GL_TEXTURE1_ARB, x, y);
    } else
#endif
        glTexCoord2d(x, y);
#endif
}

void ClimatologyOverlayFactory::DrawGLTexture( ClimatologyOverlay &O1, ClimatologyOverlay &O2,
                                               double dpos, PlugIn_ViewPort &vp, double transparency)
{ 
    if( !O1.m_iTexture || !O2.m_iTexture )
        return;

    if(vp.m_projection_type != PI_PROJECTION_MERCATOR)
        return;

#ifdef __ANDROID__      //TODO  Implement shader structure
#if 0
        int w = vp.pix_width, h = vp.pix_height;

        double lat[4], lon[4];
        GetCanvasLLPix( &vp, wxPoint(0, 0), &lat[0], &lon[0] );
        GetCanvasLLPix( &vp, wxPoint(w, 0), &lat[1], &lon[1] );
        GetCanvasLLPix( &vp, wxPoint(w, h), &lat[2], &lon[2] );
        GetCanvasLLPix( &vp, wxPoint(0, h), &lat[3], &lon[3] );

        for(int i = 0; i < 4; i++) {
            if(lon[i] - vp.clon > 180)
                lon[i] -= 360;
            else if(lon[i] - vp.clon < -180)
                lon[i] += 360;

            // normalize
            lon[i] = lon[i] / 360.0;

            // mercator conversion
            double s1 = sin(deg2rad(lat[i]));
            double y1 = .5 * log((1 + s1) / (1 - s1));
            lat[i] = (1 + y1/M_PI)/2;
        }
    
    glEnable(texture_format);
    glBindTexture(texture_format, O1.m_iTexture);

    float uv[8];
    float coords[8];
    
    //normal uv
    uv[0] = lon[0]; uv[1] = lat[0]; uv[2] = lon[1]; uv[3] = lat[1];
    uv[4] = lon[2]; uv[5] = lat[2]; uv[6] = lon[3]; uv[7] = lat[3];
        
    coords[0] = 0; coords[1] = 0; coords[2] = w; coords[3] = 0;
    coords[4] = w; coords[5] = h; coords[6] = 0; coords[7] = h;
    
    glUseProgram( pi_texture_2DA_shader_program );
    
    // Get pointers to the attributes in the program.
    GLint mPosAttrib = glGetAttribLocation( pi_texture_2DA_shader_program, "aPos" );
    GLint mUvAttrib  = glGetAttribLocation( pi_texture_2DA_shader_program, "aUV" );
    
    // Set up the texture sampler to texture unit 0
    GLint texUni = glGetUniformLocation( pi_texture_2DA_shader_program, "uTex" );
    glUniform1i( texUni, 0 );
    
    // Disable VBO's (vertex buffer objects) for attributes.
    glBindBuffer( GL_ARRAY_BUFFER, 0 );
    glBindBuffer( GL_ELEMENT_ARRAY_BUFFER, 0 );
    
    // Set the attribute mPosAttrib with the vertices in the screen coordinates...
    glVertexAttribPointer( mPosAttrib, 2, GL_FLOAT, GL_FALSE, 0, coords );
    // ... and enable it.
    glEnableVertexAttribArray( mPosAttrib );

    float colorv[4] = {0, 0, 0, -(float)transparency};

    GLint colloc = glGetUniformLocation(pi_texture_2DA_shader_program,"color");
    glUniform4fv(colloc, 1, colorv);

    // Set the attribute mUvAttrib with the vertices in the GL coordinates...
    glVertexAttribPointer( mUvAttrib, 2, GL_FLOAT, GL_FALSE, 0, uv );
    // ... and enable it.
    glEnableVertexAttribArray( mUvAttrib );
    
    // Rotate 
    float angle = 0;
    mat4x4 I, Q;
    mat4x4_identity(I);
    mat4x4_rotate_Z(Q, I, angle);
    
    // Translate
    Q[3][0] = 0;
    Q[3][1] = 0;
    
    GLint matloc = glGetUniformLocation(pi_texture_2DA_shader_program,"TransformMatrix");
    glUniformMatrix4fv( matloc, 1, GL_FALSE, (const GLfloat*)Q); 
    
    // Select the active texture unit.
    glActiveTexture( GL_TEXTURE0 );

    float co1[8];
    co1[0] = coords[0];
    co1[1] = coords[1];
    co1[2] = coords[2];
    co1[3] = coords[3];
    co1[4] = coords[6];
    co1[5] = coords[7];
    co1[6] = coords[4];
    co1[7] = coords[5];
    
    float tco1[8];
    tco1[0] = uv[0];
    tco1[1] = uv[1];
    tco1[2] = uv[2];
    tco1[3] = uv[3];
    tco1[4] = uv[6];
    tco1[5] = uv[7];
    tco1[6] = uv[4];
    tco1[7] = uv[5];
    
    glVertexAttribPointer( mPosAttrib, 2, GL_FLOAT, GL_FALSE, 0, co1 );
    glVertexAttribPointer( mUvAttrib, 2, GL_FLOAT, GL_FALSE, 0, tco1 );
    
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    
    glDisable(texture_format);
#endif
#else
    int multitexturing;
    if(&O1 == &O2)
        multitexturing = 0;
    else
        multitexturing = s_multitexturing;

#if defined(__ANDROID__) || defined(__APPLE__)
    multitexturing = 0;
#endif

#if !defined(__ANDROID__) && !defined(__APPLE__)
    if(multitexturing) {
        s_glActiveTextureARB (GL_TEXTURE0_ARB);
        glEnable(texture_format);
        glBindTexture(texture_format, O2.m_iTexture);
        glTexEnvf(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
        s_glActiveTextureARB (GL_TEXTURE1_ARB); 
    } else
#endif
        glTexEnvf(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);

    glEnable(texture_format);
    glBindTexture(texture_format, O1.m_iTexture);

#if !defined(__ANDROID__) && !defined(__APPLE__)
    if(multitexturing) {
        float fpos = dpos;
        GLfloat constColor[4] = {0, 0, 0, fpos};

        glTexEnvfv(GL_TEXTURE_ENV, GL_TEXTURE_ENV_COLOR, constColor);

        glTexEnvf(GL_TEXTURE_ENV, GL_COMBINE_RGB_ARB, GL_INTERPOLATE_ARB);
        glTexEnvf(GL_TEXTURE_ENV, GL_COMBINE_ALPHA_ARB, GL_INTERPOLATE_ARB);

        glTexEnvf(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_COMBINE_ARB);

        glTexEnvf(GL_TEXTURE_ENV, GL_SOURCE0_RGB_ARB, GL_TEXTURE);
        glTexEnvf(GL_TEXTURE_ENV, GL_SOURCE0_ALPHA_ARB, GL_TEXTURE);
        glTexEnvf(GL_TEXTURE_ENV, GL_OPERAND0_RGB_ARB, GL_SRC_COLOR);
        glTexEnvf(GL_TEXTURE_ENV, GL_OPERAND0_ALPHA_ARB, GL_SRC_ALPHA);

        glTexEnvf(GL_TEXTURE_ENV, GL_SOURCE1_RGB_ARB, GL_PREVIOUS_ARB);
        glTexEnvf(GL_TEXTURE_ENV, GL_SOURCE1_ALPHA_ARB, GL_PREVIOUS_ARB);
        glTexEnvf(GL_TEXTURE_ENV, GL_OPERAND1_RGB_ARB, GL_SRC_COLOR);
        glTexEnvf(GL_TEXTURE_ENV, GL_OPERAND1_ALPHA_ARB, GL_SRC_ALPHA);

        if(multitexturing > 1) {
            s_glActiveTextureARB (GL_TEXTURE2_ARB);

            glEnable(texture_format);
            glBindTexture(texture_format, O2.m_iTexture);

//            constColor[3] = 1;//1 - transparency;
//            glTexEnvfv(GL_TEXTURE_ENV, GL_TEXTURE_ENV_COLOR, constColor);
        
            glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_COMBINE_ARB);
            glTexEnvi(GL_TEXTURE_ENV, GL_COMBINE_RGB_ARB, GL_REPLACE);
            glTexEnvf(GL_TEXTURE_ENV, GL_COMBINE_ALPHA_ARB, GL_MODULATE);

            glTexEnvf(GL_TEXTURE_ENV, GL_SOURCE0_RGB_ARB, GL_PREVIOUS_ARB);
            glTexEnvf(GL_TEXTURE_ENV, GL_SOURCE0_ALPHA_ARB, GL_PREVIOUS_ARB);
            glTexEnvf(GL_TEXTURE_ENV, GL_OPERAND0_RGB_ARB, GL_SRC_COLOR);
            glTexEnvf(GL_TEXTURE_ENV, GL_OPERAND0_ALPHA_ARB, GL_SRC_ALPHA);

            glTexEnvf(GL_TEXTURE_ENV, GL_SOURCE1_RGB_ARB, GL_CONSTANT_ARB);
            glTexEnvf(GL_TEXTURE_ENV, GL_SOURCE1_ALPHA_ARB, GL_CONSTANT_ARB);
            glTexEnvf(GL_TEXTURE_ENV, GL_SOURCE1_RGB_ARB, GL_PRIMARY_COLOR_ARB);
            glTexEnvf(GL_TEXTURE_ENV, GL_SOURCE1_ALPHA_ARB, GL_PRIMARY_COLOR_ARB);
        }
    }
#endif
    glColor4f(1, 1, 1, 1 - transparency);

    if(s_bnoglrepeat) {
        // this should be replaced by vp box
        double x = vp.rv_rect.x, y = vp.rv_rect.y;
        double w = x+vp.rv_rect.width, h = y+vp.rv_rect.height;

        double tx[2], ty[2];
        double r = vp.rotation;
        vp.rotation = 0;
        GetCanvasLLPix( &vp, wxPoint(x, y), &ty[0], &tx[0] );
        GetCanvasLLPix( &vp, wxPoint(w, h), &ty[1], &tx[1] );
        vp.rotation = r;

        for(int i = 0; i < 2; i++) {
            tx[i] -= O1.m_lonoff;
            ty[i] -= O1.m_latoff;

            // normalize
            tx[i] = tx[i] / 360.0 * (O1.m_width-1)/O1.m_width;

            // mercator conversion
            double s1 = sin(deg2rad(ty[i]));
            double y1 = .5 * log((1 + s1) / (1 - s1));
            ty[i] = (1 + y1/M_PI)/2;

            if(texture_format == GL_TEXTURE_RECTANGLE_ARB) {
                tx[i] *= O1.m_width;
                ty[i] *= O1.m_height;
            }
        }
    
        double tw = (texture_format == GL_TEXTURE_RECTANGLE_ARB) ? O1.m_width : 1;
        double s = .5 * tw/O1.m_width;

        if(tx[0] > tx[1])
            tx[1] += tw;

        glPushMatrix();
        glTranslated(vp.pix_width/2.0, vp.pix_height/2.0, 0);
        glRotated(vp.rotation*180/M_PI, 0, 0, 1);
        glTranslated(-vp.pix_width/2.0, -vp.pix_height/2.0, 0);

        glBegin(GL_QUADS);
        if(tx[1]*tx[0]<0) { // meridian crosses rv_rect
            double t = x + (w-x)*tx[0]/(tx[0] - tx[1]);

            glTexCoord2d_2(multitexturing,       s, ty[0]), glVertex2d(t, y);
            glTexCoord2d_2(multitexturing, tx[1]+s, ty[0]), glVertex2d(w, y);
            glTexCoord2d_2(multitexturing, tx[1]+s, ty[1]), glVertex2d(w, h);
            glTexCoord2d_2(multitexturing,       s, ty[1]), glVertex2d(t, h);

            w = t;
            tx[0] += tw-2*s;
            tx[1] = tw-2*s;
        } else {
            if(tx[0] < 0)
                tx[0] += tw-2*s;
            if(tx[1] < 0)
                tx[1] += tw-2*s;
        }

        glTexCoord2d_2(multitexturing, tx[0]+s, ty[0]), glVertex2d(x, y);
        glTexCoord2d_2(multitexturing, tx[1]+s, ty[0]), glVertex2d(w, y);
        glTexCoord2d_2(multitexturing, tx[1]+s, ty[1]), glVertex2d(w, h);
        glTexCoord2d_2(multitexturing, tx[0]+s, ty[1]), glVertex2d(x, h);
        glEnd();

        glPopMatrix();
    } else {
        // this only works if npot textures support GL_REPEAT, and of course
        // for mercator projections only
        // it's kind of cool because uses a single quad exactly filling viewport
        // and modifys tex coordinates
        int x = 0, y = 0;
        int w = vp.pix_width, h = vp.pix_height;

        double lat[4], lon[4];
        GetCanvasLLPix( &vp, wxPoint(x, y), &lat[0], &lon[0] );
        GetCanvasLLPix( &vp, wxPoint(w, y), &lat[1], &lon[1] );
        GetCanvasLLPix( &vp, wxPoint(w, h), &lat[2], &lon[2] );
        GetCanvasLLPix( &vp, wxPoint(x, h), &lat[3], &lon[3] );

        for(int i = 0; i < 4; i++) {
            if(lon[i] - vp.clon > 180)
                lon[i] -= 360;
            else if(lon[i] - vp.clon < -180)
                lon[i] += 360;

            // normalize
            lon[i] = lon[i] / 360.0;

            // mercator conversion
            double s1 = sin(deg2rad(lat[i]));
            double y1 = .5 * log((1 + s1) / (1 - s1));
            lat[i] = (1 + y1/M_PI)/2;
        }

        glBegin(GL_QUADS);
        glTexCoord2d_2(multitexturing, lon[0], lat[0]), glVertex2i(x, y);
        glTexCoord2d_2(multitexturing, lon[1], lat[1]), glVertex2i(w, y);
        glTexCoord2d_2(multitexturing, lon[2], lat[2]), glVertex2i(w, h);
        glTexCoord2d_2(multitexturing, lon[3], lat[3]), glVertex2i(x, h);
        glEnd();
    }

#if !defined(__ANDROID__) && !defined(__APPLE__)
    if(multitexturing) {
        if(multitexturing > 1) {
            glDisable(texture_format);
            s_glActiveTextureARB (GL_TEXTURE1_ARB);
        }
        glDisable(texture_format);
        s_glActiveTextureARB (GL_TEXTURE0_ARB);
    }
#endif

    glDisable(texture_format);
#endif
}

/* give value for y at a given x location on a segment */
static double interp_value(double v0, double v1, double d)
{
    return climatology::InterpolateMissing(v0, v1, d);
}

// interpolate two angles in range +- PI, with resulting angle in the same range
static double interp_angle(double a0, double a1, double d)
{
    return climatology::InterpolateAngleRadians(a0, a1, d);
}

static double ArrayValue(wxInt16 *a, int index)
{
    int v = a[index];
    if(v == 32767)
        return NAN;
    return v;
}

static double InterpArray(double x, double y, wxInt16 *a, int rows, int columns)
{
    return climatology::Bilinear(rows, columns, x, y,
        [a, columns](size_t row, size_t column) {
            return ArrayValue(a, static_cast<int>(row * columns + column));
        });
}

double WindData::WindPolar::Value(enum Coord coord, int dir_cnt)
{
    if(gale == 255)
        return NAN;

    if(coord == DIRECTION) // maybe should do most likely here rather than vector average?
        return atan2(Value(U, dir_cnt), Value(V, dir_cnt));

    int totald = 0, totals = 0;
    for(int i=0; i<dir_cnt; i++) {
        totald += directions[i];

        double mul = 0;
        switch(coord) {
        case U: mul = sin(i*2*M_PI/dir_cnt); break;
        case V: mul = cos(i*2*M_PI/dir_cnt); break;
        case MAG: mul = 1; break;
        default: printf("error, invalid coord: %d\n", coord);
        }

        totals += mul*speeds[i]*directions[i];
    }
    return totald ? (double)totals / totald : NAN;
}

double CurrentData::Value(enum Coord coord, int xi, int yi)
{
    if(xi < 0 || xi >= latitudes || yi < 0 || yi >= longitudes)
        return NAN;

    double u = data[0][xi*longitudes + yi], v = data[1][xi*longitudes + yi];
    switch(coord) {
    case U: return u;
    case V: return v;
    case MAG: return hypot(u, v);
    case DIRECTION: return !u && !v ? NAN : atan2(u, v);
    default: printf("error, invalid coord: %d\n", coord);
    }
    return NAN;
}

double WindData::InterpWind(enum Coord coord, double x, double y)
{
    if(!std::isfinite(x) || !std::isfinite(y) || x < -90.0 || x > 90.0)
        return NAN;
    double latoff = 90.0/latitudes, lonoff = 180.0/longitudes;

    double xi = latitudes*(.5 + (x - latoff)/180.0);
    double yi = longitudes*positive_degrees(y - lonoff)/360.0;

    int h = longitudes, d = dir_cnt;

    if(yi<0) yi+=h;

    if(xi < 0) xi = 0;
    if(xi > latitudes - 1) xi = latitudes - 1;
    int x0 = floor(xi), x1 = x0+1;
    int y0 = floor(yi), y1 = y0+1;
    int y1v = y1;
    if(x1 == latitudes)
        x1 = x0;
    if(y1v == h) y1v = 0;

    double v00 = data[x0*h + y0].Value(coord, d), v01 = data[x0*h + y1v].Value(coord, d);
    double v10 = data[x1*h + y0].Value(coord, d), v11 = data[x1*h + y1v].Value(coord, d);

    if(coord == DIRECTION) {
        double a0 = interp_angle(v00, v01, yi-y0);
        double a1 = interp_angle(v10, v11, yi-y0);
        return      interp_angle(a0,  a1,  xi-x0 ) * 180/M_PI;
    }

    double v0 = interp_value(v00, v01, yi-y0);
    double v1 = interp_value(v10, v11, yi-y0);
    return      interp_value(v0,  v1,  xi-x0 ) / speed_multiplier;
}

double CurrentData::InterpCurrent(enum Coord coord, double x, double y)
{
    if(!std::isfinite(x) || !std::isfinite(y) || x < -80.0 || x > 80.0)
        return NAN;
    y = positive_degrees(y);
    if(coord == MAG || coord == DIRECTION) {
        const double u = InterpCurrent(U, x, y);
        const double v = InterpCurrent(V, x, y);
        if(isnan(u) || isnan(v))
            return NAN;
        if(coord == MAG)
            return hypot(u, v);
        return (!u && !v) ? NAN : atan2(u, v) * 180/M_PI;
    }
    double xi = (latitudes-1)*(.5 - x/160.0);
    double yi = longitudes*y/360.0;
    int h = longitudes;

    int x0 = floor(xi), x1 = x0+1;
    int y0 = floor(yi), y1 = y0+1;
    int y1v = y1;
    if(x1 == latitudes) x1 = x0;
    if(y1v == h) y1v = 0;

    double v00 = Value(coord, x0, y0), v01 = Value(coord, x0, y1v);
    double v10 = Value(coord, x1, y0), v11 = Value(coord, x1, y1v);

    double v0 = interp_value(v00, v01, yi-y0);
    double v1 = interp_value(v10, v11, yi-y0);
    return      interp_value(v0,  v1,  xi-x0 );
}

static double interp_table_value(double x, double x1, double x2, double y1, double y2)
{
    if(x == x1)
        return y1;
    if(x == x2)
        return y2;
    return (y2 - y1)*(x - x1)/(x2 - x1) + y1;
}

static double InterpTable(double ind, const double table[], int tablesize)
{
    int ind1 = floor(ind), ind2 = ceil(ind);
    if(ind1 < 0)
        return table[0];
   if(ind2 >= tablesize)
        return table[tablesize - 1];

    return interp_table_value(ind, ind1, ind2, table[ind1], table[ind2]);
}
 
double ClimatologyOverlayFactory::getValueMonth(enum Coord coord, int setting,
                                                double lat, double lon, int month)
{
    if(!IsReady() || setting < ClimatologyOverlaySettings::WIND ||
       setting > ClimatologyOverlaySettings::SEADEPTH)
        return NAN;

    if(coord != MAG &&
       setting != ClimatologyOverlaySettings::WIND &&
       setting != ClimatologyOverlaySettings::CURRENT)
        return NAN;

    if(isnan(lat) || isnan(lon))
        return NAN;

    climatology::QueryComponent component = climatology::QueryComponent::Magnitude;
    if(coord == U) component = climatology::QueryComponent::Eastward;
    else if(coord == V) component = climatology::QueryComponent::Northward;
    else if(coord == DIRECTION) component = climatology::QueryComponent::Direction;
    return m_query->ValueMonth(
        static_cast<climatology::DatasetField>(setting), component,
        lat, lon, month);
}

double ClimatologyOverlayFactory::getValue(enum Coord coord, int setting,
                                           double lat, double lon, const wxDateTime *date)
{
    int month, nmonth;
    double dpos;
    GetDateInterpolation(date, month, nmonth, dpos);
    if(!IsReady() || setting < ClimatologyOverlaySettings::WIND ||
       setting > ClimatologyOverlaySettings::SEADEPTH)
        return NAN;
    climatology::QueryComponent component = climatology::QueryComponent::Magnitude;
    if(coord == U) component = climatology::QueryComponent::Eastward;
    else if(coord == V) component = climatology::QueryComponent::Northward;
    else if(coord == DIRECTION) component = climatology::QueryComponent::Direction;
    climatology::MonthPosition position;
    position.month = month;
    position.next_month = nmonth;
    position.current_weight = dpos;
    return m_query->Value(static_cast<climatology::DatasetField>(setting),
                          component, lat, lon, position);
}

double ClimatologyOverlayFactory::getCurCalibratedValue(enum Coord coord, int setting, double lat, double lon)
{
    double v = getCurValue(coord, setting, lat, lon);
    if(coord == DIRECTION)
        return v;

    return m_Settings.CalibrateValue(setting, v);
}

double ClimatologyOverlayFactory::getCalibratedValueMonth(enum Coord coord, int setting, double lat, double lon, int month)
{
    double v = getValueMonth(coord, setting, lat, lon, month);
    if(coord == DIRECTION)
        return v;

    return m_Settings.CalibrateValue(setting, v);
}

double ClimatologyOverlayFactory::GetMin(int setting)
{
    switch(setting) {
    case ClimatologyOverlaySettings::WIND: return 0;
    case ClimatologyOverlaySettings::CURRENT: return 0;
    case ClimatologyOverlaySettings::SLP: return 920;
    case ClimatologyOverlaySettings::SST: return 0;
    case ClimatologyOverlaySettings::AT:  return -50;
    case ClimatologyOverlaySettings::CLOUD: return 0;
    case ClimatologyOverlaySettings::PRECIPITATION:  return 0;
    case ClimatologyOverlaySettings::RELATIVE_HUMIDITY:  return 0;
    case ClimatologyOverlaySettings::LIGHTNING:  return 0;
    case ClimatologyOverlaySettings::SEADEPTH:  return 0;
    default: return 0;
    }
}

double ClimatologyOverlayFactory::GetMax(int setting)
{
    switch(setting) {
    case ClimatologyOverlaySettings::WIND: return 100;
    case ClimatologyOverlaySettings::CURRENT: return 10;
    case ClimatologyOverlaySettings::SLP: return 1080;
    case ClimatologyOverlaySettings::SST: return 35;
    case ClimatologyOverlaySettings::AT:  return 50;
    case ClimatologyOverlaySettings::CLOUD: return 8;
    case ClimatologyOverlaySettings::PRECIPITATION:  return 1000;
    case ClimatologyOverlaySettings::RELATIVE_HUMIDITY:  return 100;
    case ClimatologyOverlaySettings::LIGHTNING:  return 100; // ???
    case ClimatologyOverlaySettings::SEADEPTH:  return 40;
    default: return NAN;
    }
}

#define EPS 2e-14
#define EPS2 2e-7

static inline int TestIntersectionXY(double x1, double y1, double x2, double y2,
                              double x3, double y3, double x4, double y4)
{
    double ax = x2 - x1, ay = y2 - y1;
    double bx = x3 - x4, by = y3 - y4;
    double cx = x1 - x3, cy = y1 - y3;

    double denom = ay * bx - ax * by;

    if(fabs(denom) < EPS) { /* parallel or really close to parallel */
//        if(fabs((y1*ax - ay*x1)*bx - (y3*bx - by*x3)*ax) > EPS2)
//            return 0; /* different intercepts, no intersection */
      return 1;
//        return 0;
    }

    double recip = 1 / denom;
    double na = (by * cx - bx * cy) * recip;
    if(na < -EPS2 || na > 1 + EPS2)
        return 0;

    double nb = (ax * cy - ay * cx) * recip;
    if(nb < -EPS2 || nb > 1 + EPS2)
        return 0;

    return 1;
}

int ClimatologyOverlayFactory::CycloneTrackCrossings(double lat1, double lon1, double lat2, double lon2,
                                                     const wxDateTime &date, int dayrange)
{
    // if dayrange is zero, we are just probing, so other parameters are likely invalid
    if(!dayrange)
        return 0;

    m_cyclone_cache_semaphore.Wait();
    /* build hash table cache to speed up cyclone crossing calculation */
    if(m_cyclone_cache.empty()) {
        m_cyclone_cache_semaphore.Post();
        return -1;
    }
        
    int lon_min = wxMin(lon1, lon2), lon_max = wxMax(lon1, lon2);

    if(lon_min > 15 || lon_max > 15)
        lon_min -= 360, lon_max -= 360;

    int lat_min = wxMin(lat1, lat2), lat_max = wxMax(lat1, lat2);

    int day = date.GetMonth()*365/12 + date.GetDay()-1;
    const int half_range = wxMin(dayrange / 2, 182);
    const int day1 = (day - half_range + 365) % 365;
    const int day2 = (day + half_range) % 365;
    const int month_start = dayrange >= 365 ? 0 : day1 * 12 / 365;
    const int month_end = dayrange >= 365 ? 11 : day2 * 12 / 365;

    for(int loni = lon_min; loni <= lon_max; loni++)
        for(int lati = lat_min; lati <= lat_max; lati++) {
            int monthi = month_start;
            for(;;) {

                int hash = (floor((double)loni) * 180
                            + floor((double)lati))*12 + monthi;

                climatology::CycloneSpatialIndex::const_iterator bucket =
                    m_cyclone_cache.find(hash);
                if(bucket == m_cyclone_cache.end()) {
                    if(monthi == month_end) break;
                    monthi = (monthi + 1) % 12;
                    continue;
                }
                const std::vector<const climatology::CycloneSegment*> &
                    cyclonestates = bucket->second;
                for(std::vector<const climatology::CycloneSegment*>::const_iterator
                        it = cyclonestates.begin();
                    it != cyclonestates.end(); ++it) {
                    const climatology::CycloneSegment *segment = *it;
                        
                    int cday = segment->time.month * 365 / 12 +
                        segment->time.day - 1;
                    const int daydiff = climatology::CircularDayDistance(cday, day);

                    if(daydiff <= half_range) {
                        /* we should split all cyclones at 15 degrees longitude... until then... */
                        while(lon1 - segment->longitude0 > 180)
                            lon1 -= 360, lon2 -= 360;
                        while(lon1 - segment->longitude0 < -180)
                            lon1 += 360, lon2 += 360;
                                
                        if(TestIntersectionXY(lat1, lon1, lat2, lon2,
                                              segment->latitude0,
                                              segment->longitude0,
                                              segment->latitude1,
                                              segment->longitude1)) {
                            m_cyclone_cache_semaphore.Post();
                            return 1;
                        }
                    }
                }
                if(monthi == month_end)
                    break;
                monthi = (monthi + 1) % 12;
            }
        }

    m_cyclone_cache_semaphore.Post();
    return 0;
}

void ClimatologyOverlayFactory::RenderOverlayMap( int setting, PlugIn_ViewPort &vp)
{
    const climatology::FieldRenderState &state = m_renderState.fields[setting];
    if(!state.overlay_map)
        return;

    int month, nmonth;
    double dpos;

    if(setting == ClimatologyOverlaySettings::SEADEPTH) {
        month = nmonth = 0;
        dpos = 1;
    } else
        GetDateInterpolation(NULL, month, nmonth, dpos);

    if(!state.overlay_interpolation) {
        nmonth = month;
        dpos = 1;
    }

    ClimatologyOverlay &O1 = m_pOverlay[month][setting];
    ClimatologyOverlay &O2 = m_pOverlay[nmonth][setting];

    if( !m_dc->GetDC() )
    {
        if( !O1.m_iTexture )
            CreateGLTexture( O1, setting, month, vp);

        if( !O2.m_iTexture )
            CreateGLTexture( O2, setting, nmonth, vp);

        DrawGLTexture(O1, O2, dpos, vp,
                      state.overlay_transparency / 100.0);
    }
    else
    {
        wxString msg = _("Climatology overlay map unsupported unless OpenGL is enabled");

        wxMemoryDC mdc;
        wxBitmap bm( 1000, 1000 );
        mdc.SelectObject( bm );
        mdc.Clear();

        wxFont font( 10, wxFONTFAMILY_DEFAULT, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL );

        mdc.SetFont( font );
        mdc.SetPen( *wxTRANSPARENT_PEN);

        mdc.SetBrush( wxColour(243, 47, 229 ) );
        int w, h;
        mdc.GetMultiLineTextExtent( msg, &w, &h );
        h += 2;
        int label_offset = 10;
        int wdraw = w + ( label_offset * 2 );
        mdc.DrawRectangle( 0, 0, wdraw, h );

        mdc.DrawLabel( msg, wxRect( label_offset, 0, wdraw, h ), wxALIGN_LEFT| wxALIGN_CENTRE_VERTICAL);
        mdc.SelectObject( wxNullBitmap );

        wxBitmap sbm = bm.GetSubBitmap( wxRect( 0, 0, wdraw, h ) );
        int x = vp.pix_width, y = vp.pix_height;
        m_dc->DrawBitmap( sbm, (x-wdraw)/2, y - ( GetChartbarHeight() + h ), false );
    }
}

ZUFILE *ClimatologyOverlayFactory::TryOpenFile(wxString filename)
{
    const wxString ext = ".gz";
    ZUFILE *f = zu_open(filename.mb_str(), "rb", ZU_COMPRESS_AUTO);
    if(!f) {
        f = zu_open((filename+ext).mb_str(), "rb", ZU_COMPRESS_AUTO);
        if(f)
            wxLogMessage("climatology found compressed data: " + filename+ext);
    }

    if(!f)
        wxLogMessage("climatology failed to read: " + filename);


    return f;
}

void ClimatologyOverlayFactory::RenderNumber(wxPoint p, double v, const wxColour &color)
{
    wxString text;
    if(isnan(v))
        text = _("N/A");
    else
        text.Printf("%.0f", round(v));

    m_dc->SetTextForeground(color);

    wxCoord w, h;
    m_dc->GetTextExtent(text, &w, &h);
    m_dc->DrawText(text, p.x-w/2, p.y-h/2);
}

void ClimatologyOverlayFactory::RenderIsoBars(int setting, PlugIn_ViewPort &vp)
{
    const climatology::FieldRenderState &state = m_renderState.fields[setting];
    if(!state.isobars)
        return;

    int month = m_bAllTimes ? 12 : m_CurrentTimeline.GetMonth();
    int day = 15; //m_CurrentTimeline.GetDay();
    if(setting == ClimatologyOverlaySettings::SEADEPTH)
        month = 0;

    ClimatologyIsoBarMap *&pIsobars = m_Settings.Settings[setting].m_pIsobars[month];
    const double spacing = state.isobar_spacing;
    const double step = IsobarStep(state.isobar_step);

    int units = state.units;
    if(pIsobars && !pIsobars->SameSettings(spacing, step, units, month, day)) {
        delete pIsobars;
        pIsobars = NULL;
    }

    if( !pIsobars ) {
        if(m_isobarJob) {
            const bool same = m_isobarJob->setting == setting &&
                m_isobarJob->month == month && m_isobarJob->units == units &&
                m_isobarJob->spacing == spacing && m_isobarJob->step == step;
            if(m_isobarJob->setting == setting && !same)
                m_isobarJob->map->m_bNeedsRecompute.store(
                    true, std::memory_order_release);
            return;
        }

        static const char *names[] = {
            "Wind", "Current", "Sea Level Pressure",
            "Sea Surface Temperature", "Air Temperature", "Cloud Cover",
            "Precipitation", "Relative Humidity", "Lightning", "Sea Depth"};
        m_isobarJob.reset(new ClimatologyIsobarJob);
        ClimatologyIsobarJob *job = m_isobarJob.get();
        job->setting = setting;
        job->month = month;
        job->units = units;
        job->spacing = spacing;
        job->step = step;
        job->map.reset(new ClimatologyIsoBarMap(
            _(names[setting]), spacing, step, *this, setting, units, month, day));
        try {
            job->worker = std::thread([job]() {
                try {
                    job->succeeded = job->map->Recompute(NULL);
                } catch(...) {
                    job->succeeded = false;
                }
                job->done.store(true, std::memory_order_release);
            });
        } catch(...) {
            m_isobarJob.reset();
        }
        return;
    }

    pIsobars->Plot(m_dc, vp);
}

void ClimatologyOverlayFactory::RenderNumbers(int setting, PlugIn_ViewPort &vp)
{
    const climatology::FieldRenderState &state = m_renderState.fields[setting];
    if(!state.numbers)
        return;

    double space = state.numbers_spacing;
    wxPoint p;
    for(p.y = space/2; p.y <= vp.rv_rect.height-space/4; p.y+=space)
        for(p.x = space/2; p.x <= vp.rv_rect.width-space/4; p.x+=space) {
            double lat, lon;
            GetCanvasLLPix( &vp, p, &lat, &lon);
            RenderNumber(p, getCurCalibratedValue(MAG, setting, lat, lon), *wxBLACK);
        }
}

void ClimatologyOverlayFactory::RenderDirectionArrows(int setting, PlugIn_ViewPort &vp)
{
    const climatology::FieldRenderState &state = m_renderState.fields[setting];
    if(!state.direction_arrows)
        return;

    int month = m_bAllTimes ? 12 : m_CurrentTimeline.GetMonth();

    double step;
    switch(setting) {
    case ClimatologyOverlaySettings::WIND:
        if(!m_snapshot || !m_snapshot->wind.available[month])
            return;
        step = m_snapshot->wind.geometry.longitude_step;
        break;
    case ClimatologyOverlaySettings::CURRENT:
        if(!m_snapshot || !m_snapshot->current.available[month])
            return;
        step = m_snapshot->current.geometry.longitude_step;
        break;
    default: return;
    }

    double lengthtype = state.arrow_length_type;
    int width = state.arrow_width;
    wxColour color(state.arrow_red, state.arrow_green, state.arrow_blue,
                   state.arrow_alpha);
    double size = state.arrow_size;
    double spacing = state.arrow_spacing;

    int w = vp.pix_width, h = vp.pix_height;
    while((vp.lat_max - vp.lat_min) / step > h / spacing ||
          (vp.lon_max - vp.lon_min) / step > w / spacing)
        step *= 2;

    for(double lat = round(vp.lat_min/step)*step-1; lat <= vp.lat_max+1; lat+=step)
        for(double lon = round(vp.lon_min/step)*step-1; lon <= vp.lon_max+1; lon+=step) {
            double u = getCurValue(U, setting, lat, lon), v = getCurValue(V, setting, lat, lon);

            // for wind, flip direction to render where the wind is blowing
            if(setting == ClimatologyOverlaySettings::WIND)
               u = -u, v = -v;

            double mag = hypot(u, v);

            double cstep, minv;
            if(setting == ClimatologyOverlaySettings::CURRENT) {
                cstep = .25;
                minv = .25;
            } else {
                cstep = 5;
                minv = 2;

                if(lengthtype) {
                    u /= 15;
                    v /= 15;
                }
            }

            if(!lengthtype)
                u/=mag, v/=mag;
            else if(mag < minv)
                continue;
            
            double t = vp.rotation;
            double x = -size*(u*cos(t) + v*sin(t)), y = size*(v*cos(t) - u*sin(t));
            wxPoint p;
            GetCanvasPixLL( &vp, &p, lat, lon );
            DrawLine(p.x+x, p.y+y, p.x-x, p.y-y, color, width);

            if(!lengthtype) {
                double ix = x, iy = y, dir = 1;
                while(mag > cstep) {
                    DrawLine(p.x+ix, p.y+iy, p.x+ix+x/3+dir*y/2, p.y+iy+y/3-dir*x/2,
                             color, width);
                    dir = -dir;
                    if(dir > 0) {
                        ix = ix*2/3;
                        iy = iy*2/3;
                    }
                    mag -= cstep;
                }
            }

            DrawLine(p.x-x, p.y-y, p.x-x/3+y*2/3, p.y-y/3-x*2/3,
                     color, width);
            DrawLine(p.x-x, p.y-y, p.x-x/3-y*2/3, p.y-y/3+x*2/3,
                     color, width);
        }
}

void ClimatologyOverlayFactory::RenderWindAtlas(PlugIn_ViewPort &vp)
{
    if(!m_renderState.wind_atlas.enabled)
        return;

    int month, nmonth;
    double dpos;

    GetDateInterpolation(NULL, month, nmonth, dpos);

    if(!m_snapshot || !m_snapshot->wind.available[month] ||
       !m_snapshot->wind.available[nmonth])
        return;

    double latstep = m_snapshot->wind.geometry.latitude_step;
    double lonstep = m_snapshot->wind.geometry.longitude_step;
    const double r = 12;
    double size = m_renderState.wind_atlas.size;
    double spacing = m_renderState.wind_atlas.spacing;

    int w = vp.pix_width, h = vp.pix_height;
    while((vp.lat_max - vp.lat_min) / latstep > h / spacing)
        latstep *= 2;
    while((vp.lon_max - vp.lon_min) / lonstep > w / spacing)
        lonstep *= 2;

    int dir_cnt = climatology::kWindSectors;
    double latoff = 90.0/m_snapshot->wind.geometry.rows;
    double lonoff = 180.0/m_snapshot->wind.geometry.columns;

    for(double lat = round(vp.lat_min/latstep)*latstep-latoff; lat <= vp.lat_max+1; lat+=latstep)
        for(double lon = round(vp.lon_min/lonstep)*lonstep-lonoff; lon <= vp.lon_max+1; lon+=lonstep) {
            double directions[64], speeds[64], gale, calm;
            if(!InterpolateWindAtlasTime(month, nmonth, dpos, lat, lon, directions, speeds, gale, calm))
                continue;

            wxPoint p;
            GetCanvasPixLL(&vp, &p, lat, lon);

            int opacity = m_renderState.wind_atlas.opacity;

            if(gale*2 > calm)
                RenderNumber(p, 100.0*gale, wxColour(255, 0, 0, opacity));
            else if(calm > 0)
                RenderNumber(p, 100.0*calm, wxColour(0, 0, 180, opacity));
            
            wxColour c(0, 0, 0, opacity);
            DrawCircle(p.x, p.y, r, c, 2);

            for(int d = 0; d<dir_cnt; d++) {
                double theta = 2*M_PI*d/dir_cnt + vp.rotation; 
                double x1 = p.x+r*sin(theta), y1 = p.y-r*cos(theta);
                
                if(directions[d] == 0)
                    continue;

                const double maxdirection = .29;
                bool split = directions[d] > maxdirection;
                double u = r + size*(split ? maxdirection : directions[d]);
                double x2 = p.x+u*sin(theta), y2 = p.y-u*cos(theta);

                if(split) {
                    wxPoint q((x1 + x2)/2, (y1 + y2)/2);
                    RenderNumber(q, 100*directions[d], c);

                    DrawLine(x1, y1, (3*x1+x2)/4, (3*y1+y2)/4, c, 2);
                    DrawLine((x1+3*x2)/4, (y1+3*y2)/4, x2, y2, c, 2);
                } else
                    DrawLine(x1, y1, x2, y2, c, 2);

                /* draw wind barbs */
                double cur_speed = speeds[d];
                double dir = 1;
                const double a = 10, b = M_PI*2/3;
                while(cur_speed > 2) {
                    DrawLine(x2, y2, x2-a*sin(theta+dir*b), y2+a*cos(theta+dir*b), c, 2);
                    dir = -dir;
                    if(dir > 0)
                        x2 -= 3*sin(theta), y2 += 3*cos(theta);
                    cur_speed -= 5;
                }
            }
        }
}

void ClimatologyOverlayFactory::RenderCycloneSegment(
    const climatology::CycloneSegment &segment, PlugIn_ViewPort &vp,
    int dayspan,
    std::unordered_set<const climatology::CycloneSegment*> &drawn)
{
    if(!drawn.insert(&segment).second)
        return;
                    
    if(!m_renderState.all_times) {
        int daydiff = abs(segment.time.day - m_CurrentTimeline.GetDay()
                          + 30.42*(segment.time.month - m_CurrentTimeline.GetMonth()));
        if(daydiff > 183)
            daydiff = 365 - daydiff;
        if(daydiff > dayspan/2)
            return;
    }

    const double lat[2] = {segment.latitude0, segment.latitude1};
    const double lon[2] = {segment.longitude0, segment.longitude1};
#if 0
    /* prevent wrong crossover */
    if((lastlon+180 > vp.clon || lon+180 < vp.clon) &&
       (lastlon+180 < vp.clon || lon+180 > vp.clon) &&
       (lastlon-180 > vp.clon || lon-180 < vp.clon) &&
       (lastlon-180 < vp.clon || lon-180 > vp.clon))
#endif
    {
        wxPoint p[2];
        GetCanvasPixLL( &vp, &p[0], lat[0], lon[0] );
        GetCanvasPixLL( &vp, &p[1], lat[1], lon[1] );

        wxColour c = GetGraphicColor(CYCLONE_SETTING, segment.wind_knots);
                            
        DrawLine(p[0].x, p[0].y, p[1].x, p[1].y, c, 2);
                            
        /* direction arrow */
        wxPoint a((p[0].x+p[1].x)/2, (p[0].y+p[1].y)/2);
        wxPoint d(p[0].x-p[1].x, p[0].y-p[1].y);
                            
        DrawLine(a.x, a.y, a.x + (d.x+d.y)/5, a.y + (d.y-d.x)/5, c, 2);
        DrawLine(a.x, a.y, a.x + (d.x-d.y)/5, a.y + (d.x+d.y)/5, c, 2);
    }
}

void ClimatologyOverlayFactory::RenderCyclones(PlugIn_ViewPort &vp)
{
    /* no cyclones ever existed between 10 and 20 longitude
       so use 15 east as the meridian to split the world on.. */
//#define USE_DL
#ifdef USE_DL
    PlugIn_ViewPort nvp = vp;
    double cclon = 15;
    static const double NORM_FACTOR = 16;

    if(!m_cyclonesDisplayList)
        m_cyclonesDisplayList = glGenLists(1);

    if(!m_pdc) {
        glPushMatrix();

        wxPoint point;
        GetCanvasPixLL(&vp, &point, 0, cclon - 180);
        glTranslatef(point.x, point.y, 0);
        glScalef(vp.view_scale_ppm/NORM_FACTOR, vp.view_scale_ppm/NORM_FACTOR, 1);
        glRotated(vp.rotation*180/M_PI, 0, 0, 1);
    }
    
    if(!m_bUpdateCyclones)
        glCallList(m_cyclonesDisplayList);
    else {
        if(!m_pdc) {
            m_bUpdateCyclones = false;
        
            nvp.clat = 0;
            nvp.clon = cclon - 180;
            nvp.view_scale_ppm = NORM_FACTOR;
            nvp.rotation = nvp.skew = 0;
    
            if(!m_cyclonesDisplayList)
                m_cyclonesDisplayList = glGenLists(1);
            glNewList(m_cyclonesDisplayList, GL_COMPILE_AND_EXECUTE);
        }
#endif

    int dayspan = m_renderState.cyclone_filter.day_span;

    int month_start, month_end;
    if(m_renderState.all_times) {
        month_start = 0;
        month_end = 11;
    } else {
        wxDateTime dt2(m_CurrentTimeline.GetDay(), m_CurrentTimeline.GetMonth(), 1999);
        wxTimeSpan ts_dayspan = wxTimeSpan::Days(dayspan/2);
        month_start = (dt2 - ts_dayspan).GetMonth();
        month_end = (dt2 + ts_dayspan).GetMonth();
    }

    m_cyclone_cache_semaphore.Wait();
    if(m_cyclone_cache.empty()) {
        m_cyclone_cache_semaphore.Post();
        return;
    }

    std::unordered_set<const climatology::CycloneSegment*> drawn;

    wxDateTime start = wxDateTime::Now();
    for(int lati = floor(vp.lat_min); lati <= ceil(vp.lat_max); lati++)
        for(int loni = floor(vp.lon_min); loni <= ceil(vp.lon_max); loni++) {
            int monthi = month_start;
            for(;;) {
                int lonin = loni < 15 ? loni : loni - 360;
                int hash = (lonin * 180 + lati)*12 + monthi;

                climatology::CycloneSpatialIndex::const_iterator bucket =
                    m_cyclone_cache.find(hash);
                if(bucket != m_cyclone_cache.end()) {
                    const std::vector<const climatology::CycloneSegment*> &states =
                        bucket->second;
                    for(std::vector<const climatology::CycloneSegment*>
                            ::const_iterator it = states.begin();
                        it != states.end(); ++it)
                        RenderCycloneSegment(**it, vp, dayspan, drawn);
                }

                if(monthi == month_end)
                    break;
                if(++monthi == 12) monthi = 0;
            }
        }

    wxDateTime end = wxDateTime::Now();
    m_cyclone_cache_semaphore.Post();

    /* rendering is taking too long, and display lists not used */
    if(m_dc->GetDC() && (end - start).GetMilliseconds() >= 1200) {
        m_disableCyclonesForPerformance = true;
    }

#ifdef USE_DL
    if(!m_pdc)
        glEndList();

    if(!m_pdc) {
        //  Does current vp cross cclon ?
        // if so, call the display list again translated
        // to the other side of it..

        if( vp.lon_min < cclon && vp.lon_max > cclon ) {
            double ts = 40058986*NORM_FACTOR; /* 360 degrees in normalized viewport */

            glPushMatrix();
            if( vp.clon > cclon )
                glTranslated(-ts, 0, 0);
            else
                glTranslated(ts, 0, 0);
            glCallList(m_cyclonesDisplayList);
            glPopMatrix();
        }
        glPopMatrix();
    }
#endif
}

static void QueryGL()
{
#if !defined(__ANDROID__) && !defined(__APPLE__)

    // assume we have GL_ARB_multitexture if this passes
    if(QueryExtension( "GL_ARB_texture_env_combine" )) {
        s_glActiveTextureARB = (PFNGLACTIVETEXTUREARBPROC)
            systemGetProcAddress("glActiveTextureARB");
        s_glMultiTexCoord2dARB = (PFNGLMULTITEXCOORD2DARBPROC)
            systemGetProcAddress("glMultiTexCoord2dARB");
        s_multitexturing = s_glActiveTextureARB && s_glMultiTexCoord2dARB;

        if(s_multitexturing) {
            GLint MaxTextureUnits;
            glGetIntegerv(GL_MAX_TEXTURE_UNITS_ARB, &MaxTextureUnits);
            if(MaxTextureUnits > 2)
                s_multitexturing = 2; /* with blending */
        }
    }
#else
    s_multitexturing = 0;
#endif


    // npot textures don't support GL_REPEAT on GLES
    // and texture rectangle doesn't either
    if( QueryExtension( "GL_ARB_texture_non_power_of_two" ) )
        texture_format = GL_TEXTURE_2D, s_bnoglrepeat = false;
    else if( QueryExtension( "GL_OES_texture_npot" ) )
        texture_format = GL_TEXTURE_2D;
    else if( QueryExtension( "GL_ARB_texture_rectangle" ) )
        texture_format = GL_TEXTURE_RECTANGLE_ARB;
    else
        texture_format = 0; // overlays disabled without npot support

#ifdef USE_ANDROID_GLES2
    s_bnoglrepeat = false; // supported on gles2 thankfully
#endif
}

bool ClimatologyOverlayFactory::RenderOverlay( piDC &dc, PlugIn_ViewPort &vp )
{
    if(!IsReady())
        return false;
    m_renderState = m_dlg.CaptureRenderState();
    m_dc = &dc;

    if(!dc.GetDC()) {
        if(!glQueried) {
            QueryGL();
            glQueried = true;
        }
#ifndef USE_GLSL
        glPushAttrib( GL_LINE_BIT | GL_ENABLE_BIT | GL_HINT_BIT ); //Save state

        //      Enable anti-aliased lines, at best quality
        glEnable( GL_LINE_SMOOTH );
        glHint( GL_LINE_SMOOTH_HINT, GL_NICEST );

        glBlendFunc( GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA );
#endif
        glEnable( GL_BLEND );
    }

    wxFont font( 12, wxFONTFAMILY_DEFAULT, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL );
    m_dc->SetFont( font );

    for(int overlay = 1; overlay >= 0; overlay--)
    for(int i=0; i<ClimatologyOverlaySettings::SETTINGS_COUNT; i++) {
        if(!m_renderState.fields[i].visible)
            continue;

        if(!m_renderState.fields[i].enabled)
            continue;

        if(overlay) /* render overlays first */
            RenderOverlayMap( i, vp );
        else {
            RenderIsoBars(i, vp);
            RenderNumbers(i, vp);
            RenderDirectionArrows(i, vp);
        }

    }

    if(m_renderState.show_wind_atlas)
        RenderWindAtlas(vp);

    if(m_renderState.show_cyclones)
        RenderCyclones(vp);

#ifndef USE_GLSL
    if(!dc.GetDC())
        glPopAttrib();
#endif
    return true;
}
