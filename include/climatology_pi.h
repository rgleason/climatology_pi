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
#ifndef _CLIMATOLOGY_PI_PLUGIN_H_
#define _CLIMATOLOGY_PI_PLUGIN_H_

#include <wx/wx.h>
#include <wx/timer.h>
#include <wx/glcanvas.h>
#include <GL/glew.h>
#include <wx/notifmsg.h>

#include "version.h"
#include "ocpn_plugin.h"
#include "json/json.h"
#include "defs.h"

#include "ClimatologyDialog.h"
#include "ClimatologyControlDialog.h"
#include "ClimatologyOverlayFactory.h"
#include "StandardDisplayParams.h"
#include "DownloadManager.hpp"
#include "DownloadFileEntry.hpp"

#ifdef __OCPN__ANDROID__
extern QString qtStyleSheet;
#endif

#define ID_CYCLONE_READY    9001
#define ID_CYCLONE_PROGRESS 9002

#ifndef __OCPN__ANDROID__
#define GetDateCtrlValue GetValue
#endif

// Forward declarations
class DownloadManager;
class ClimatologyOverlayFactory;
class ClimatologyDialog;
class ClimatologyControlDialog;
class IsoBarMap;
class Cyclone;
struct StandardDisplayParams;

// Global overlay factory pointer
extern ClimatologyOverlayFactory* g_pOverlayFactory;

// Data directory helper
wxString ClimatologyDataDirectory();

//----------------------------------------------------------------------------
//    The PlugIn Class Definition
//---------------------------------------------------------------------------

#define CLIMATOLOGY_TOOL_POSITION    -1          
// Request default positioning of toolbar tool

class climatology_pi : public opencpn_plugin_120
{
public:
    climatology_pi(void* ppimgr);
    ~climatology_pi(void);

    // Required PlugIn Methods
    int Init(void);
    bool DeInit(void);

    int GetAPIVersionMajor();
    int GetAPIVersionMinor();
    int GetPlugInVersionMajor();
    int GetPlugInVersionMinor();
    int GetPlugInVersionPatch();
    int GetPlugInVersionPost();

    wxBitmap* GetPlugInBitmap();
    wxString GetCommonName();
    wxString GetShortDescription();
    wxString GetLongDescription();

    wxBitmap m_panelBitmap;

    void CreateOverlayFactory();

    // Override PlugIn Methods
    void OnToolbarToolCallback(int id);
    bool RenderGLOverlay(wxGLContext* pcontext, PlugIn_ViewPort* vp);

    void OnStartupTimer(wxTimerEvent& event);
    void OnDownloadComplete(wxCommandEvent& event);

    void ShowTransientMessage(const wxString& message,
                              const wxString& title,
                              int timeout_ms);

    void SetPluginMessage(wxString& message_id, wxString& message_body);
    void SendClimatology(bool valid);
    void SendTimelineMessage(wxDateTime time);

    // Dialog geometry setters
    void SetClimatologyDialogX(int x) { m_climatology_dialog_x = x; }
    void SetClimatologyDialogY(int y) { m_climatology_dialog_y = y; }
    void SetClimatologyDialogSizeX(int x) { m_climatology_dialog_sx = x; }
    void SetClimatologyDialogSizeY(int y) { m_climatology_dialog_sy = y; }

    void SetColorScheme(PI_ColorScheme cs);

private:
    void FreeData();
    void SetCursorLatLon(double lat, double lon);
    void SetDefaults(void);
    int GetToolbarToolCount(void);
	std::unique_ptr<DownloadManager> m_downloadManager;

    void OnClimatologyDialogClose();

    void SetStandardDisplayParams(const StandardDisplayParams& p);
    const StandardDisplayParams& GetStandardDisplayParams() const;
 //   void SendClimatology(const StandardDisplayParams& p);

    ClimatologyControlDialog* m_pControlDialog = nullptr;
    ClimatologyDialog*        m_pClimatologyDialog = nullptr;

    StandardDisplayParams m_params;

    wxTimer m_startupTimer;

    bool m_bCompletedLoading = false;

    bool LoadConfig(void);
    bool SaveConfig(void);

    wxFileConfig* m_pconfig = nullptr;
    wxWindow*     m_parent_window = nullptr;

    void BootstrapDataDirectory();
    wxString m_plugin_dir;

    int m_display_width = 0;
    int m_display_height = 0;
    int m_leftclick_tool_id = 0;

    int m_climatology_dialog_x = 0;
    int m_climatology_dialog_y = 0;
    int m_climatology_dialog_sx = 0;
    int m_climatology_dialog_sy = 0;
};

#endif // _CLIMATOLOGY_PI_PLUGIN_H_
