/******************************************************************************
 * Climatology Plugin — Main Plugin Class (Implementation)
 * Modern unified‑grid architecture
 ******************************************************************************/
#pragma message("Header: " __FILE__)

// wxWidgets must be first
#include <wx/wxprec.h>
#ifndef WX_PRECOMP
#include <wx/wx.h>
#endif

// To avoid wxWidgets\include\wx\withimages.h(19,1): error C2236: unexpected token 'class'.
#include "ocpn_plugin_guarded.h"


// wx extras
#include <wx/fileconf.h>

// ADD nlohmann/json
#include "json.hpp"
using json = nlohmann::json;

// plugin headers
#include "climatology_pi.h"
#include "ClimatologyDialog.h"
#include "ClimatologyConfigDialog.h"
#include "ClimatologyOverlayFactory.h"
#include "ManifestLoader.hpp"
#include "DownloadManager.hpp"
#include "DownloadFileEntry.hpp"
#include "icons.h"


// MUST COME AFTER wx includes
#include "version.h"

#include <wx/progdlg.h>
#include <wx/generic/progdlgg.h>
#include <wx/aui/aui.h>
#include <wx/notifmsg.h>

#include <curl/curl.h>
#ifdef CLIMATOLOGY_BUNDLED_CURL
#include <windows.h>
#endif

// Global overlay factory
ClimatologyOverlayFactory* g_pOverlayFactory = nullptr;


//wxBEGIN_EVENT_TABLE(climatology_pi, wxEvtHandler)
//    EVT_TIMER(wxID_ANY, climatology_pi::OnStartupTimer)
//wxEND_EVENT_TABLE()


// ---------------------------------------------------------------------------
// Utility: Data directory
// ---------------------------------------------------------------------------
wxString ClimatologyDataDirectory()
{
    wxFileName dir(GetPluginDataDir("climatology_pi"), "");
    dir.AppendDir("data");

    if (!dir.DirExists())
        dir.Mkdir(wxS_DIR_DEFAULT, wxPATH_MKDIR_FULL);

    return dir.GetFullPath();
}

// ---------------------------------------------------------------------------
// Constructor
// ---------------------------------------------------------------------------

climatology_pi::climatology_pi(void* ppimgr)
    : opencpn_plugin_120(ppimgr)
{
    m_pClimatologyDialog = nullptr;
    m_pConfigDialog      = nullptr;

    m_plugin_dir = GetPluginDataDir("climatology_pi");

    initialize_images();  // load embedded icons

    // Load panel icon
    wxFileName fn(m_plugin_dir, "");
    fn.AppendDir("usericons");
    fn.SetFullName("climatology_panel.png");

    wxImage panelIcon(fn.GetFullPath());
    if (panelIcon.IsOk())
        m_panelBitmap = wxBitmap(panelIcon);
}

// ---------------------------------------------------------------------------
// Destructor
// ---------------------------------------------------------------------------
climatology_pi::~climatology_pi()
{
    FreeData();
}

// ---------------------------------------------------------------------------
// Init — plugin startup
// ---------------------------------------------------------------------------
int climatology_pi::Init(void)
{
    curl_global_init(CURL_GLOBAL_DEFAULT);
 //   Climatology_InitCurl();   

    memset(&m_params, 0, sizeof(m_params));
    m_params.overlayType = OVERLAY_WIND;

    AddLocaleCatalog(_T("opencpn-climatology_pi"));

    m_parent_window = GetOCPNCanvasWindow();
    m_pconfig       = GetOCPNConfigObject();

    // Insert toolbar icon
    m_leftclick_tool_id = InsertPlugInTool(
        _T("Climatology"),
        _img_climatology,
        _img_climatology,
        wxITEM_CHECK,
        _("Climatology"),
        _T(""),
        nullptr,
        -1,
        0,
        this
    );

    SendClimatology(true);

    // Startup timer (no wxBEGIN_EVENT_TABLE)
    m_startupTimer.SetOwner(m_parent_window);
    m_startupTimer.StartOnce(500);

    return (WANTS_OVERLAY_CALLBACK |
            WANTS_OPENGL_OVERLAY_CALLBACK |
            WANTS_CURSOR_LATLON |
            WANTS_TOOLBAR_CALLBACK |
            INSTALLS_TOOLBAR_TOOL |
            WANTS_CONFIG |
            WANTS_PLUGIN_MESSAGING);
}

// ---------------------------------------------------------------------------
// DeInit — plugin shutdown
// ---------------------------------------------------------------------------
bool climatology_pi::DeInit(void)
{
    curl_global_cleanup();
    SendClimatology(false);
    FreeData();
    RemovePlugInTool(m_leftclick_tool_id);
    return true;
}

// ---------------------------------------------------------------------------
// Create overlay factory (once)
// ---------------------------------------------------------------------------
void climatology_pi::CreateOverlayFactory()
{
    if (!g_pOverlayFactory)
        g_pOverlayFactory = new ClimatologyOverlayFactory(m_parent_window);
}

// ---------------------------------------------------------------------------
// Startup timer — load data or download if missing
// ---------------------------------------------------------------------------
void climatology_pi::OnStartupTimer(wxTimerEvent& event)
{
    m_startupTimer.Stop();
    CreateOverlayFactory();

    BootstrapDataDirectory();

    wxString dir = ClimatologyDataDirectory();
    wxString manifestPath = dir + "/manifest.json";

    // Load manifest
    ManifestLoader loader(manifestPath.ToStdString());
    std::vector<ManifestEntry> manifestEntries;

    m_downloadManager = std::make_unique<DownloadManager>(m_parent_window, dir);

    if (!loader.Load(manifestEntries))
    {
        // Manifest missing → download everything
        m_parent_window->Bind(EVT_DM_COMPLETE,
                              &climatology_pi::OnDownloadComplete,
                              this);

        m_downloadManager->StartBackgroundDownload(true);
        m_bCompletedLoading = false;
        return;
    }

    // Convert manifest → file list
	 std::vector<DownloadFileEntry> files;
	for (auto& m : manifestEntries)
	{
		DownloadFileEntry f;
		f.filename = m.filename;
		f.url      = m.url;
		f.size     = m.size;
		files.push_back(f);
	}

    m_downloadManager->SetManifest(files);

    if (m_downloadManager->AllFilesPresent())
    {
        g_pOverlayFactory->Load();
        m_bCompletedLoading = true;
        return;
    }

    // Missing files → download
    m_parent_window->Bind(EVT_DM_COMPLETE,
                          &climatology_pi::OnDownloadComplete,
                          this);

    m_downloadManager->StartBackgroundDownload(true);
    m_bCompletedLoading = false;
}


void climatology_pi::BootstrapDataDirectory()
{
    wxString dir = ClimatologyDataDirectory();
    wxFileName::Mkdir(dir, wxS_DIR_DEFAULT, wxPATH_MKDIR_FULL);
}


// ---------------------------------------------------------------------------
// Download complete
// ---------------------------------------------------------------------------
void climatology_pi::OnDownloadComplete(wxCommandEvent& event)
{
    m_parent_window->Unbind(EVT_DM_COMPLETE,
                            &climatology_pi::OnDownloadComplete,
                            this);

    if (!m_downloadManager->AllFilesPresent())
    {
        wxMessageBox(_("Some climatology files failed to download."),
                     _("Climatology"), wxOK | wxICON_WARNING);
        m_bCompletedLoading = false;
        return;
    }

    g_pOverlayFactory->Load();   // RAW DATA loaded here
    m_bCompletedLoading = true;
}

// ---------------------------------------------------------------------------
// Toolbar callback — show quick access dialog
// ---------------------------------------------------------------------------
void climatology_pi::OnToolbarToolCallback(int id)
{
    if (id != m_leftclick_tool_id)
        return;

    if (!m_pClimatologyDialog)
    {
		m_pClimatologyDialog = new ClimatologyDialog(
			m_parent_window,
			g_pOverlayFactory,
			wxID_ANY,
			_("Climatology"),
			wxDefaultPosition,
			wxDefaultSize
		);
    }

    m_pClimatologyDialog->Show();
    m_pClimatologyDialog->Raise();
}

// ---------------------------------------------------------------------------
// Render overlay (DC path)
// ---------------------------------------------------------------------------
bool climatology_pi::RenderOverlay(wxDC& dc, PlugIn_ViewPort* vp)
{
    if (!m_pClimatologyDialog ||
        !m_pClimatologyDialog->IsShown() ||
        !g_pOverlayFactory)
        return false;

    piDC pidc(&dc);   // correct constructor
 
	ClimatologyRenderParams p;
		m_pClimatologyDialog->BuildRenderParams(vp, &pidc, p);


    g_pOverlayFactory->Render(p);
    return true;
}

// ---------------------------------------------------------------------------
// Render overlay (OpenGL path)
// ---------------------------------------------------------------------------
bool climatology_pi::RenderGLOverlay(wxGLContext* pcontext, PlugIn_ViewPort* vp)
{
    if (!m_pClimatologyDialog || !g_pOverlayFactory)
        return false;

	ClimatologyRenderParams p;
		m_pClimatologyDialog->BuildRenderParams(vp, nullptr, p);


    g_pOverlayFactory->Render(p);
    return true;
}

// ---------------------------------------------------------------------------
// Cursor lat/lon → dialog
// ---------------------------------------------------------------------------
void climatology_pi::SetCursorLatLon(double lat, double lon)
{
    if (m_pClimatologyDialog)
//        m_pClimatologyDialog->SetCursorLatLon(lat, lon);
	;
}


// ---------------------------------------------------------------------------
// Plugin messaging — GRIB timeline sync
// ---------------------------------------------------------------------------
void climatology_pi::SetPluginMessage(wxString& message_id,
                                      wxString& message_body)
{
    if (message_id == "GRIB_TIMELINE")
    {
        if (!m_pClimatologyDialog)
            return;

       // Modern jsoncpp reader
        Json::CharReaderBuilder builder;
        std::unique_ptr<Json::CharReader> reader(builder.newCharReader());
        Json::Value root;
        std::string errs;
		
        std::string msg = message_body.ToStdString();
        if (!reader->parse(msg.c_str(), msg.c_str() + msg.size(), &root, &errs))
            return;

        int day   = root.get("Day",   -1).asInt();
        int month = root.get("Month", -1).asInt();
        int year  = root.get("Year",  -1).asInt();

        wxDateTime t;
        if (day >= 1 && month >= 0 && year >= 0)
            t.Set(day, (wxDateTime::Month)month, year);
        else
            t = wxDateTime(); // invalid

//  ClimatologyRenderParams p = m_pClimatologyDialog->GetRenderParams();
	ClimatologyRenderParams p;
	
	p.cycloneParams.currentMonth = t.GetMonth();
	p.cycloneParams.centerDay = t.IsValid() ? t.GetDayOfYear() : 0;

    m_pClimatologyDialog->ApplyRenderParams(p);
        return;
    }
}

// ---------------------------------------------------------------------------
// Notify OpenCPN that climatology is active
// ---------------------------------------------------------------------------
void climatology_pi::SendClimatology(bool valid)
{
    Json::Value v;
    v["ClimatologyVersionMajor"] = GetPlugInVersionMajor();
    v["ClimatologyVersionMinor"] = GetPlugInVersionMinor();

    Json::FastWriter writer;
    SendPluginMessage("CLIMATOLOGY", writer.write(v));
}

// ---------------------------------------------------------------------------
// Dialog geometry persistence
// ---------------------------------------------------------------------------
bool climatology_pi::LoadConfig(void)
{
    wxFileConfig* pConf = m_pconfig;
    if (!pConf)
        return false;

    pConf->SetPath("/Settings/Climatology");

    m_climatology_dialog_sx = pConf->Read("DialogSizeX", 300L);
    m_climatology_dialog_sy = pConf->Read("DialogSizeY", 540L);
    m_climatology_dialog_x  = pConf->Read("DialogPosX", 20L);
    m_climatology_dialog_y  = pConf->Read("DialogPosY", 170L);

    return true;
}

bool climatology_pi::SaveConfig(void)
{
    wxFileConfig* pConf = m_pconfig;
    if (!pConf)
        return false;

    pConf->SetPath("/Settings/Climatology");

    pConf->Write("DialogSizeX", m_climatology_dialog_sx);
    pConf->Write("DialogSizeY", m_climatology_dialog_sy);
    pConf->Write("DialogPosX",  m_climatology_dialog_x);
    pConf->Write("DialogPosY",  m_climatology_dialog_y);

    return true;
}

// ---------------------------------------------------------------------------
// Cleanup
// ---------------------------------------------------------------------------
void climatology_pi::FreeData()
{
    delete g_pOverlayFactory;
    g_pOverlayFactory = nullptr;

    if (m_pClimatologyDialog)
    {
//        m_pClimatologyDialog->Save();
        delete m_pClimatologyDialog;
        m_pClimatologyDialog = nullptr;
    }

    if (m_pConfigDialog)
    {
        delete m_pConfigDialog;
        m_pConfigDialog = nullptr;
    }
}


// ---------------------------------------------------------------------------
// Plugin metadata exports
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

// ---------------------------------------------------------------------------
// Plugin factory exports
// ---------------------------------------------------------------------------
extern "C" DECL_EXP opencpn_plugin* create_pi(void* ppimgr)
{
    return new climatology_pi(ppimgr);
}

extern "C" DECL_EXP void destroy_pi(opencpn_plugin* p)
{
    delete p;
}

//OLD
void climatology_pi::SetColorScheme(PI_ColorScheme cs)
{
    // No-op: dialogs auto-dime via DimeWindow()
}
// Necessary for API120
// void climatology_pi::UpdateFollowState(int mode, bool enabled)
// {
//     // No-op stub for API compatibility
// }

// Necessary for API120
// void climatology_pi::OnTideCurrentClick(TCClickInfo info)
// {
//    // No-op stub for API compatibility
//}


