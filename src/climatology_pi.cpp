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

#include <GL/glew.h>

#include "wx/wxprec.h"
#ifndef WX_PRECOMP
  #include "wx/wx.h"
  #include <wx/glcanvas.h>
#endif

#include <wx/treectrl.h>
#include <wx/fileconf.h>
#include <wx/log.h>
#include <wx/filename.h>
#include <wx/event.h>
#include <wx/timer.h>
#include <wx/aui/aui.h>
#include <wx/aui/framemanager.h>
#include <wx/progdlg.h>
#include <wx/generic/progdlgg.h>

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

ClimatologyOverlayFactory* g_pOverlayFactory = nullptr;

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
#endif  // CLIMATOLOGY_BUNDLED_CURL


//Begin  of Windows bundling of CURL
void Climatology_InitCurl()
{
#ifdef CLIMATOLOGY_BUNDLED_CURL
    LoadBundledCurl();
#else
    wxLogMessage("Climatology: using system curl");
#endif
}
//End of Windows bundling of CURL

// ---------------------------------------------------------------------------
// Utility helpers
// ---------------------------------------------------------------------------

wxString ClimatologyDataDirectory()
{
    wxFileName dir(GetPluginDataDir("climatology_pi"), "");
    dir.AppendDir("data");

    if (!dir.DirExists())
        dir.Mkdir(wxS_DIR_DEFAULT, wxPATH_MKDIR_FULL);

    return dir.GetFullPath();
}

void climatology_pi::ShowTransientMessage(const wxString& message,
                                          const wxString& title,
                                          int timeout_ms)
{
    wxFrame* popup = new wxFrame(
        m_parent_window,
        wxID_ANY,
        title,
        wxDefaultPosition,
        wxSize(320, 120),
        wxFRAME_NO_TASKBAR | wxSTAY_ON_TOP | wxBORDER_SIMPLE
    );

    wxPanel* panel = new wxPanel(popup);
    wxBoxSizer* sizer = new wxBoxSizer(wxVERTICAL);

    wxStaticText* text = new wxStaticText(panel, wxID_ANY, message);
    text->Wrap(300);

    sizer->Add(text, 1, wxALL | wxALIGN_CENTER, 10);
    panel->SetSizer(sizer);

    popup->CentreOnParent();
    popup->Show();

    wxTimer* timer = new wxTimer(popup);
    timer->Bind(wxEVT_TIMER, [popup, timer](wxTimerEvent&) {
        popup->Destroy();
        timer->Stop();
        delete timer;
    });
    timer->StartOnce(timeout_ms);
}

void climatology_pi::BootstrapDataDirectory()
{
    wxString dataDir = ClimatologyDataDirectory();
    wxFileName::Mkdir(dataDir, wxS_DIR_DEFAULT, wxPATH_MKDIR_FULL);
}

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

// ---------------------------------------------------------------------------
// Plugin core
// ---------------------------------------------------------------------------

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


climatology_pi::~climatology_pi()
{
      FreeData();
}



int climatology_pi::Init()
{
    // --- 1. Initialize curl global state FIRST ---
    curl_global_init(CURL_GLOBAL_DEFAULT);
	
	// Initialize params
	memset(&m_params, 0, sizeof(m_params));
	m_params.overlayType = OVERLAY_WIND;   // or any default you prefer


    // --- 2. Load bundled curl BEFORE any curl_easy_init() ---
    Climatology_InitCurl();

    // --- 3. Localization ---
    AddLocaleCatalog(_T("opencpn-climatology_pi"));

    // --- 4. UI and plugin setup ---
    m_climatology_dialog_x = 0;
    m_climatology_dialog_y = 0;
    m_climatology_dialog_sx = 200;
    m_climatology_dialog_sy = 400;

	//Retrieves current screen resolution in pixles of primary display
    ::wxDisplaySize(&m_display_width, &m_display_height);

	// OpenCPN?s global configuration object - how plugins read/write persistant settings.
    m_pconfig = GetOCPNConfigObject();
	
	//Retrieves a pointer to the main OpenCPN chart canvas window.
	//plugins must attach dialogs to the canvas window so:
	// they stay on top, minimize/restore with OpenCPN, inherit theme
	// they receive keyboard/mouse focus correctly
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

    // Tell OpenCPN that climatology is active
    SendClimatology(true);

    // --------------------------------------------------------------------
    // IMPORTANT: Do NOT load data, manifest, or create DownloadManager here.
    // Init() must return quickly so the toolbar and plugin icon appear
    // immediately. All heavy work is deferred to OnStartupTimer().
    // --------------------------------------------------------------------

	// --- 5. Bind the startup timer event BEFORE starting the timer ---
	// Bind the timer to the plugin object itself, but using the wxEvtHandler base of the plugin.
	this->Bind(wxEVT_TIMER,
			   &climatology_pi::OnStartupTimer,
			   this,
			   m_startupTimer.GetId());
		   
	// --- 6. Start the deferred startup timer ---
    // This fires ~500ms after Init() returns, allowing OpenCPN to finish
    // startup and show the toolbar before climatology begins heavy work.	
	m_startupTimer.StartOnce(500);

    // --- 7. Return plugin capability flags ---
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
	m_parent_window->Unbind(EVT_DM_COMPLETE,
			&climatology_pi::OnDownloadComplete,
			this);
	
	curl_global_cleanup();
    SendClimatology(false);
    FreeData();
    RemovePlugInTool(m_leftclick_tool_id);
    return true;
}


// ---------------------------------------------------------------------------
// Overlay factory creation and startup
// ---------------------------------------------------------------------------


void climatology_pi::CreateOverlayFactory()
{
    // Create overlay factory once
    if (!g_pOverlayFactory) {
        wxLogMessage("Climatology: Creating overlay factory");
        g_pOverlayFactory = new ClimatologyOverlayFactory(m_parent_window);
    }
}


void climatology_pi::OnStartupTimer(wxTimerEvent &event)
{
    wxLogMessage("Climatology: Startup timer fired");
    m_startupTimer.Stop();   // one-shot
	
	CreateOverlayFactory();
 
    // --- 1. Ensure data directory exists ---
    wxString dir = ClimatologyDataDirectory();
    if (!wxDirExists(dir)) {
        wxMessageBox(
            _("Climatology data directory is missing.\n"
              "A new directory will be created and data will be downloaded."),
            _("Climatology"),
            wxOK | wxICON_WARNING
        );
        wxMkdir(dir);
    }

 	// --- 2. Construct downloadmanager
	m_downloadManager = std::make_unique<DownloadManager>(
		m_parent_window,
		ClimatologyDataDirectory()
	);

    // --- 3. Load manifest.json ---
    wxString manifestPath = dir + "manifest.json";
	
    ManifestLoader loader(manifestPath.ToStdString());
    std::vector<ManifestEntry> manifestEntries;

    if (!loader.Load(manifestEntries)) {
        wxLogWarning("Climatology: manifest.json missing or invalid ? forcing download");

        wxNotificationMessage msg(
            _("Climatology"),
            _("Downloading climatology data?"),
            m_parent_window
        );
        msg.Show(3000);   // portable, works on MSVC

        m_parent_window->Bind(EVT_DM_COMPLETE,
                              &climatology_pi::OnDownloadComplete,
                              this);

        m_downloadManager->StartBackgroundDownload(true);
        m_bCompletedLoading = false;
        return;
    }

    // --- 4. Convert manifest to file list ---
    std::vector<DownloadFileEntry> files = ConvertManifest(manifestEntries);
    m_downloadManager->SetManifest(files);

    // --- 5. Check if all files are present ---
 	if (m_downloadManager->AllFilesPresent()) {
		wxLogMessage("Climatology: All data present — loading climatology data");

		// Load NOAA data into COF
		g_pOverlayFactory->Load();
		m_bCompletedLoading = true;
		return;
	}

    // --- 7. Missing files ? start download ---
    wxLogMessage("Climatology: Missing data files ? starting download");

	ShowTransientMessage(
		_("Downloading climatology data?"),
		_("Climatology"),
		3000
	);

    m_parent_window->Bind(EVT_DM_COMPLETE,
                          &climatology_pi::OnDownloadComplete,
                          this);

    m_downloadManager->StartBackgroundDownload(true);
    m_bCompletedLoading = false;
}


void climatology_pi::OnDownloadComplete(wxCommandEvent &event)
{
    wxLogMessage("Climatology: Download complete ? checking results");

    // Unbind completion event
    m_parent_window->Unbind(EVT_DM_COMPLETE,
                            &climatology_pi::OnDownloadComplete,
                            this);

    // Cleanup progress dialog + unbind progress events
    if (m_downloadManager) {
        m_downloadManager->DestroyProgressDialog();
    }

    // Check if all files downloaded successfully
    if (!m_downloadManager->AllFilesPresent()) {
        wxLogWarning("Climatology: Some files failed to download");

        int res = wxMessageBox(
            _("Some climatology files failed to download.\n"
              "Would you like to retry?"),
            _("Climatology Download Error"),
            wxYES_NO | wxICON_WARNING
        );

        if (res == wxYES) {
            wxLogMessage("Climatology: Retrying download");
            m_parent_window->Bind(EVT_DM_COMPLETE,
                                  &climatology_pi::OnDownloadComplete,
                                  this);
            m_downloadManager->StartBackgroundDownload(true);
            return;
        }

        wxLogWarning("Climatology: User declined retry ? plugin will not load data");
        m_bCompletedLoading = false;
        return;
    }

    // Success
    wxLogMessage("Climatology: All files downloaded successfully");
	
	ShowTransientMessage(
    _("Download complete ? click the toolbar icon to load."),
    _("Climatology"),
    3000
	);

    wxLogMessage("Climatology: All files downloaded — loading climatology data");

	g_pOverlayFactory->Load();
	m_bCompletedLoading = true;
}

// ---------------------------------------------------------------------------
// Toolbar and dialogs
// ---------------------------------------------------------------------------

// climatology_pi.cpp
void climatology_pi::OnToolbarToolCallback(int id)
{
    if (id == m_toolbar_item_id)
    {
        if (!m_pControlDialog)
        {
            m_pControlDialog = new ClimatologyControlDialog(
                GetOCPNCanvasWindow(),
                this,
                g_pOverlayFactory   // correct factory pointer
            );
        }

        m_pControlDialog->Show();
        m_pControlDialog->Raise();
    }
}

// ---------------------------------------------------------------------------
// Overlay callbacks (minimal wrappers)
// ---------------------------------------------------------------------------

void ClimatologyOverlayFactory::Render(const ClimatologyRenderParams& p)
{
    // --------------------------------------------------------------------
    // 1. Bail out early if no data is loaded
    // --------------------------------------------------------------------
    if (!m_bCompletedLoading) {
        // Nothing to render yet
        return;
    }

    // --------------------------------------------------------------------
    // 2. GL path (p.dc == nullptr)
    // --------------------------------------------------------------------
    if (p.dc == nullptr) {

        // Ensure GL state is correct for climatology rendering
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

        // Bind shader program
        glUseProgram(m_shaderProgram);

        // Update uniforms based on viewport
        glUniform2f(m_uViewportSize,
                    (float)p.vp->pix_width,
                    (float)p.vp->pix_height);

        glUniform1f(m_uOpacity, p.opacity);
        glUniform1i(m_uOverlayType, p.overlayType);

        // Set date/time uniform
        glUniform1f(m_uJulianDay, p.julianDay);

        // Set geographic transform
        glUniform2f(m_uLatLon,
                    (float)p.cursorLat,
                    (float)p.cursorLon);

        // Render the overlay using GL primitives
        RenderGL(p);

        // Cleanup
        glUseProgram(0);
        return;
    }

    // --------------------------------------------------------------------
    // 3. DC path (p.dc != nullptr)
    // --------------------------------------------------------------------
    piDC* dc = p.dc;

    // Set transparency
    dc->SetPen(wxPen(wxColour(0,0,0,0)));
    dc->SetBrush(wxBrush(wxColour(0,0,0,0)));

    // Render using wxDC primitives
    RenderDC(*dc, p);

    // Done
}

void ClimatologyOverlayFactory::RenderGL(const ClimatologyRenderParams& p)
{
    // Example: draw wind barbs, arrows, scalar fields, etc.
    for (const auto& cell : m_gridCells) {

        // Convert lat/lon → screen XY using viewport
        wxPoint2DDouble xy = LatLonToScreen(p.vp, cell.lat, cell.lon);

        // Draw GL primitives
        DrawGLCell(cell, xy, p);
    }
}

void ClimatologyOverlayFactory::RenderDC(piDC& dc,
                                         const ClimatologyRenderParams& p)
{
    for (const auto& cell : m_gridCells) {

        wxPoint xy = LatLonToScreenDC(p.vp, cell.lat, cell.lon);

        // Draw DC primitives
        DrawDCCell(dc, cell, xy, p);
    }
}


bool climatology_pi::RenderOverlay(wxDC &dc, PlugIn_ViewPort *vp)
{
    // --- 1. Bail out if dialog or factory is not ready ---
    if (!m_pClimatologyDialog ||
        !m_pClimatologyDialog->IsShown() ||
        !g_pOverlayFactory)
        return false;

    // --- 2. Wrap wxDC in piDC (OpenCPN’s DC abstraction) ---
    piDC pidc(dc);

    // --- 3. Build render parameters (DC path) ---
    ClimatologyRenderParams p =
        m_pClimatologyDialog->BuildRenderParams(vp, &pidc);

    // --- 4. Render using the overlay factory (DC mode) ---
    g_pOverlayFactory->Render(p);

    return true;
}





// ---------------------------------------------------------------------------
// Messaging, params, cursor
// ---------------------------------------------------------------------------

void climatology_pi::SetCursorLatLon(double lat, double lon)
{
    if(m_pClimatologyDialog)
        m_pClimatologyDialog->SetCursorLatLon(lat, lon);
	
	wxLogMessage("Climatology: SetCursorLatLon plugin: lat=%f lon=%f dlg=%p",
             lat, lon, m_pClimatologyDialog);
}


void climatology_pi::SetStandardDisplayParams(const StandardDisplayParams& p)
{
    m_params = p;
}

const StandardDisplayParams& climatology_pi::GetStandardDisplayParams() const
{
    return m_params;
}


void climatology_pi::SendClimatology(const StandardDisplayParams& p)
{
    if (g_pOverlayFactory) {
        g_pOverlayFactory->SetParams(p);
        g_pOverlayFactory->RequestRefresh();
    }
}



void climatology_pi::SendClimatology(bool valid)
{
    Json::Value v;
    v["ClimatologyVersionMajor"] = GetPlugInVersionMajor();
    v["ClimatologyVersionMinor"] = GetPlugInVersionMinor();

    Json::FastWriter writer;
    SendPluginMessage(wxT("CLIMATOLOGY"), writer.write(v));
}


void climatology_pi::SetPluginMessage(wxString &message_id, wxString &message_body)
{
    if(message_id == "CLIMATOLOGY_REQUEST") {
        SendClimatology(true);
    }
}


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

// ---------------------------------------------------------------------------
// Config and cleanup
// ---------------------------------------------------------------------------

void climatology_pi::FreeData()
{
    delete g_pOverlayFactory;
    g_pOverlayFactory = nullptr;

    if (m_pClimatologyDialog) {
        m_pClimatologyDialog->Save();
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

// ---------------------------------------------------------------------------
// Plugin exports
// ---------------------------------------------------------------------------
int climatology_pi::GetAPIVersionMajor()  { return OCPN_API_VERSION_MAJOR; }
int climatology_pi::GetAPIVersionMinor()  { return OCPN_API_VERSION_MINOR; }
int climatology_pi::GetPlugInVersionMajor() { return PLUGIN_VERSION_MAJOR; }
int climatology_pi::GetPlugInVersionMinor() { return PLUGIN_VERSION_MINOR; }
int climatology_pi::GetPlugInVersionPatch() { return PLUGIN_VERSION_PATCH; }
int climatology_pi::GetPlugInVersionPost()  { return PLUGIN_VERSION_TWEAK; }

wxBitmap* climatology_pi::GetPlugInBitmap() { return &m_panelBitmap; }

wxString climatology_pi::GetCommonName()        { return _T(PLUGIN_COMMON_NAME); }
wxString climatology_pi::GetShortDescription()  { return _(PLUGIN_SHORT_DESCRIPTION); }
wxString climatology_pi::GetLongDescription()   { return _(PLUGIN_LONG_DESCRIPTION); }

void climatology_pi::SetDefaults(void) {}
int climatology_pi::GetToolbarToolCount(void) { return 1; }

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


