/******************************************************************************
 *
 * Project:  OpenCPN
 * Purpose:  Climatology Plugin
 * Author:   Sean D'Epagnier
 *
 ***************************************************************************
 *   Copyright (C) 2015 by Sean D'Epagnier                                 *
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
 */
/******************************************************************************
 * Climatology Plugin — Main Plugin Class (Header)
 * Modern unified‑grid architecture
 *
 * This plugin provides:
 *   - A quick‑access dialog (ClimatologyDialog)
 *   - A full configuration dialog (ClimatologyConfigDialog)
 *   - Unified NOAA‑grid rendering via ClimatologyOverlayFactory
 *   - GRIB timeline synchronization
 *   - Background data download and manifest management
 *
 * All rendering is driven by:
 *   StandardDisplayParams
 *   CycloneParams
 *   CycloneFilterParams
 *   WindAtlasParams
 *   ClimatologyRenderParams
 *
 ******************************************************************************/

#ifndef _CLIMATOLOGY_PI_PLUGIN_H_
#define _CLIMATOLOGY_PI_PLUGIN_H_

#pragma message("Header: " __FILE__)


// wxWidgets must be first
#include <wx/wxprec.h>
#ifndef WX_PRECOMP
#   include <wx/wx.h>
#endif

// now wx headers are safe
#include <wx/timer.h>
#include <wx/glcanvas.h>
#include <wx/notifmsg.h>
#include <memory>
//include <wx/jsonreader.h>
//#include <wx/jsonwriter.h>
#include <nlohmann/json.hpp>
using json = nlohmann::json;


#include "ocpn_plugin.h"

#include "StandardDisplayParams.h"      // Persistent + runtime settings
#include "CycloneParams.h"
#include "CycloneFilterParams.h"
#include "WindAtlasParams.h"
#include "DownloadManager.hpp"
#include "DownloadFileEntry.hpp"

class ClimatologyDialog;
class ClimatologyConfigDialog;
class ClimatologyOverlayFactory;

// Global overlay factory pointer
extern ClimatologyOverlayFactory* g_pOverlayFactory;

// Data directory helper
wxString ClimatologyDataDirectory();

/**
 * @brief Main plugin class for the Climatology plugin.
 *
 * Responsibilities:
 *   - Manage plugin lifecycle (Init, DeInit)
 *   - Manage toolbar icon and callbacks
 *   - Manage dialogs (quick access + config)
 *   - Manage unified-grid overlay rendering
 *   - Manage background data downloads
 *   - Handle GRIB timeline synchronization
 *   - Provide cursor lat/lon to dialogs
 *   - Persist dialog geometry
 */
 

class climatology_pi : public opencpn_plugin_120
{
public:
    climatology_pi(void* ppimgr);
    virtual ~climatology_pi();

    //virtual void UpdateFollowState(int mode, bool enabled) override;  //api121
    //virtual void OnTideCurrentClick(TCClickInfo info) override;     //api121

    // Required plugin API
    int  Init(void) override;
    bool DeInit(void) override;

    int GetAPIVersionMajor() override;
    int GetAPIVersionMinor() override;
    int GetPlugInVersionMajor() override;
    int GetPlugInVersionMinor() override;
    int GetPlugInVersionPatch() override;
    int GetPlugInVersionPost() override;

    wxBitmap* GetPlugInBitmap() override;
    wxString  GetCommonName() override;
    wxString  GetShortDescription() override;
    wxString  GetLongDescription() override;
	
    // Toolbar + dialogs
    void OnToolbarToolCallback(int id) override;

    // Overlay rendering
    bool RenderGLOverlay(wxGLContext* pcontext, PlugIn_ViewPort* vp) override;
    bool RenderOverlay(wxDC& dc, PlugIn_ViewPort* vp) override;

    // Messaging
    void SetPluginMessage(wxString& message_id, wxString& message_body) override;
    void SendClimatology(bool valid);
    void SendTimelineMessage(wxDateTime time);

    // Cursor
    void SetCursorLatLon(double lat, double lon) override;

    // Dialog geometry persistence
    bool LoadConfig(void);
    bool SaveConfig(void);

    void SetColorScheme(PI_ColorScheme cs) override;

	private:
    // Internal helpers
    void CreateOverlayFactory();
    void FreeData();
    void BootstrapDataDirectory();

    // Download manager
    void OnStartupTimer(wxTimerEvent& event);
    void OnDownloadComplete(wxCommandEvent& event);

    // Persistent + runtime parameters
    StandardDisplayParams m_params;

    // Dialogs
    ClimatologyDialog*        m_pClimatologyDialog = nullptr;  // Quick access
    ClimatologyConfigDialog*  m_pConfigDialog      = nullptr;  // Modal 4‑tab config

    // Download manager
    std::unique_ptr<DownloadManager> m_downloadManager;

    // Plugin config + UI
    wxFileConfig* m_pconfig = nullptr;
    wxWindow*     m_parent_window = nullptr;

    wxString m_plugin_dir;
    wxBitmap m_panelBitmap;

    // Toolbar
    int m_leftclick_tool_id = 0;

    // Dialog geometry
    int m_climatology_dialog_x  = 0;
    int m_climatology_dialog_y  = 0;
    int m_climatology_dialog_sx = 0;
    int m_climatology_dialog_sy = 0;

    // Startup
    wxTimer m_startupTimer;
    bool    m_bCompletedLoading = false;
}	

#endif // _CLIMATOLOGY_PI_PLUGIN_H_
