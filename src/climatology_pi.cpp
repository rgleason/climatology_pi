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

//#define GLEW_STATIC
#include <GL/glew.h>

#include "wx/wxprec.h"

#ifndef  WX_PRECOMP
  #include "wx/wx.h"
  #include <wx/glcanvas.h>
#endif 

#include <wx/treectrl.h>
#include <wx/fileconf.h>
#include <wx/log.h>
#include <wx/filename.h>
#include <wx/log.h>

#include "ocpn_plugin.h"
#include "climatology_shaders.h"


#include "icons.h"
#include "climatology_pi.h"
#include "ClimatologyOverlayFactory.h"
#include "ClimatologyDialog.h"
#include "ManifestLoader.hpp"
#include "DownloadManager.hpp"
#include "DownloadFileEntry.hpp"
#include <curl/curl.h>



#ifdef CLIMATOLOGY_BUNDLED_CURL
#include <windows.h>


static bool LoadBundledCurl()
{
    wxString pluginDir = GetPluginDataDir("climatology_pi");
    wxFileName curlDll(pluginDir, "libcurl.dll");

    if (!curlDll.FileExists()) {
        wxLogMessage("Climatology: bundled libcurl.dll not found at %s",
                     curlDll.GetFullPath());
        return false;
    }

    HMODULE hCurl = LoadLibraryW(curlDll.GetFullPath().wc_str());
    if (!hCurl) {
        wxLogMessage("Climatology: LoadLibrary failed for %s",
                     curlDll.GetFullPath());
        return false;
    }

    wxLogMessage("Climatology: bundled libcurl.dll loaded from %s",
                 curlDll.GetFullPath());
    return true;
}
#endif



ClimatologyOverlayFactory* g_pOverlayFactory = nullptr;


wxString ClimatologyDataDirectory()
{
    wxFileName dir(GetPluginDataDir("climatology_pi"), "");
    dir.AppendDir("data");

    if (!dir.DirExists())
        dir.Mkdir(wxS_DIR_DEFAULT, wxPATH_MKDIR_FULL);

    return dir.GetFullPath();
}



void climatology_pi::BootstrapDataDirectory()
{
    // Ensure the plugin's writable data directory exists
    wxString dataDir = ClimatologyDataDirectory();
    wxFileName::Mkdir(dataDir, wxS_DIR_DEFAULT, wxPATH_MKDIR_FULL);
}


void climatology_pi::SetColorScheme(PI_ColorScheme cs)
{
    // No-op: dialog does not implement color scheme changes
}



void Climatology_InitCurl()
{
#ifdef CLIMATOLOGY_BUNDLED_CURL
    LoadBundledCurl();
#else
    wxLogMessage("Climatology: using system curl");
#endif
}

climatology_pi::climatology_pi(void *ppimgr)
    : opencpn_plugin_120(ppimgr)
{
    m_pClimatologyDialog = nullptr;

    // Plugin installation directory (icons, metadata, curl DLL, cacert.pem)
    m_plugin_dir = GetPluginDataDir("climatology_pi");

    // Create the PlugIn icons (embedded SVGs + PNGs from icons.cpp)
    initialize_images();

    // Load PNG panel icon from <plugin_dir>/usericons/climatology_panel.png
    wxFileName fn(m_plugin_dir, "");
    fn.AppendDir("usericons");
    fn.SetFullName("climatology_panel.png");

    wxString pngPath = fn.GetFullPath();

    wxInitAllImageHandlers();

    wxLogDebug("Climatology: Loading PNG panel icon: " + pngPath);

    wxImage panelIcon(pngPath);
    if (panelIcon.IsOk()) {
        m_panelBitmap = wxBitmap(panelIcon);
        wxLogMessage("Climatology: Loaded PNG panel icon");
    } else {
        wxLogWarning("Climatology panel icon has NOT been loaded");
    }
}

bool climatology_pi::RenderGLOverlay(wxGLContext *pcontext, PlugIn_ViewPort *vp)
{
    if (!g_pOverlayFactory)
        return false;

    g_pOverlayFactory->RenderGLOverlay(pcontext, vp);
    return true;
}


climatology_pi::~climatology_pi()
{
      FreeData();
}

int climatology_pi::Init()
{
    // --- 1. Initialize curl global state FIRST ---
    curl_global_init(CURL_GLOBAL_DEFAULT);

    // --- 2. Load bundled curl BEFORE any curl_easy_init() ---
    Climatology_InitCurl();

    // --- 3. Localization ---
    AddLocaleCatalog(_T("opencpn-climatology_pi"));

    // --- 4. UI and plugin setup ---
    m_climatology_dialog_x = 0;
    m_climatology_dialog_y = 0;
    m_climatology_dialog_sx = 200;
    m_climatology_dialog_sy = 400;

    ::wxDisplaySize(&m_display_width, &m_display_height);

    m_pconfig = GetOCPNConfigObject();
    m_parent_window = GetOCPNCanvasWindow();

#ifdef PLUGIN_USE_SVG
    m_leftclick_tool_id = InsertPlugInToolSVG(
        _T("Climatology"),
        _svg_climatology, _svg_climatology, _svg_climatology_toggled,
        wxITEM_CHECK, _("Climatology"), _T(""), NULL,
        CLIMATOLOGY_TOOL_POSITION, 0, this);
#else
    m_leftclick_tool_id = InsertPlugInTool(
        _T(""), _img_climatology, _img_climatology,
        wxITEM_NORMAL, _("Climatology"), _T(""), NULL,
        CLIMATOLOGY_TOOL_POSITION, 0, this);
#endif

    SendClimatology(true);

    // Ensure plugin data directory exists
    BootstrapDataDirectory();
	
    // Load climatology shaders
    LoadClimatologyShaders();

    // Configure shader with initial viewport
    ConfigureClimatologyShader(m_display_width, m_display_height);
	
    // Load manifest.json
    wxString manifestPath = m_plugin_dir + "/manifest.json";

    ManifestLoader loader(manifestPath.ToStdString());
    std::vector<ManifestEntry> manifestEntries;

    if (!loader.Load(manifestEntries)) {
        wxLogWarning("Climatology: manifest.json failed to load");
        return (WANTS_OVERLAY_CALLBACK |
                WANTS_OPENGL_OVERLAY_CALLBACK |
                WANTS_CURSOR_LATLON |
                WANTS_TOOLBAR_CALLBACK |
                INSTALLS_TOOLBAR_TOOL |
                WANTS_CONFIG |
                WANTS_PLUGIN_MESSAGING);
    }

	// Convert manifest entries into DownloadFileEntry list (build full URLs)
	std::vector<DownloadFileEntry> files = ConvertManifest(manifestEntries);

    // Create DownloadManager
    m_downloadManager = std::make_unique<DownloadManager>(
        m_parent_window,
        ClimatologyDataDirectory()
    );

    // Give it the file list
    m_downloadManager->SetManifest(files);

    // Bind completion event
    m_parent_window->Bind(
        EVT_DM_COMPLETE,
        &climatology_pi::OnDownloadComplete,
        this
    );

    // Start background download
    m_downloadManager->StartBackgroundDownload(true);

    return (WANTS_OVERLAY_CALLBACK |
            WANTS_OPENGL_OVERLAY_CALLBACK |
            WANTS_CURSOR_LATLON |
            WANTS_TOOLBAR_CALLBACK |
            INSTALLS_TOOLBAR_TOOL |
            WANTS_CONFIG |
            WANTS_PLUGIN_MESSAGING);
}


bool climatology_pi::DeInit()
{
	m_parent_window->Unbind(EVT_DM_COMPLETE, &climatology_pi::OnDownloadComplete, this);
	
	curl_global_cleanup();
    SendClimatology(false);
    FreeData();
    RemovePlugInTool(m_leftclick_tool_id);
    return true;
}

int climatology_pi::GetAPIVersionMajor() 
{ return OCPN_API_VERSION_MAJOR; }

int climatology_pi::GetAPIVersionMinor() 
{ return OCPN_API_VERSION_MINOR; }

int climatology_pi::GetPlugInVersionMajor()
{ return PLUGIN_VERSION_MAJOR; }

int climatology_pi::GetPlugInVersionMinor()
{  return PLUGIN_VERSION_MINOR; }

int climatology_pi::GetPlugInVersionPatch()
{ return PLUGIN_VERSION_PATCH; }

int climatology_pi::GetPlugInVersionPost()
{ return PLUGIN_VERSION_TWEAK; }

// Uses climatology_panel.png file to make the bitmap.
wxBitmap *climatology_pi::GetPlugInBitmap()  { return &m_panelBitmap; }

wxString climatology_pi::GetCommonName()
{ return _T(PLUGIN_COMMON_NAME); }

wxString climatology_pi::GetShortDescription()
{ return _(PLUGIN_SHORT_DESCRIPTION); }

wxString climatology_pi::GetLongDescription()
{ return _(PLUGIN_LONG_DESCRIPTION); }

void climatology_pi::CreateOverlayFactory()
{
    // If dialog already exists, nothing to do
    if (m_pClimatologyDialog)
        return;

    // Create dialog FIRST
    LoadConfig();
    m_pClimatologyDialog = new ClimatologyDialog(m_parent_window, this);
    m_pClimatologyDialog->Move(wxPoint(m_climatology_dialog_x, m_climatology_dialog_y));

    wxIcon icon;
    icon.CopyFromBitmap(*_img_climatology);
    m_pClimatologyDialog->SetIcon(icon);

    // Now create the overlay factory
    if (!g_pOverlayFactory) {
        g_pOverlayFactory = new ClimatologyOverlayFactory(*m_pClimatologyDialog);
    }

    // If data is still loading, disable controls and show status
	if (!g_pOverlayFactory->m_bCompletedLoading) {
    m_pClimatologyDialog->EnableAllControls(false);
    m_pClimatologyDialog->m_tStatus->SetLabel("Loading climatology data...");
    m_pClimatologyDialog->Show(true);   // ⭐ keep dialog visible
    return;
	}

    // Data is already loaded and ready — update UI
    SendClimatology(true);
    m_pClimatologyDialog->UpdateTrackingControls();
    m_pClimatologyDialog->FitLater();
    m_pClimatologyDialog->Hide();
}


void climatology_pi::SetDefaults(void)
{
}

int climatology_pi::GetToolbarToolCount(void)
{
      return 1;
}

static bool ClimatologyData(int setting, wxDateTime &date, double lat, double lon,
                            double &dir, double &speed)
{
    // Ensure factory exists prevents WeatherRouting from querying 
	// climatology while data is still loading.
	if (!g_pOverlayFactory || !g_pOverlayFactory->m_bCompletedLoading)
    return false;

    speed = g_pOverlayFactory->getValue(MAG, setting, lat, lon, &date);
    if (isnan(speed))
        return false;

    dir = g_pOverlayFactory->getValue(DIRECTION, setting, lat, lon, &date);
    if (isnan(dir))
        return false;

    return true;
}


static bool ClimatologyWindAtlasData(wxDateTime &date, double lat, double lon,
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


void climatology_pi::SetCursorLatLon(double lat, double lon)
{
    if(m_pClimatologyDialog)
        m_pClimatologyDialog->SetCursorLatLon(lat, lon);
	
	wxLogMessage("Climatology: SetCursorLatLon plugin: lat=%f lon=%f dlg=%p",
             lat, lon, m_pClimatologyDialog);
}

void climatology_pi::SendClimatology(bool valid)
{
    Json::Value v;
    v["ClimatologyVersionMajor"] = GetPlugInVersionMajor();
    v["ClimatologyVersionMinor"] = GetPlugInVersionMinor();

    char ptr[64];
    snprintf(ptr, sizeof ptr, "%p", valid ? ClimatologyData : NULL);
    v["ClimatologyDataPtr"] = ptr;

    snprintf(ptr, sizeof ptr, "%p", valid ? ClimatologyWindAtlasData : NULL);
    v["ClimatologyWindAtlasDataPtr"] = ptr;

    snprintf(ptr, sizeof ptr, "%p", valid ? ClimatologyCycloneTrackCrossings : NULL);
    v["ClimatologyCycloneTrackCrossingsPtr"] = ptr;

    Json::FastWriter writer;
    SendPluginMessage(wxT("CLIMATOLOGY"), writer.write( v ));
}

void climatology_pi::SetPluginMessage(wxString &message_id, wxString &message_body)
{
    if(message_id == "CLIMATOLOGY_REQUEST") {
        SendClimatology(true);
    }
}

void climatology_pi::OnDownloadComplete(wxCommandEvent& event)
{
    wxLogMessage("Climatology_pi: Downloads complete — loading data");

    // Unbind event to avoid duplicate callbacks
    m_parent_window->Unbind(EVT_DM_COMPLETE,
        &climatology_pi::OnDownloadComplete, this);

    // Clean up download manager
    m_downloadManager.reset();

    // Ensure dialog/factory exist
    if (!m_pClimatologyDialog)
        CreateOverlayFactory();

    // Load climatology data from disk
    g_pOverlayFactory->Load();

    if (!g_pOverlayFactory->m_bCompletedLoading) {
        wxLogMessage("Climatology_pi: Data load failed");
        if (m_pClimatologyDialog) {
            m_pClimatologyDialog->EnableAllControls(false);
            m_pClimatologyDialog->m_tStatus->SetLabel("Climatology data load failed.");
            m_pClimatologyDialog->Show(true);
        }
        return;
    }

    // Success
    if (m_pClimatologyDialog) {
        m_pClimatologyDialog->EnableAllControls(true);
        m_pClimatologyDialog->m_tStatus->SetLabel("Climatology data loaded.");
        m_pClimatologyDialog->UpdateTrackingControls();
        m_pClimatologyDialog->Show(true);
    }

    wxLogMessage("Climatology_pi: Data load complete");
}


// -------------------------------------------------------
// GRIBTIMELINE is a misnomer
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


// void climatology_pi::SetColorScheme(PI_ColorScheme cs)
// {
//    DimeWindow(m_pClimatologyDialog);
// }

extern "C" DECL_EXP int GetAPIVersionMajor()
{
    return OCPN_API_VERSION_MAJOR;   // 1
}

extern "C" DECL_EXP int GetAPIVersionMinor()
{
    return OCPN_API_VERSION_MINOR;   // 20
}

extern "C" DECL_EXP int GetPlugInVersionMajor()
{
    return PLUGIN_VERSION_MAJOR;     // 1
}

extern "C" DECL_EXP int GetPlugInVersionMinor()
{
    return PLUGIN_VERSION_MINOR;     // 6
}

extern "C" DECL_EXP int GetPlugInVersionPatch()
{
    return PLUGIN_VERSION_PATCH;     // 38
}

extern "C" DECL_EXP int GetPlugInVersionPost()
{
    return PLUGIN_VERSION_TWEAK;     // 2
}

extern "C" DECL_EXP const char* GetCommonName()
{
    return PLUGIN_COMMON_NAME;       // "Climatology"
}

extern "C" DECL_EXP const char* GetShortDescription()
{
    return PLUGIN_SHORT_DESCRIPTION;
}

extern "C" DECL_EXP const char* GetLongDescription()
{
    return PLUGIN_LONG_DESCRIPTION;
}

extern "C" DECL_EXP opencpn_plugin* create_pi(void* ppimgr)
{
    return new climatology_pi(ppimgr);
}

extern "C" DECL_EXP void destroy_pi(opencpn_plugin* p)
{
    delete p;
}
