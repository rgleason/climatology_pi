/******************************************************************************
 *
 * Project:  OpenCPN
 * Purpose:  Climatology Plugin - Configuration Dialog
 * Author:   Sean D'Epagnier, modernized by refactor
 * ClimatologyConfigDialog.cpp
 * Fully corrected version with unified-grid Blend Mode support and
 * StandardDisplayParams integration.
 ***************************************************************************
 *   Copyright (C) 2014 by Sean D'Epagnier                                 *
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
 ******************************************************************************
 * ClimatologyConfigDialog.cpp
 * Fully corrected version with unified-grid Blend Mode support and
 * StandardDisplayParams integration.
 ******************************************************************************/

// Legacy cyclone bitmask handlers (removed in your refactor)
// stateMask
// basinMask
// categoryMask
// CYCLONE_TROPICAL, etc.


// Otherwise MSVC will misorder wx includes (wx/wxprec.h)
// and the plugin API symbols will not resolve.
#include "ocpn_plugin_guarded.h"

#include <wx/wxprec.h>
#ifndef WX_PRECOMP
    #include <wx/wx.h>
#endif

// wxWidgets
#include <wx/notebook.h>
#include <wx/clrpicker.h>
#include <wx/datectrl.h>
#include <wx/dateevt.h>
#include <wx/dir.h>
#include <wx/spinctrl.h>
#include <wx/checkbox.h>
#include <wx/choice.h>
#include <wx/slider.h>
#include <wx/statline.h>
#include <wx/clrpicker.h>
#include <wx/timer.h>
#include <wx/html/htmlwin.h>
#include <wx/hash.h>
#include <wx/fileconf.h>
#include "version.h"

// REMOVE — not needed, causes circular dependencies
// #include "ocpn_plugin.h"

// REMOVE — config dialog does not use OpenGL
// #include <GL/glew.h>
// #include "gldefs.h"

// Plugin headers
#include "ClimatologyConfigDialog.h"
#include "ClimatologyDialog.h"
#include "ClimatologyOverlayFactory.h"
#include "climatology_pi.h"
#include "CycloneUtils.h"
#include "ClimatologyEnums.h"
#include "ManifestLoader.hpp"
#include "DownloadManager.hpp"

// ---------------------------------------------------------------------------
// Event table
// ---------------------------------------------------------------------------

wxBEGIN_EVENT_TABLE(ClimatologyConfigDialog, wxDialog)
    EVT_NOTEBOOK_PAGE_CHANGED(wxID_ANY, ClimatologyConfigDialog::OnPageChanged)
    EVT_TIMER(wxID_ANY, ClimatologyConfigDialog::OnRefreshTimer)
wxEND_EVENT_TABLE()

// ---------------------------------------------------------------------------
// Static helpers: unit labels per overlay (UI only)
// ---------------------------------------------------------------------------

static const wxString units_wind[]      = {_("Knots"), _("m/s"), _("mph"), _("km/h"), wxEmptyString};
static const wxString units_pressure[]  = {_("mbar"), _("mmHg"), wxEmptyString};
static const wxString units_precip[]    = {_("mm/day"), _("in/day"), _("mm/month"), _("m/month"),
                                          _("in/month"), _("ft/month"), _("m/year"),
                                          _("in/year"), _("ft/year"), wxEmptyString};
static const wxString units_temp[]      = {_("Celsius"), _("Fahrenheit"), wxEmptyString};
static const wxString units_percent[]   = {_("Percent"), wxEmptyString};
static const wxString units_depth[]     = {_("Meters"), _("Feet"), wxEmptyString};

static const wxString* GetUnitListForOverlay(int overlayType)
{
    switch (overlayType) {
    case OVERLAY_WIND:
    case OVERLAY_CURRENT:
        return units_wind;
    case OVERLAY_PRESSURE:
        return units_pressure;
    case OVERLAY_PRECIP:
        return units_precip;
    case OVERLAY_SEA_TEMP:
    case OVERLAY_AIR_TEMP:
        return units_temp;
    case OVERLAY_CLOUD:
    case OVERLAY_RH:
        return units_percent;
    case OVERLAY_SEA_DEPTH:
        return units_depth;
    default:
        return units_wind;
    }
}

// ---------------------------------------------------------------------------
// Constructors
// ---------------------------------------------------------------------------
// Factory-based constructor
// Role A: 4-Tab Config Dialog
// Role B: Factory Based Constructor - Lightweight entry point
// StandardDisplayParams and  CycloneParams live inside COF. 
// COF owns m_displayParams, m_cycloneParams, m_cycloneFilterParams

ClimatologyConfigDialog::ClimatologyConfigDialog(ClimatologyDialog* parent)
    : wxDialog(parent, wxID_ANY, _("Climatology Configuration"),
               wxDefaultPosition, wxDefaultSize,
               wxDEFAULT_DIALOG_STYLE | wxSTAY_ON_TOP | wxRESIZE_BORDER),
      m_parent(parent),
      m_refreshTimer(this),
      m_displayParams(parent->GetDisplayParams())
{
    DimeWindow(this);
    
    // Create notebook with 4 tabs
    m_notebook = new wxNotebook(this, wxID_ANY);
    
    // Build the 4 tabs
    m_panelStandard  = BuildStandardTab(m_notebook);
    m_panelWindAtlas = BuildWindAtlasTab(m_notebook);
    m_panelCyclones  = BuildCyclonesTab(m_notebook);
    m_panelInfo      = BuildInfoTab(m_notebook);
    
    m_notebook->AddPage(m_panelStandard, _("Standard"));
    m_notebook->AddPage(m_panelWindAtlas, _("Wind Atlas"));
    m_notebook->AddPage(m_panelCyclones, _("Cyclones"));
    m_notebook->AddPage(m_panelInfo, _("Information"));
    
    // Button sizer
    wxStdDialogButtonSizer* buttonSizer = new wxStdDialogButtonSizer();
    wxButton* okButton = new wxButton(this, wxID_OK);
    wxButton* cancelButton = new wxButton(this, wxID_CANCEL);
    buttonSizer->AddButton(okButton);
    buttonSizer->AddButton(cancelButton);
    buttonSizer->Realize();
    
    // Main sizer
    wxBoxSizer* topSizer = new wxBoxSizer(wxVERTICAL);
    topSizer->Add(m_notebook, 1, wxEXPAND | wxALL, 5);
    topSizer->Add(buttonSizer, 0, wxEXPAND | wxALL, 5);
    
    SetSizer(topSizer);
    
    // Load settings and sync UI
    LoadSettings();
    SyncStandardTabFromModel();
    SyncWindAtlasTabFromModel();
    SyncCyclonesTabFromModel();
    
    Fit();
    SetMinSize(wxSize(500, 600));
    
    // Event handlers
    okButton->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
        Save();
        EndModal(wxID_OK);
    });
    
    cancelButton->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
        EndModal(wxID_CANCEL);
    });
}
	
	// ---------------------------------------------------------------------------
	// Destructor
	// ---------------------------------------------------------------------------

	ClimatologyConfigDialog::~ClimatologyConfigDialog()
	{
	}

	// ---------------------------------------------------------------------------
	// Theme helper (wraps DimeWindow)
	// ---------------------------------------------------------------------------

wxPanel* ClimatologyConfigDialog::BuildStandardTab(wxWindow* parent)
{
    wxPanel* panel = new wxPanel(parent);
    wxBoxSizer* sizer = new wxBoxSizer(wxVERTICAL);
    
    // Overlay Type
    wxBoxSizer* row = new wxBoxSizer(wxHORIZONTAL);
    row->Add(new wxStaticText(panel, wxID_ANY, _("Overlay Type:")), 0, wxALIGN_CENTER_VERTICAL | wxALL, 5);
    m_cOverlayType = new wxChoice(panel, wxID_ANY);
    m_cOverlayType->Append(_("Wind"));
    m_cOverlayType->Append(_("Current"));
    m_cOverlayType->Append(_("Pressure"));
    m_cOverlayType->Append(_("Sea Temperature"));
    m_cOverlayType->Append(_("Air Temperature"));
    m_cOverlayType->Append(_("Cloud Cover"));
    m_cOverlayType->Append(_("Precipitation"));
    m_cOverlayType->Append(_("Relative Humidity"));
    m_cOverlayType->Append(_("Lightning"));
    m_cOverlayType->Append(_("Sea Depth"));
    row->Add(m_cOverlayType, 1, wxEXPAND | wxALL, 5);
    sizer->Add(row, 0, wxEXPAND);
    
    // Units
    row = new wxBoxSizer(wxHORIZONTAL);
    row->Add(new wxStaticText(panel, wxID_ANY, _("Units:")), 0, wxALIGN_CENTER_VERTICAL | wxALL, 5);
    m_cUnits = new wxChoice(panel, wxID_ANY);
    row->Add(m_cUnits, 1, wxEXPAND | wxALL, 5);
    sizer->Add(row, 0, wxEXPAND);
    
    // Blend Mode
    row = new wxBoxSizer(wxHORIZONTAL);
    row->Add(new wxStaticText(panel, wxID_ANY, _("Blend Mode:")), 0, wxALIGN_CENTER_VERTICAL | wxALL, 5);
    m_cBlendMode = new wxChoice(panel, wxID_ANY);
    m_cBlendMode->Append(_("Single Scaled"));
    m_cBlendMode->Append(_("Multi Blended"));
    row->Add(m_cBlendMode, 1, wxEXPAND | wxALL, 5);
    sizer->Add(row, 0, wxEXPAND);
    
    // Show Overlay checkbox
    m_cbShowOverlay = new wxCheckBox(panel, wxID_ANY, _("Show Overlay"));
    sizer->Add(m_cbShowOverlay, 0, wxALL, 5);
    
    // Transparency slider
    row = new wxBoxSizer(wxHORIZONTAL);
    row->Add(new wxStaticText(panel, wxID_ANY, _("Transparency:")), 0, wxALIGN_CENTER_VERTICAL | wxALL, 5);
    m_sTransparency = new wxSlider(panel, wxID_ANY, 100, 0, 100);
    row->Add(m_sTransparency, 1, wxEXPAND | wxALL, 5);
    sizer->Add(row, 0, wxEXPAND);
    
    // IsoBars
    m_cbShowIsoBars = new wxCheckBox(panel, wxID_ANY, _("Show Iso Bars"));
    sizer->Add(m_cbShowIsoBars, 0, wxALL, 5);
    
    row = new wxBoxSizer(wxHORIZONTAL);
    row->Add(new wxStaticText(panel, wxID_ANY, _("Iso Spacing:")), 0, wxALIGN_CENTER_VERTICAL | wxALL, 5);
    m_sIsoSpacing = new wxSpinCtrl(panel, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize, wxSP_ARROW_KEYS, 0, 100, 20);
    row->Add(m_sIsoSpacing, 1, wxEXPAND | wxALL, 5);
    sizer->Add(row, 0, wxEXPAND);
    
    row = new wxBoxSizer(wxHORIZONTAL);
    row->Add(new wxStaticText(panel, wxID_ANY, _("Iso Step:")), 0, wxALIGN_CENTER_VERTICAL | wxALL, 5);
    m_cIsoStep = new wxChoice(panel, wxID_ANY);
    for (int i = 0; i < 10; i++)
        m_cIsoStep->Append(wxString::Format("%d", i));
    row->Add(m_cIsoStep, 1, wxEXPAND | wxALL, 5);
    sizer->Add(row, 0, wxEXPAND);
    
    // Numbers
    m_cbShowNumbers = new wxCheckBox(panel, wxID_ANY, _("Show Numbers"));
    sizer->Add(m_cbShowNumbers, 0, wxALL, 5);
    
    row = new wxBoxSizer(wxHORIZONTAL);
    row->Add(new wxStaticText(panel, wxID_ANY, _("Number Size:")), 0, wxALIGN_CENTER_VERTICAL | wxALL, 5);
    m_sNumberSize = new wxSpinCtrl(panel, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize, wxSP_ARROW_KEYS, 8, 48, 12);
    row->Add(m_sNumberSize, 1, wxEXPAND | wxALL, 5);
    sizer->Add(row, 0, wxEXPAND);
    
    // Arrows
    m_cbShowArrows = new wxCheckBox(panel, wxID_ANY, _("Show Arrows"));
    sizer->Add(m_cbShowArrows, 0, wxALL, 5);
    
    row = new wxBoxSizer(wxHORIZONTAL);
    row->Add(new wxStaticText(panel, wxID_ANY, _("Arrow Width:")), 0, wxALIGN_CENTER_VERTICAL | wxALL, 5);
    m_sArrowWidth = new wxSpinCtrl(panel, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize, wxSP_ARROW_KEYS, 1, 10, 2);
    row->Add(m_sArrowWidth, 1, wxEXPAND | wxALL, 5);
    sizer->Add(row, 0, wxEXPAND);
    
    row = new wxBoxSizer(wxHORIZONTAL);
    row->Add(new wxStaticText(panel, wxID_ANY, _("Arrow Spacing:")), 0, wxALIGN_CENTER_VERTICAL | wxALL, 5);
    m_sArrowSpacing = new wxSpinCtrl(panel, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize, wxSP_ARROW_KEYS, 10, 100, 20);
    row->Add(m_sArrowSpacing, 1, wxEXPAND | wxALL, 5);
    sizer->Add(row, 0, wxEXPAND);
    
    row = new wxBoxSizer(wxHORIZONTAL);
    row->Add(new wxStaticText(panel, wxID_ANY, _("Arrow Size:")), 0, wxALIGN_CENTER_VERTICAL | wxALL, 5);
    m_sArrowSize = new wxSpinCtrl(panel, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize, wxSP_ARROW_KEYS, 5, 50, 12);
    row->Add(m_sArrowSize, 1, wxEXPAND | wxALL, 5);
    sizer->Add(row, 0, wxEXPAND);
    
    row = new wxBoxSizer(wxHORIZONTAL);
    row->Add(new wxStaticText(panel, wxID_ANY, _("Arrow Mode:")), 0, wxALIGN_CENTER_VERTICAL | wxALL, 5);
    m_cArrowMode = new wxChoice(panel, wxID_ANY);
    m_cArrowMode->Append(_("Default"));
    m_cArrowMode->Append(_("Barbed"));
    row->Add(m_cArrowMode, 1, wxEXPAND | wxALL, 5);
    sizer->Add(row, 0, wxEXPAND);
    
    // Color picker
    row = new wxBoxSizer(wxHORIZONTAL);
    row->Add(new wxStaticText(panel, wxID_ANY, _("Overlay Color:")), 0, wxALIGN_CENTER_VERTICAL | wxALL, 5);
    m_cpOverlayColor = new wxColourPickerCtrl(panel, wxID_ANY);
    row->Add(m_cpOverlayColor, 0, wxALL, 5);
    sizer->Add(row, 0, wxEXPAND);
    
    panel->SetSizer(sizer);
    return panel;
}

wxPanel* ClimatologyConfigDialog::BuildWindAtlasTab(wxWindow* parent)
{
    wxPanel* panel = new wxPanel(parent);
    wxBoxSizer* sizer = new wxBoxSizer(wxVERTICAL);
    
    m_cbWindAtlasEnable = new wxCheckBox(panel, wxID_ANY, _("Enable Wind Atlas"));
    sizer->Add(m_cbWindAtlasEnable, 0, wxALL, 5);
    
    wxBoxSizer* row = new wxBoxSizer(wxHORIZONTAL);
    row->Add(new wxStaticText(panel, wxID_ANY, _("Size:")), 0, wxALIGN_CENTER_VERTICAL | wxALL, 5);
    m_sWindAtlasSize = new wxSpinCtrl(panel, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize, wxSP_ARROW_KEYS, 50, 300, 120);
    row->Add(m_sWindAtlasSize, 1, wxEXPAND | wxALL, 5);
    sizer->Add(row, 0, wxEXPAND);
    
    row = new wxBoxSizer(wxHORIZONTAL);
    row->Add(new wxStaticText(panel, wxID_ANY, _("Spacing:")), 0, wxALIGN_CENTER_VERTICAL | wxALL, 5);
    m_sWindAtlasSpacing = new wxSpinCtrl(panel, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize, wxSP_ARROW_KEYS, 50, 200, 80);
    row->Add(m_sWindAtlasSpacing, 1, wxEXPAND | wxALL, 5);
    sizer->Add(row, 0, wxEXPAND);
    
    row = new wxBoxSizer(wxHORIZONTAL);
    row->Add(new wxStaticText(panel, wxID_ANY, _("Opacity:")), 0, wxALIGN_CENTER_VERTICAL | wxALL, 5);
    m_sWindAtlasOpacity = new wxSlider(panel, wxID_ANY, 100, 0, 100);
    row->Add(m_sWindAtlasOpacity, 1, wxEXPAND | wxALL, 5);
    sizer->Add(row, 0, wxEXPAND);
    
    panel->SetSizer(sizer);
    return panel;
}

wxPanel* ClimatologyConfigDialog::BuildCyclonesTab(wxWindow* parent)
{
    wxPanel* panel = new wxPanel(parent);
    wxBoxSizer* sizer = new wxBoxSizer(wxVERTICAL);
    
    m_cbCyclonesEnable = new wxCheckBox(panel, wxID_ANY, _("Enable Cyclones"));
    sizer->Add(m_cbCyclonesEnable, 0, wxALL, 5);
    
    wxBoxSizer* row = new wxBoxSizer(wxHORIZONTAL);
    row->Add(new wxStaticText(panel, wxID_ANY, _("Center Day:")), 0, wxALIGN_CENTER_VERTICAL | wxALL, 5);
    m_sCycloneCenterDay = new wxSpinCtrl(panel, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize, wxSP_ARROW_KEYS, 1, 365, 180);
    row->Add(m_sCycloneCenterDay, 1, wxEXPAND | wxALL, 5);
    sizer->Add(row, 0, wxEXPAND);
    
    row = new wxBoxSizer(wxHORIZONTAL);
    row->Add(new wxStaticText(panel, wxID_ANY, _("Day Span:")), 0, wxALIGN_CENTER_VERTICAL | wxALL, 5);
    m_sCycloneDaySpan = new wxSpinCtrl(panel, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize, wxSP_ARROW_KEYS, 1, 180, 30);
    row->Add(m_sCycloneDaySpan, 1, wxEXPAND | wxALL, 5);
    sizer->Add(row, 0, wxEXPAND);
    
    row = new wxBoxSizer(wxHORIZONTAL);
    row->Add(new wxStaticText(panel, wxID_ANY, _("Min Wind (kt):")), 0, wxALIGN_CENTER_VERTICAL | wxALL, 5);
    m_sMinWind = new wxSpinCtrl(panel, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize, wxSP_ARROW_KEYS, 0, 200, 35);
    row->Add(m_sMinWind, 1, wxEXPAND | wxALL, 5);
    sizer->Add(row, 0, wxEXPAND);
    
    row = new wxBoxSizer(wxHORIZONTAL);
    row->Add(new wxStaticText(panel, wxID_ANY, _("Max Pressure (mb):")), 0, wxALIGN_CENTER_VERTICAL | wxALL, 5);
    m_sMaxPressure = new wxSpinCtrl(panel, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize, wxSP_ARROW_KEYS, 800, 1100, 1080);
    row->Add(m_sMaxPressure, 1, wxEXPAND | wxALL, 5);
    sizer->Add(row, 0, wxEXPAND);
    
    m_cbAllTimesCyclones = new wxCheckBox(panel, wxID_ANY, _("All Times (ignore date range)"));
    sizer->Add(m_cbAllTimesCyclones, 0, wxALL, 5);
    
    sizer->Add(new wxStaticText(panel, wxID_ANY, _("Storm Types:")), 0, wxALL, 5);
    m_cbCycloneTropical = new wxCheckBox(panel, wxID_ANY, _("Tropical"));
    m_cbCycloneSubTropical = new wxCheckBox(panel, wxID_ANY, _("Subtropical"));
    m_cbCycloneExtraTropical = new wxCheckBox(panel, wxID_ANY, _("Extratropical"));
    m_cbCycloneWave = new wxCheckBox(panel, wxID_ANY, _("Wave"));
    m_cbCycloneRemanent = new wxCheckBox(panel, wxID_ANY, _("Remanent"));
    m_cbCycloneUnknown = new wxCheckBox(panel, wxID_ANY, _("Unknown"));
    sizer->Add(m_cbCycloneTropical, 0, wxALL, 5);
    sizer->Add(m_cbCycloneSubTropical, 0, wxALL, 5);
    sizer->Add(m_cbCycloneExtraTropical, 0, wxALL, 5);
    sizer->Add(m_cbCycloneWave, 0, wxALL, 5);
    sizer->Add(m_cbCycloneRemanent, 0, wxALL, 5);
    sizer->Add(m_cbCycloneUnknown, 0, wxALL, 5);
    
    sizer->Add(new wxStaticText(panel, wxID_ANY, _("ENSO:")), 0, wxALL, 5);
    m_cbElNino = new wxCheckBox(panel, wxID_ANY, _("El Niño"));
    m_cbLaNina = new wxCheckBox(panel, wxID_ANY, _("La Niña"));
    m_cbNeutral = new wxCheckBox(panel, wxID_ANY, _("Neutral"));
    m_cbUnknownENSO = new wxCheckBox(panel, wxID_ANY, _("Unknown"));
    sizer->Add(m_cbElNino, 0, wxALL, 5);
    sizer->Add(m_cbLaNina, 0, wxALL, 5);
    sizer->Add(m_cbNeutral, 0, wxALL, 5);
    sizer->Add(m_cbUnknownENSO, 0, wxALL, 5);
    
    sizer->Add(new wxStaticText(panel, wxID_ANY, _("Basins:")), 0, wxALL, 5);
    m_cbEPA = new wxCheckBox(panel, wxID_ANY, _("East Pacific"));
    m_cbWPA = new wxCheckBox(panel, wxID_ANY, _("West Pacific"));
    m_cbSPA = new wxCheckBox(panel, wxID_ANY, _("South Pacific"));
    m_cbATL = new wxCheckBox(panel, wxID_ANY, _("Atlantic"));
    m_cbNIO = new wxCheckBox(panel, wxID_ANY, _("North Indian"));
    m_cbSHE = new wxCheckBox(panel, wxID_ANY, _("South Hemisphere"));
    sizer->Add(m_cbEPA, 0, wxALL, 5);
    sizer->Add(m_cbWPA, 0, wxALL, 5);
    sizer->Add(m_cbSPA, 0, wxALL, 5);
    sizer->Add(m_cbATL, 0, wxALL, 5);
    sizer->Add(m_cbNIO, 0, wxALL, 5);
    sizer->Add(m_cbSHE, 0, wxALL, 5);
    
    panel->SetSizer(sizer);
    return panel;
}


// About Inof
wxPanel* ClimatologyConfigDialog::BuildInfoTab(wxWindow* parent)
{
    wxPanel* panel = new wxPanel(parent);
    wxBoxSizer* sizer = new wxBoxSizer(wxVERTICAL);
    
    m_htmlInformation = new wxHtmlWindow(panel, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxHW_SCROLLBAR_AUTO);
    m_htmlInformation->SetPage(
        _("<html><body>"
          "<h3>Climatology Plugin</h3>"
          "<p>Display climatological weather data overlays including wind, current, sea temperature, and cyclone tracks.</p>"
          "<p><b>Data Sources:</b> Various climatological databases</p>"
          "<p><b>Usage:</b> Select overlay types and configure display options in the tabs.</p>"
          "</body></html>")
    );
	
	sizer->Add(new wxStaticLine(panel), 0, wxEXPAND | wxALL, 5);
	sizer->Add(m_htmlInformation, 1, wxEXPAND | wxALL, 5);
	
	// --- Add Redownload All button ---
    wxButton* redownloadBtn = new wxButton(panel, wxID_ANY, _("Redownload All Data"));
    sizer->Add(redownloadBtn, 0, wxALL, 5);

    redownloadBtn->Bind(wxEVT_BUTTON,
                        &ClimatologyConfigDialog::OnRedownloadAll,
                        this);
	
	// --- Add Download Missing Files button --- 	
	wxButton* missingBtn = new wxButton(panel, wxID_ANY, _("Download Missing Files Only"));
	sizer->Add(missingBtn, 0, wxALL, 5);

	missingBtn->Bind(wxEVT_BUTTON,
					 &ClimatologyConfigDialog::OnDownloadMissing,
					 this);
							

    // --- Add Check Integrity button ---
    wxButton* integrityBtn = new wxButton(panel, wxID_ANY, _("Check Data Integrity"));
    sizer->Add(integrityBtn, 0, wxALL, 5);

    integrityBtn->Bind(wxEVT_BUTTON,
                       &ClimatologyConfigDialog::OnCheckIntegrity,
                       this);
					   
					   
    
    panel->SetSizer(sizer);
    return panel;
}




	void ClimatologyConfigDialog::ApplyTheme()
	{
		DimeWindow(this);
	}

	// ---------------------------------------------------------------------------
	// Persistence: LoadSettings / SaveSettings
	// ---------------------------------------------------------------------------

	void ClimatologyConfigDialog::LoadSettings()
	{
		wxFileConfig* pConf = GetOCPNConfigObject();
		if (!pConf)
			return;

		pConf->SetPath("/Settings/Climatology");

		pConf->Read("lastOverlayType", &m_lastOverlayType, OVERLAY_WIND);

		int modeInt = (int)DisplayMode::SingleScaled;
		pConf->Read("BlendMode", &modeInt, modeInt);
		m_displayParams.mode = (DisplayMode)modeInt;

		// Wind Atlas
		pConf->SetPath("/PlugIns/Climatology/WindAtlas");
		bool enabled = false;
		pConf->Read("Enabled", &enabled);
		m_displayParams.windAtlas.enabled = enabled;
		m_displayParams.windAtlas.size    = (double)pConf->Read("Size1", 120L);
		m_displayParams.windAtlas.spacing = (double)pConf->Read("Spacing1", 80L);
		m_displayParams.alpha[OVERLAY_WIND_ATLAS] =
			(double)pConf->Read("Opacity", 205L) / 255.0;

		// Cyclones (persistent appearance)
		pConf->SetPath("/PlugIns/Climatology/Cyclones");
		bool cEnabled = false;
		pConf->Read("Enabled", &cEnabled);
		m_parent->GetCycloneParams().enabled = cEnabled;
		m_parent->GetCycloneParams().centerDay    = (int)pConf->Read("CenterDay", 180L);
		m_parent->GetCycloneParams().daySpan      = (int)pConf->Read("CycloneDaySpan", 30L);
		double minWind = 35.0;
		pConf->Read("MinWindSpeed", &minWind);
		m_parent->GetCycloneParams().minWind = minWind;
		// m_parent->GetCycloneParams().maxPressure  = (double)pConf->Read("MaxPressure", 1080L);
		bool allTimes = false;
		pConf->Read("AllTimesCyclones", &allTimes);
		m_parent->GetCycloneParams().allTimesCyclones = allTimes;

	}

	void ClimatologyConfigDialog::SaveSettings()
	{
		wxFileConfig* pConf = GetOCPNConfigObject();
		if (!pConf)
			return;

		pConf->SetPath("/Settings/Climatology");
		pConf->Write("lastOverlayType", m_lastOverlayType);
		pConf->Write("BlendMode", (int)m_displayParams.mode);

		pConf->SetPath("/PlugIns/Climatology/WindAtlas");
		pConf->Write("Enabled",  m_displayParams.windAtlas.enabled);
		pConf->Write("Size1",    (long)m_displayParams.windAtlas.size);
		pConf->Write("Spacing1", (long)m_displayParams.windAtlas.spacing);
		pConf->Write("Opacity",  (long)(m_displayParams.alpha[OVERLAY_WIND_ATLAS] * 255.0));

		pConf->SetPath("/PlugIns/Climatology/Cyclones");
		pConf->Write("Enabled",        m_parent->GetCycloneParams().enabled);
		pConf->Write("CenterDay",      (long)m_parent->GetCycloneParams().centerDay);
		pConf->Write("CycloneDaySpan", (long)m_parent->GetCycloneParams().daySpan);
		pConf->Write("MinWindSpeed",   (long)m_parent->GetCycloneParams().minWind);
		// pConf->Write("MaxPressure",    (long)m_parent->GetCycloneParams().maxPressure);
		pConf->Write("AllTimesCyclones", m_parent->GetCycloneParams().allTimesCyclones);
	}

	// ---------------------------------------------------------------------------
	// Save() - public entry point
	// ---------------------------------------------------------------------------

	void ClimatologyConfigDialog::Save()
	{
		SyncStandardTabToModel();
		SyncWindAtlasTabToModel();
		SyncCyclonesTabToModel();
		SaveSettings();
	}

	// ---------------------------------------------------------------------------
	// UI <-> model sync: Standard tab
	// ---------------------------------------------------------------------------

	void ClimatologyConfigDialog::SyncStandardTabFromModel()
	{
		m_cOverlayType->SetSelection(m_displayParams.overlayType);
		m_lastOverlayType = m_displayParams.overlayType;

		// Units list for current overlay
		m_cUnits->Clear();
		const wxString* units = GetUnitListForOverlay(m_displayParams.overlayType);
		for (int i = 0; !units[i].empty(); ++i)
			m_cUnits->Append(units[i]);
		m_cUnits->SetSelection(m_displayParams.units[m_displayParams.overlayType]);

		m_cBlendMode->SetSelection((int)m_displayParams.mode);

		int idx = m_displayParams.overlayType;
		m_cbShowOverlay->SetValue(m_displayParams.showOverlayType[idx]);
		m_sTransparency->SetValue((int)(m_displayParams.alpha[idx] * 100.0));

		m_cbShowIsoBars->SetValue(m_displayParams.showIsoBars[idx]);
		m_sIsoSpacing->SetValue(m_displayParams.isoSpacing[idx]);
		m_cIsoStep->SetSelection(m_displayParams.isoStep[idx]);

		m_cbShowNumbers->SetValue(m_displayParams.showNumbers[idx]);
		m_sNumberSize->SetValue((int)m_displayParams.numberSize[idx]);

		m_cbShowArrows->SetValue(m_displayParams.showArrows[idx]);
		m_sArrowWidth->SetValue(m_displayParams.arrowWidth[idx]);
		m_sArrowSpacing->SetValue(m_displayParams.arrowSpacing[idx]);
		m_sArrowSize->SetValue(m_displayParams.arrowSize[idx]);
		m_cArrowMode->SetSelection(m_displayParams.arrowMode[idx]);

		m_cpOverlayColor->SetColour(m_displayParams.color[idx]);
	}

	void ClimatologyConfigDialog::SyncStandardTabToModel()
	{
		int idx = m_cOverlayType->GetSelection();
		if (idx < 0 || idx >= NUM_OVERLAYS)
			return;

		m_displayParams.overlayType = idx;
		m_lastOverlayType = idx;

		m_displayParams.mode = (DisplayMode)m_cBlendMode->GetSelection();

		m_displayParams.units[idx] = m_cUnits->GetSelection();

		m_displayParams.showOverlayType[idx] = m_cbShowOverlay->GetValue();
		m_displayParams.alpha[idx] = m_sTransparency->GetValue() / 100.0;

		m_displayParams.showIsoBars[idx] = m_cbShowIsoBars->GetValue();
		m_displayParams.isoSpacing[idx]  = m_sIsoSpacing->GetValue();
		m_displayParams.isoStep[idx]     = m_cIsoStep->GetSelection();

		m_displayParams.showNumbers[idx] = m_cbShowNumbers->GetValue();
		m_displayParams.numberSize[idx]  = (double)m_sNumberSize->GetValue();
	}

	// ---------------------------------------------------------------------------
	// UI <-> model sync: WindAtlas Tab
	// ---------------------------------------------------------------------------


	void ClimatologyConfigDialog::SyncWindAtlasTabFromModel()
	{
		m_cbWindAtlasEnable->SetValue(m_displayParams.windAtlas.enabled);
		m_sWindAtlasSize->SetValue((int)m_displayParams.windAtlas.size);
		m_sWindAtlasSpacing->SetValue((int)m_displayParams.windAtlas.spacing);
		m_sWindAtlasOpacity->SetValue((int)(m_displayParams.alpha[OVERLAY_WIND_ATLAS] * 100.0));
	}


	void ClimatologyConfigDialog::SyncWindAtlasTabToModel()
	{
		m_displayParams.windAtlas.enabled = m_cbWindAtlasEnable->GetValue();
		m_displayParams.windAtlas.size    = (double)m_sWindAtlasSize->GetValue();
		m_displayParams.windAtlas.spacing = (double)m_sWindAtlasSpacing->GetValue();
		m_displayParams.alpha[OVERLAY_WIND_ATLAS] =
			m_sWindAtlasOpacity->GetValue() / 100.0;
	}

	// ---------------------------------------------------------------------------
	// UI <-> model sync: Cyclone tab
	// ---------------------------------------------------------------------------

	void ClimatologyConfigDialog::SyncCyclonesTabFromModel()
	{
		// Appearance (CycloneParams)
		m_cbCyclonesEnable->SetValue(m_parent->GetCycloneParams().enabled);
		m_sCycloneCenterDay->SetValue(m_parent->GetCycloneParams().centerDay);
		m_sCycloneDaySpan->SetValue(m_parent->GetCycloneParams().daySpan);
		m_cbAllTimesCyclones->SetValue(m_parent->GetCycloneParams().allTimesCyclones);

		// Appearance thresholds (visual only)
		m_sMinWind->SetValue((int)m_parent->GetCycloneParams().minWind);
		m_sMaxPressure->SetValue((int)m_parent->GetCycloneParams().maxPressure);

		// ENSO visibility (CycloneParams)
		m_cbElNino->SetValue(m_parent->GetCycloneParams().showElNino);
		m_cbLaNina->SetValue(m_parent->GetCycloneParams().showLaNina);
		m_cbNeutral->SetValue(m_parent->GetCycloneParams().showNeutral);
		m_cbUnknownENSO->SetValue(m_parent->GetCycloneParams().showNotAvailable);

		// Basin visibility (CycloneParams)
		m_cbEPA->SetValue(m_parent->GetCycloneParams().showEPA);
		m_cbWPA->SetValue(m_parent->GetCycloneParams().showWPA);
		m_cbSPA->SetValue(m_parent->GetCycloneParams().showSPA);
		m_cbATL->SetValue(m_parent->GetCycloneParams().showATL);
		m_cbNIO->SetValue(m_parent->GetCycloneParams().showNIO);
		m_cbSHE->SetValue(m_parent->GetCycloneParams().showSHE);

		// Storm-type mask (CycloneParams)  LEGACY
	//    uint32_t mask = m_parent->GetCycloneParams.stateMask;
	//    m_cbCycloneTropical->SetValue(mask & CYCLONE_TROPICAL);
	//    m_cbCycloneSubTropical->SetValue(mask & CYCLONE_SUBTROPICAL);
	//    m_cbCycloneExtraTropical->SetValue(mask & CYCLONE_EXTRATROPICAL);
	//    m_cbCycloneWave->SetValue(mask & CYCLONE_WAVE);
	//    m_cbCycloneRemanent->SetValue(mask & CYCLONE_REMANENT);
	//    m_cbCycloneUnknown->SetValue(mask & CYCLONE_UNKNOWN);
		
	}


	void ClimatologyConfigDialog::SyncCyclonesTabToModel()
	{
		// -----------------------------------------------------------------------
		// Appearance (CycloneParams)
		// -----------------------------------------------------------------------
		m_parent->GetCycloneParams().enabled          = m_cbCyclonesEnable->GetValue();
		m_parent->GetCycloneParams().centerDay        = m_sCycloneCenterDay->GetValue();
		m_parent->GetCycloneParams().daySpan          = m_sCycloneDaySpan->GetValue();
		m_parent->GetCycloneParams().allTimesCyclones = m_cbAllTimesCyclones->GetValue();

		// Appearance thresholds (visual only)
		m_parent->GetCycloneParams().minWind     = (double)m_sMinWind->GetValue();
		m_parent->GetCycloneParams().maxPressure = (double)m_sMaxPressure->GetValue();

		// ENSO visibility (appearance only)
		m_parent->GetCycloneParams().showElNino       = m_cbElNino->GetValue();
		m_parent->GetCycloneParams().showLaNina       = m_cbLaNina->GetValue();
		m_parent->GetCycloneParams().showNeutral      = m_cbNeutral->GetValue();
		m_parent->GetCycloneParams().showNotAvailable = m_cbUnknownENSO->GetValue();

		// Basin visibility (appearance only)
		m_parent->GetCycloneParams().showEPA = m_cbEPA->GetValue();
		m_parent->GetCycloneParams().showWPA = m_cbWPA->GetValue();
		m_parent->GetCycloneParams().showSPA = m_cbSPA->GetValue();
		m_parent->GetCycloneParams().showATL = m_cbATL->GetValue();
		m_parent->GetCycloneParams().showNIO = m_cbNIO->GetValue();
		m_parent->GetCycloneParams().showSHE = m_cbSHE->GetValue();

		// -----------------------------------------------------------------------
		// CycloneFilterParams (modern unified model)
		// -----------------------------------------------------------------------

		// ENSO filter
		m_parent->GetCycloneFilter().ensoFilter.clear();
		if (m_cbElNino->GetValue())      m_parent->GetCycloneFilter().ensoFilter.insert(CycloneENSO::EL_NINO);
		if (m_cbLaNina->GetValue())      m_parent->GetCycloneFilter().ensoFilter.insert(CycloneENSO::LA_NINA);
		if (m_cbNeutral->GetValue())     m_parent->GetCycloneFilter().ensoFilter.insert(CycloneENSO::NEUTRAL);
		if (m_cbUnknownENSO->GetValue()) m_parent->GetCycloneFilter().ensoFilter.insert(CycloneENSO::NA);

		// Storm-type filter
		m_parent->GetCycloneFilter().stateFilter.clear();
		if (m_cbCycloneTropical->GetValue())       m_parent->GetCycloneFilter().stateFilter.insert(CycloneState::TROPICAL);
		if (m_cbCycloneSubTropical->GetValue())    m_parent->GetCycloneFilter().stateFilter.insert(CycloneState::SUBTROPICAL);
		if (m_cbCycloneExtraTropical->GetValue())  m_parent->GetCycloneFilter().stateFilter.insert(CycloneState::EXTRATROPICAL);
		if (m_cbCycloneWave->GetValue())           m_parent->GetCycloneFilter().stateFilter.insert(CycloneState::WAVE);
		if (m_cbCycloneRemanent->GetValue())       m_parent->GetCycloneFilter().stateFilter.insert(CycloneState::REMANENT);
		if (m_cbCycloneUnknown->GetValue())        m_parent->GetCycloneFilter().stateFilter.insert(CycloneState::UNKNOWN);

		// Basin filter
		m_parent->GetCycloneFilter().basinFilter.clear();
		if (m_cbEPA->GetValue()) m_parent->GetCycloneFilter().basinFilter.insert(CycloneBasin::EPA);
		if (m_cbWPA->GetValue()) m_parent->GetCycloneFilter().basinFilter.insert(CycloneBasin::WPA);
		if (m_cbSPA->GetValue()) m_parent->GetCycloneFilter().basinFilter.insert(CycloneBasin::SPA);
		if (m_cbATL->GetValue()) m_parent->GetCycloneFilter().basinFilter.insert(CycloneBasin::ATL);
		if (m_cbNIO->GetValue()) m_parent->GetCycloneFilter().basinFilter.insert(CycloneBasin::NIO);
		if (m_cbSHE->GetValue()) m_parent->GetCycloneFilter().basinFilter.insert(CycloneBasin::SHE);

		// Intensity filters
		m_parent->GetCycloneFilter().windMin     = (double)m_sMinWind->GetValue();
		m_parent->GetCycloneFilter().windMax     = 999.0;     // UI does not expose max wind
		m_parent->GetCycloneFilter().pressureMin = 0.0;       // UI does not expose min pressure
		m_parent->GetCycloneFilter().pressureMax = (double)m_sMaxPressure->GetValue();

		// Timeline window (used by interpolation)
		m_parent->GetCycloneFilter().daySpan = m_sCycloneDaySpan->GetValue();

		// Month filter: empty = all months (UI does not expose month selection)
		m_parent->GetCycloneFilter().monthFilter.clear();

		// Year range (UI does not expose year selection)
		m_parent->GetCycloneFilter().yearMin = 1850;
		m_parent->GetCycloneFilter().yearMax = 2100;

		// Date range (UI does not expose date selection)
		m_parent->GetCycloneFilter().startDate = wxDateTime(1, wxDateTime::Jan, 1850);
		m_parent->GetCycloneFilter().endDate   = wxDateTime(31, wxDateTime::Dec, 2100);
	}


	// MODERN  - Cyclone Filter
	CycloneFilterParams ClimatologyConfigDialog::GetCycloneFilterParams() const
	{
		CycloneFilterParams p;

		// Intensity filters
		p.windMin     = m_sMinWind->GetValue();
		p.windMax     = 999.0;
		p.pressureMin = 0.0;
		p.pressureMax = m_sMaxPressure->GetValue();

		// ENSO filter
		if (m_cbElNino->GetValue())      p.ensoFilter.insert(CycloneENSO::EL_NINO);
		if (m_cbLaNina->GetValue())      p.ensoFilter.insert(CycloneENSO::LA_NINA);
		if (m_cbNeutral->GetValue())     p.ensoFilter.insert(CycloneENSO::NEUTRAL);
		if (m_cbUnknownENSO->GetValue()) p.ensoFilter.insert(CycloneENSO::NA);

		// Storm-type filter
		if (m_cbCycloneTropical->GetValue())      p.stateFilter.insert(CycloneState::TROPICAL);
		if (m_cbCycloneSubTropical->GetValue())   p.stateFilter.insert(CycloneState::SUBTROPICAL);
		if (m_cbCycloneExtraTropical->GetValue()) p.stateFilter.insert(CycloneState::EXTRATROPICAL);
		if (m_cbCycloneWave->GetValue())          p.stateFilter.insert(CycloneState::WAVE);
		if (m_cbCycloneRemanent->GetValue())      p.stateFilter.insert(CycloneState::REMANENT);
		if (m_cbCycloneUnknown->GetValue())       p.stateFilter.insert(CycloneState::UNKNOWN);

		// Basin filter
		if (m_cbEPA->GetValue()) p.basinFilter.insert(CycloneBasin::EPA);
		if (m_cbWPA->GetValue()) p.basinFilter.insert(CycloneBasin::WPA);
		if (m_cbSPA->GetValue()) p.basinFilter.insert(CycloneBasin::SPA);
		if (m_cbATL->GetValue()) p.basinFilter.insert(CycloneBasin::ATL);
		if (m_cbNIO->GetValue()) p.basinFilter.insert(CycloneBasin::NIO);
		if (m_cbSHE->GetValue()) p.basinFilter.insert(CycloneBasin::SHE);

		// Timeline window
		p.daySpan = m_sCycloneDaySpan->GetValue();
		
		//Should you add year/month filtering later?
		// YES — UnifiedGrid supports year indexing (yearToTracks)
		//  YES — CycloneFilterParams supports yearMin/yearMax/monthFilter
		// Add it later when the UI is ready  Phase 2

		return p;
	}

	// MODERN — cyclone UI update handler
	void ClimatologyConfigDialog::OnUpdateCyclones(wxEvent& event)
	{
		SyncCyclonesTabToModel();
		m_parent->PushParamsToFactoryAndRender();
	}

	// MODERN — cyclone spin control update handler
	void ClimatologyConfigDialog::OnUpdateCyclonesSpin(wxSpinEvent& event)
	{
		SyncCyclonesTabToModel();
		m_parent->PushParamsToFactoryAndRender();
	}

	// MODERN — cyclone spin control update handler
	void ClimatologyConfigDialog::OnCycloneFilterChanged(wxCommandEvent& event)
	{
		SyncCyclonesTabToModel();
		m_parent->PushParamsToFactoryAndRender();
	}
	
	void ClimatologyConfigDialog::OnPageChanged(wxBookCtrlEvent& event)
	{
		event.Skip();
	}

	void ClimatologyConfigDialog::OnRefreshTimer(wxTimerEvent& event)
	{
		event.Skip();
	}

	// ---------------------------------------------------------------------------
	// UI <-> About Tab  Info
	// ---------------------------------------------------------------------------

void ClimatologyConfigDialog::OnRedownloadAll(wxCommandEvent& event)
{
    wxLogMessage("Climatology: Redownload All triggered");

    // Prevent double-clicking the button during an active download
	auto* pi = m_parent->GetPlugin();
	auto* dm = pi->GetDownloadManager();

	if (dm->IsBusy()) {
		wxMessageBox(_("Download already in progress."), _("Climatology"));
		return;
	}

	wxString dataDir = pi->GetDataDir();

    wxDir dir(dataDir);
    wxString filename;
    bool cont = dir.GetFirst(&filename);
    while (cont) {
        wxRemoveFile(dataDir + filename);
        cont = dir.GetNext(&filename);
    }

    // Restart download interactively
    m_parent->GetPlugin()->StartDownload(true);
}


void ClimatologyConfigDialog::OnDownloadMissing(wxCommandEvent& event)
{
    wxLogMessage("Climatology: Download Missing Files Only triggered");

	auto* pi = m_parent->GetPlugin();
    auto* dm = pi->GetDownloadManager();

     if (!dm) {
        wxMessageBox(_("Download manager not initialized."),
                     _("Climatology"));
        return;
    }

    // Prevent double-clicking during active download
    if (dm->IsBusy()) {
        wxMessageBox(_("Download already in progress."), _("Climatology"));
        return;
    }

    // Build a filtered manifest containing only missing or zero-size files
    std::vector<ManifestEntry> missing;

    const auto& files = dm->GetManifest();   // accessor returning const 
    for (const auto& entry : files) {
        wxString path = pi->GetDataDir() + entry.filename;

        if (!wxFileExists(path) || wxFileName::GetSize(path) == 0)
            missing.push_back(entry);
    }

    if (missing.empty()) {
        wxMessageBox(_("All files are present and valid."), _("Climatology"));
        return;
    }

    wxLogMessage("Climatology: %zu missing files will be downloaded", missing.size());

    // Replace manifest with only missing files
    dm->SetManifest(missing);

    // Start download interactively
    pi->StartDownload(true);
}



void ClimatologyConfigDialog::OnCheckIntegrity(wxCommandEvent& event)
{
	auto* pi = m_parent->GetPlugin();
    auto* dm = pi->GetDownloadManager();
	
    if (!dm) {
        wxMessageBox(_("Download manager not initialized."),
                     _("Climatology Data Integrity"));
        return;
    }

    bool ok = dm->CheckIntegrity();

    wxMessageBox(ok ? _("All climatology data files are valid.")
                    : _("Some climatology data files are missing or corrupted."),
                 _("Climatology Data Integrity"));
}
