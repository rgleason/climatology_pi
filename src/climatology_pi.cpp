/******************************************************************************
 *
 * Project:  OpenCPN
 * Purpose:  Climatology Plugin
 * Author:   Sean D'Epagnier
 *
 ***************************************************************************
 *   Copyright (C) 2016 by Sean D'Epagnier                                 *
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
 *   51 Franklin Street, Fifth Floor, Boston, MA 02110-1301,  USA.             *
 ***************************************************************************
 */


#include "wx/wxprec.h"

#include <cinttypes>
#include <cstdint>
#include <ctime>
#include <cstdio>
#include <cstring>
#include <string>

#ifndef  WX_PRECOMP
  #include "wx/wx.h"
  #include <wx/glcanvas.h>
#endif //precompiled headers

#include <wx/treectrl.h>
#include <wx/fileconf.h>

#include "climatology_pi.h"
#include "ClimatologyAPI.h"
#include "ClimatologyProviderAPI.h"

#include <wx/ffile.h>
#include "icons.h"
#include "ClimatologyDialog.h"


ClimatologyOverlayFactory *g_pOverlayFactory = NULL;

// the class factories, used to create and destroy instances of the PlugIn

extern "C" DECL_EXP opencpn_plugin* create_pi(void *ppimgr)
{
    return new climatology_pi(ppimgr);
}

extern "C" DECL_EXP void destroy_pi(opencpn_plugin* p)
{
    delete p;
}

template<typename Function>
static std::string FunctionPointerText(Function function)
{
    static_assert(sizeof(Function) <= sizeof(std::uintptr_t),
                  "function pointer does not fit the legacy message representation");
    if(function == nullptr)
        return "0x0";
    std::uintptr_t address = 0;
    std::memcpy(&address, &function, sizeof function);
    char text[2 + sizeof(address) * 2 + 1];
    std::snprintf(text, sizeof text, "0x%" PRIxPTR, address);
    return text;
}

wxString ClimatologyDataDirectory()
{
    wxFileName dir(GetPluginDataDir("climatology_pi"), "");
    dir.AppendDir("data");

    if (!dir.DirExists())
        dir.Mkdir(wxS_DIR_DEFAULT, wxPATH_MKDIR_FULL);

    return dir.GetFullPath();
}

static bool ReadDatasetManifest(Json::Value &manifest)
{
    wxFFile file(ClimatologyDataDirectory() + "dataset-manifest.json", "rb");
    wxString contents;
    if(!file.IsOpened() || !file.ReadAll(&contents))
        return false;
    Json::Reader reader;
    return reader.parse(std::string(contents.utf8_str()), manifest, false) && manifest.isObject();
}

wxString ClimatologyDatasetVersion()
{
    Json::Value manifest;
    if(ReadDatasetManifest(manifest) && manifest["dataset_version"].isString())
        return wxString::FromUTF8(manifest["dataset_version"].asCString());
    return _("legacy/unversioned");
}



climatology_pi::climatology_pi(void *ppimgr)
      :opencpn_plugin_118(ppimgr), m_dataset_load_timer(this)
{
      m_pClimatologyDialog = nullptr;

      // Create the PlugIn icons
      initialize_images();

      //original technique using images in file  icon.cpp

    // Build the full path to the panel icon
    wxFileName fn(GetPluginDataDir("climatology_pi"), "");
    fn.AppendDir("UserIcons");
    fn.SetFullName("climatology_panel.png");

    wxString path = fn.GetFullPath();

    wxInitAllImageHandlers();

    wxLogDebug(wxString("Using icon path: ") + path);
    if (!wxImage::CanRead(path)) {
        wxLogDebug("Initiating image handlers.");
        wxInitAllImageHandlers();
    }
    wxImage panelIcon(path);
    if (panelIcon.IsOk())
        m_panelBitmap = wxBitmap(panelIcon);
    else
        wxLogWarning("Climatology panel icon has NOT been loaded");
    // End 
}

climatology_pi::~climatology_pi()
{
      m_dataset_load_timer.Stop();
      FreeData();
      delete _img_climatology;
}

int climatology_pi::Init()
{
      AddLocaleCatalog( _T("opencpn-climatology_pi") );

      // Set some default private member parameters
      m_climatology_dialog_x = 0;
      m_climatology_dialog_y = 0;
      m_climatology_dialog_sx = 200;
      m_climatology_dialog_sy = 400;

      ::wxDisplaySize(&m_display_width, &m_display_height);

      //    Get a pointer to the opencpn configuration object
      m_pconfig = GetOCPNConfigObject();

      // Get a pointer to the opencpn display canvas, to use as a parent for the  dialog
      m_parent_window = GetOCPNCanvasWindow();

      //    This PlugIn needs a toolbar icon, so request its insertion if enabled locally


#ifdef PLUGIN_USE_SVG
      m_leftclick_tool_id = InsertPlugInToolSVG( _T( "Climatology" ),
	  _svg_climatology, _svg_climatology, _svg_climatology_toggled,
	  wxITEM_CHECK, _("Climatology"), _T( "") , NULL,
	  CLIMATOLOGY_TOOL_POSITION, 0, this);
#else
      m_leftclick_tool_id  = InsertPlugInTool( _T(""), _img_climatology,
      _img_climatology, wxITEM_NORMAL, _("Climatology"), _T(""), NULL,
	  CLIMATOLOGY_TOOL_POSITION, 0, this);
#endif
      // Create the hidden facade on OpenCPN's main thread.  API callbacks may
      // later run on routing workers and must never construct wx controls.
      CreateOverlayFactory();
      SendClimatology(true);

      return (WANTS_OVERLAY_CALLBACK |
           WANTS_OPENGL_OVERLAY_CALLBACK |
           WANTS_CURSOR_LATLON       |
           WANTS_TOOLBAR_CALLBACK    |
           INSTALLS_TOOLBAR_TOOL     |
           WANTS_CONFIG              |
           WANTS_PLUGIN_MESSAGING
            );
}

bool climatology_pi::DeInit()
{
    m_dataset_load_timer.Stop();
    SendClimatology(false);
    FreeData();
    RemovePlugInTool(m_leftclick_tool_id);

    return true;
}

int climatology_pi::GetAPIVersionMajor()
{
      return OCPN_API_VERSION_MAJOR;
}

int climatology_pi::GetAPIVersionMinor()
{
      return OCPN_API_VERSION_MINOR;
}

int climatology_pi::GetPlugInVersionMajor()
{
      return PLUGIN_VERSION_MAJOR;
}

int climatology_pi::GetPlugInVersionMinor()
{
      return PLUGIN_VERSION_MINOR;
}

int climatology_pi::GetPlugInVersionPatch()
{
      return PLUGIN_VERSION_PATCH;
}

int climatology_pi::GetPlugInVersionPost()
{
      return PLUGIN_VERSION_TWEAK;
}


/*  Converts  icon.cpp file to an image. Original process
wxBitmap *climatology_pi::GetPlugInBitmap()
{
      return new wxBitmap(_img_climatology->ConvertToImage().Copy());
}
*/

// Shipdriver uses the climatology_panel.png file to make the bitmap.
wxBitmap *climatology_pi::GetPlugInBitmap()  { return &m_panelBitmap; }
// End of shipdriver process

wxString climatology_pi::GetCommonName()
{
	      return _T(PLUGIN_COMMON_NAME);
}

wxString climatology_pi::GetShortDescription()
{
    return _(PLUGIN_SHORT_DESCRIPTION);
}


wxString climatology_pi::GetLongDescription()
{
    return _(PLUGIN_LONG_DESCRIPTION);

}

void climatology_pi::CreateOverlayFactory()
{
    if(m_pClimatologyDialog)
        return;

    //    And load the configuration items
    LoadConfig();

    m_pClimatologyDialog = new ClimatologyDialog(m_parent_window, this);
    m_pClimatologyDialog->Move(wxPoint(m_climatology_dialog_x, m_climatology_dialog_y));

    wxIcon icon;
    icon.CopyFromBitmap(*_img_climatology);
    m_pClimatologyDialog->SetIcon(icon);

    // Create the drawing factory
    g_pOverlayFactory = new ClimatologyOverlayFactory( *m_pClimatologyDialog );
    m_dataset_load_timer.Start(50);
    m_pClimatologyDialog->Hide();
}

void ClimatologyLoadTimer::Notify()
{
    if(m_plugin)
        m_plugin->OnDatasetLoadTimer();
}

void climatology_pi::OnDatasetLoadTimer()
{
    if(!g_pOverlayFactory) {
        m_dataset_load_timer.Stop();
        return;
    }
    bool succeeded = false;
    wxString error;
    if(!g_pOverlayFactory->PollDatasetLoad(succeeded, error))
        return;

    m_dataset_load_timer.Stop();
    SendClimatology(succeeded);
    if(succeeded) {
        m_pClimatologyDialog->UpdateTrackingControls();
        m_pClimatologyDialog->FitLater();
        RequestRefresh(m_parent_window);
        return;
    }

    wxLogError("climatology_pi: dataset load failed: " + error);
    wxMessageDialog dialog(m_pClimatologyDialog,
        _("The Climatology dataset could not be loaded. The matching complete "
          "versioned dataset must be reinstalled.\n\n") + error,
        _("Climatology"), wxOK | wxICON_ERROR);
    dialog.ShowModal();
}

void climatology_pi::SetDefaults(void)
{
}

int climatology_pi::GetToolbarToolCount(void)
{
      return 1;
}

static bool ClimatologyData(int setting, const wxDateTime &date, double lat, double lon,
                            double &dir, double &speed)
{
    if (!g_pOverlayFactory || !g_pOverlayFactory->IsReady())
        return false;

    speed = g_pOverlayFactory->getValue(MAG, setting, lat, lon, &date);
    if(isnan(speed))
        return false;

    dir = g_pOverlayFactory->getValue(DIRECTION, setting, lat, lon, &date);
    if(isnan(dir))
        return false;

    return true;
}

static bool ClimatologyWindAtlasData(const wxDateTime &date, double lat, double lon,
                                     int &count, double *directions, double *speeds,
                                     double &storm, double &calm)
{
    if(!g_pOverlayFactory)
        return false;

    if(count != 8)
        return false;

    return g_pOverlayFactory->InterpolateWindAtlas
        (date, lat, lon, directions, speeds, storm, calm);
}

static int ClimatologyCycloneTrackCrossings(double lat1, double lon1, double lat2, double lon2,
                                            const wxDateTime &date, int dayrange)
{
    if(!g_pOverlayFactory)
        return -1;

    return g_pOverlayFactory->CycloneTrackCrossings(lat1, lon1, lat2, lon2,
                                                    date, dayrange);
}

static std::int32_t FinishClimatologyQuery(
    const climatology_api::QueryV1 *query,
    climatology_api::ResultV1 *result,
    climatology_api::Status status)
{
    if(!result || result->struct_size < sizeof(climatology_api::ResultV1))
        return climatology_api::STATUS_INVALID_REQUEST;
    climatology_api::ResultV1 output = climatology_api::MakeResultV1();
    output.status = status;
    if(query) {
        output.variable = query->variable;
        output.scenario = query->scenario;
    }
    *result = output;
    return status;
}

static bool ScalarVariableDetails(std::uint32_t variable, int &setting,
                                  std::uint32_t &unit)
{
    switch(variable) {
    case climatology_api::VARIABLE_SEA_LEVEL_PRESSURE:
        setting = ClimatologyOverlaySettings::SLP;
        unit = climatology_api::UNIT_HECTOPASCALS; return true;
    case climatology_api::VARIABLE_SEA_SURFACE_TEMPERATURE:
        setting = ClimatologyOverlaySettings::SST;
        unit = climatology_api::UNIT_DEGREES_CELSIUS; return true;
    case climatology_api::VARIABLE_AIR_TEMPERATURE:
        setting = ClimatologyOverlaySettings::AT;
        unit = climatology_api::UNIT_DEGREES_CELSIUS; return true;
    case climatology_api::VARIABLE_CLOUD_COVER:
        setting = ClimatologyOverlaySettings::CLOUD;
        unit = climatology_api::UNIT_PERCENT; return true;
    case climatology_api::VARIABLE_PRECIPITATION:
        setting = ClimatologyOverlaySettings::PRECIPITATION;
        unit = climatology_api::UNIT_MILLIMETRES_PER_DAY; return true;
    case climatology_api::VARIABLE_RELATIVE_HUMIDITY:
        setting = ClimatologyOverlaySettings::RELATIVE_HUMIDITY;
        unit = climatology_api::UNIT_PERCENT; return true;
    case climatology_api::VARIABLE_LIGHTNING:
        setting = ClimatologyOverlaySettings::LIGHTNING;
        unit = climatology_api::UNIT_LIGHTNING_INDEX; return true;
    case climatology_api::VARIABLE_SEA_DEPTH:
        setting = ClimatologyOverlaySettings::SEADEPTH;
        unit = climatology_api::UNIT_METRES; return true;
    default: return false;
    }
}

static std::int32_t ClimatologyQueryV1(
    const climatology_api::QueryV1 *query,
    climatology_api::ResultV1 *result)
{
    if(!result || result->struct_size < sizeof(climatology_api::ResultV1))
        return climatology_api::STATUS_INVALID_REQUEST;
    const climatology_api::Status validation =
        climatology_api::ValidateQueryV1(query);
    if(validation != climatology_api::STATUS_OK)
        return FinishClimatologyQuery(query, result, validation);
    if(!g_pOverlayFactory || !g_pOverlayFactory->IsReady())
        return FinishClimatologyQuery(query, result,
                                      climatology_api::STATUS_NOT_READY);

    const std::time_t timestamp = static_cast<std::time_t>(query->unix_time_utc);
    if(static_cast<std::int64_t>(timestamp) != query->unix_time_utc)
        return FinishClimatologyQuery(query, result,
                                      climatology_api::STATUS_INVALID_REQUEST);
    wxDateTime date(timestamp);
    if(!date.IsValid())
        return FinishClimatologyQuery(query, result,
                                      climatology_api::STATUS_INVALID_REQUEST);
    date = date.ToUTC();

    climatology_api::ResultV1 output = climatology_api::MakeResultV1();
    output.variable = query->variable;
    output.scenario = query->scenario;
    if(query->variable == climatology_api::VARIABLE_WIND_10M) {
        double frequencies[climatology_api::WIND_SECTOR_COUNT] = {};
        double speeds[climatology_api::WIND_SECTOR_COUNT] = {};
        double gale = 0.0, calm = 0.0;
        if(!g_pOverlayFactory->InterpolateWindAtlas(
               date, query->latitude_degrees_north,
               query->longitude_degrees_east, frequencies, speeds,
               gale, calm))
            return FinishClimatologyQuery(query, result,
                                          climatology_api::STATUS_UNKNOWN);
        output.value_kind = climatology_api::VALUE_KIND_WIND_DISTRIBUTION;
        output.unit = climatology_api::UNIT_KNOTS;
        output.direction_convention =
            climatology_api::DIRECTION_METEOROLOGICAL_FROM_TRUE_NORTH;
        output.flags = climatology_api::RESULT_HAS_WIND_DISTRIBUTION |
                       climatology_api::RESULT_HAS_CALM_PROBABILITY |
                       climatology_api::RESULT_HAS_GALE_PROBABILITY;
        for(std::size_t sector = 0;
            sector < climatology_api::WIND_SECTOR_COUNT; ++sector) {
            output.sector_direction_degrees_true[sector] = 45.0 * sector;
            output.sector_frequency[sector] = frequencies[sector];
            output.sector_speed_knots[sector] = speeds[sector];
        }
        output.calm_probability = calm;
        output.gale_probability = gale;
    } else if(query->variable == climatology_api::VARIABLE_SURFACE_CURRENT) {
        const double eastward = g_pOverlayFactory->getValue(
            U, ClimatologyOverlaySettings::CURRENT,
            query->latitude_degrees_north, query->longitude_degrees_east,
            &date);
        const double northward = g_pOverlayFactory->getValue(
            V, ClimatologyOverlaySettings::CURRENT,
            query->latitude_degrees_north, query->longitude_degrees_east,
            &date);
        if(!std::isfinite(eastward) || !std::isfinite(northward))
            return FinishClimatologyQuery(query, result,
                                          climatology_api::STATUS_UNKNOWN);
        output.value_kind = climatology_api::VALUE_KIND_VECTOR;
        output.unit = climatology_api::UNIT_KNOTS;
        output.direction_convention =
            climatology_api::DIRECTION_OCEANOGRAPHIC_TO_TRUE_NORTH;
        output.flags = climatology_api::RESULT_HAS_VECTOR;
        output.eastward_value = eastward;
        output.northward_value = northward;
        output.speed = std::hypot(eastward, northward);
        if(output.speed > 0.0) {
            output.direction_degrees_true =
                std::atan2(eastward, northward) * 180.0 / M_PI;
            if(output.direction_degrees_true < 0.0)
                output.direction_degrees_true += 360.0;
        }
    } else {
        int setting = 0;
        std::uint32_t unit = climatology_api::UNIT_NONE;
        if(!ScalarVariableDetails(query->variable, setting, unit))
            return FinishClimatologyQuery(
                query, result, climatology_api::STATUS_UNSUPPORTED_VARIABLE);
        const double value = g_pOverlayFactory->getValue(
            MAG, setting, query->latitude_degrees_north,
            query->longitude_degrees_east, &date);
        if(!std::isfinite(value))
            return FinishClimatologyQuery(query, result,
                                          climatology_api::STATUS_UNKNOWN);
        output.value_kind = climatology_api::VALUE_KIND_SCALAR;
        output.unit = unit;
        output.flags = climatology_api::RESULT_HAS_SCALAR;
        output.scalar_value = value;
    }
    output.status = climatology_api::STATUS_OK;
    *result = output;
    return climatology_api::STATUS_OK;
}

// Compile-time guards for the pointer ABI advertised in SendClimatology().
static ClimatologyDataFunction const checked_climatology_data = &ClimatologyData;
static ClimatologyWindAtlasFunction const checked_wind_atlas_data = &ClimatologyWindAtlasData;
static ClimatologyCycloneCrossingsFunction const checked_cyclone_crossings = &ClimatologyCycloneTrackCrossings;
static climatology_api::QueryV1Function const checked_query_v1 =
    &ClimatologyQueryV1;

void climatology_pi::OnToolbarToolCallback(int id)
{
    CreateOverlayFactory();

    if(m_pClimatologyDialog->IsShown() && m_pClimatologyDialog->m_cfgdlg)
        m_pClimatologyDialog->m_cfgdlg->Hide();

    m_pClimatologyDialog->Show(!m_pClimatologyDialog->IsShown());
    RequestRefresh(m_parent_window); // refresh main window
}

void climatology_pi::OnClimatologyDialogClose()
{
    if(m_pClimatologyDialog) {
        if(m_pClimatologyDialog->m_cfgdlg)
            m_pClimatologyDialog->m_cfgdlg->Hide();
        m_pClimatologyDialog->Show(false);
        RequestRefresh(m_parent_window); // refresh main window
    }
    SaveConfig();
}

bool climatology_pi::RenderOverlay(wxDC &dc, PlugIn_ViewPort *vp)
{
    if(!m_pClimatologyDialog || !m_pClimatologyDialog->IsShown() ||
       !g_pOverlayFactory)
        return false;

    piDC pidc(dc);
    g_pOverlayFactory->RenderOverlay ( pidc, *vp );
    return true;
}

bool climatology_pi::RenderGLOverlay(wxGLContext *pcontext, PlugIn_ViewPort *vp)
{
    if(!m_pClimatologyDialog || !m_pClimatologyDialog->IsShown() ||
       !g_pOverlayFactory)
        return false;

    piDC pidc;
    glEnable( GL_BLEND );
    pidc.SetVP(vp);

    g_pOverlayFactory->RenderOverlay ( pidc, *vp );
    return true;
}

void climatology_pi::SetCursorLatLon(double lat, double lon)
{
    if(m_pClimatologyDialog)
        m_pClimatologyDialog->SetCursorLatLon(lat, lon);
}

void climatology_pi::SendClimatology(bool valid)
{
    Json::Value v;
    v["ClimatologyVersionMajor"] = GetPlugInVersionMajor();
    v["ClimatologyVersionMinor"] = GetPlugInVersionMinor();
    Json::Value manifest;
    if(ReadDatasetManifest(manifest)) {
        v["ClimatologyDatasetVersion"] = manifest.get("dataset_version", "unknown");
        const Json::Value period = manifest["climatology_period"];
        v["ClimatologyDatasetPeriodStart"] = period.get("start", "");
        v["ClimatologyDatasetPeriodEnd"] = period.get("end", "");
        v["ClimatologyDatasetPeriodKind"] = period.get("kind", "");
    } else {
        v["ClimatologyDatasetVersion"] = "legacy/unversioned";
    }
    const bool ready = valid && g_pOverlayFactory &&
                       g_pOverlayFactory->IsReady();
    v["ClimatologyReady"] = ready;
    v["ClimatologyLoadState"] = !valid ? "unavailable" :
        (ready ? "ready" : "loading");

    v["ClimatologyDataPtr"] = FunctionPointerText(
        valid ? ClimatologyData : nullptr);
    v["ClimatologyWindAtlasDataPtr"] = FunctionPointerText(
        valid ? ClimatologyWindAtlasData : nullptr);
    v["ClimatologyCycloneTrackCrossingsPtr"] = FunctionPointerText(
        valid ? ClimatologyCycloneTrackCrossings : nullptr);

    v["ClimatologyQueryAPIVersion"] = climatology_api::QUERY_API_V1;
    v["ClimatologyQueryV1Ptr"] = FunctionPointerText(
        valid ? ClimatologyQueryV1 : nullptr);
    Json::Value scenarios(Json::arrayValue);
    scenarios.append("all-years");
    v["ClimatologyQueryCapabilities"]["scenarios"] = scenarios;
    Json::Value variables(Json::arrayValue);
    variables.append("wind-10m-distribution");
    variables.append("surface-current-vector");
    variables.append("sea-level-pressure");
    variables.append("sea-surface-temperature");
    variables.append("air-temperature");
    variables.append("cloud-cover");
    variables.append("precipitation");
    variables.append("relative-humidity");
    variables.append("lightning");
    variables.append("sea-depth");
    v["ClimatologyQueryCapabilities"]["variables"] = variables;

    Json::FastWriter writer;
    SendPluginMessage(wxT("CLIMATOLOGY"), writer.write( v ));
}

void climatology_pi::SetPluginMessage(wxString &message_id, wxString &message_body)
{
    if(message_id == "CLIMATOLOGY_REQUEST") {
        SendClimatology(true);
    }
}

// -------------------------------------------------------
// GRIBMELINE is a misnomer
void climatology_pi::SendTimelineMessage(wxDateTime time)
{
    Json::Value v;
    if (time.IsValid()) {
        v["Day"] = time.GetDay();
        v["Month"] = time.GetMonth();
        v["Year"] = time.GetYear();
        v["Hour"] = time.GetHour();
        v["Minute"] = time.GetMinute();
        v["Second"] = time.GetSecond();
    } else {
        v["Day"] = -1;
        v["Month"] = -1;
        v["Year"] = -1;
        v["Hour"] = -1;
        v["Minute"] = -1;
        v["Second"] = -1;
    }
    Json::FastWriter writer;
    SendPluginMessage("GRIB_TIMELINE", writer.write( v ));
}

void climatology_pi::FreeData()
{
    m_dataset_load_timer.Stop();
    delete g_pOverlayFactory;
    g_pOverlayFactory = NULL;
    if(m_pClimatologyDialog) {
        m_pClimatologyDialog->Save();
        //m_pClimatologyDialog->Destroy();
        delete m_pClimatologyDialog;
        m_pClimatologyDialog = nullptr;
    }
}

bool climatology_pi::LoadConfig(void)
{
    wxFileConfig *pConf = (wxFileConfig *)m_pconfig;

    if(!pConf)
        return false;

    pConf->SetPath (  "/Settings/Climatology"  );

    m_climatology_dialog_sx = pConf->Read ( "DialogSizeX" , 300L );
    m_climatology_dialog_sy = pConf->Read ( "DialogSizeY" , 540L );
    m_climatology_dialog_x =  pConf->Read ( "DialogPosX" , 20L );
    m_climatology_dialog_y =  pConf->Read ( "DialogPosY" , 170L );

    return true;
}

bool climatology_pi::SaveConfig(void)
{
    wxFileConfig *pConf = (wxFileConfig *)m_pconfig;

    if(!pConf)
        return false;

    pConf->SetPath ("/Settings/Climatology");

    pConf->Write("DialogSizeX", m_climatology_dialog_sx );
    pConf->Write("DialogSizeY", m_climatology_dialog_sy );
    pConf->Write("DialogPosX",  m_climatology_dialog_x );
    pConf->Write("DialogPosY",  m_climatology_dialog_y );

    return true;
}

void climatology_pi::SetColorScheme(PI_ColorScheme cs)
{
    DimeWindow(m_pClimatologyDialog);
}
