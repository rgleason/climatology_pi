/******************************************************************************
 *
 * Project:  OpenCPN
 * Purpose:  Climatology Plugin - Configuration Dialog
 * Author:   Sean D'Epagnier, modernized by refactor
 *
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
 ***************************************************************************
 */

/******************************************************************************
 * ClimatologyConfigDialog.cpp
 * Fully corrected version with unified-grid Blend Mode support and
 * StandardDisplayParams integration.
 ******************************************************************************/
/******************************************************************************
 *
 * Project:  OpenCPN
 * Purpose:  Climatology Plugin - 4-tab configuration dialog
 * Author:   Modernized for unified-grid architecture
 *
 ***************************************************************************
 */

#include <GL/glew.h>
#include "gldefs.h"

#include "wx/wx.h"
#include "wx/datetime.h"
#include <wx/dir.h>
#include <wx/dcbuffer.h>
#include <wx/debug.h>
#include <wx/graphics.h>

#include "ocpn_plugin.h"             // GetOCPNConfigObject, DimeWindow
#include "ClimatologyConfigDialog.h"
#include "ClimatologyDialog.h"
#include "climatology_pi.h"          // PLUGIN_VERSION_*
#include "ClimatologyOverlayFactory.h"
#include "ClimatologyEnums.h"      // basin / state enums if needed

wxBEGIN_EVENT_TABLE(ClimatologyConfigDialog, wxDialog)
    EVT_NOTEBOOK_PAGE_CHANGED(wxID_ANY, ClimatologyConfigDialog::OnPageChanged)
    EVT_TIMER(wxID_ANY, ClimatologyConfigDialog::OnRefreshTimer)
wxEND_EVENT_TABLE()

// ---------------------------------------------------------------------------
// Static helpers: unit labels per overlay (UI only)
// ---------------------------------------------------------------------------

static const wxString units_wind[]      = {_("Knots"), _("m/s"), _("mph"), _("km/h"), wxEmptyString};
static const wxString units_pressure[]  = {_("mbar"), _("mmHg"), wxEmptyString};
static const wxString units_precip[]   = {_("mm/day"), _("in/day"), _("mm/month"), _("m/month"),
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
// Constructor
// ---------------------------------------------------------------------------

ClimatologyConfigDialog::ClimatologyConfigDialog(ClimatologyDialog* parent,
                                                 StandardDisplayParams& displayParams,
                                                 CycloneParams& cycloneParams)
    : wxDialog(parent,
               wxID_ANY,
               _("Climatology Configuration"),
               wxDefaultPosition,
               wxDefaultSize,
               wxDEFAULT_DIALOG_STYLE | wxSTAY_ON_TOP),
      m_parent(parent),
      m_refreshTimer(this),
      m_displayParams(displayParams),
      m_cycloneParams(cycloneParams)
{
    // --- Notebook + tabs ---------------------------------------------------
    m_notebook      = new wxNotebook(this, wxID_ANY);
    m_panelStandard = new wxPanel(m_notebook);
    m_panelWindAtlas= new wxPanel(m_notebook);
    m_panelCyclones = new wxPanel(m_notebook);
    m_panelInfo     = new wxPanel(m_notebook);

    m_notebook->AddPage(m_panelStandard, _("Standard"));
    m_notebook->AddPage(m_panelWindAtlas, _("Wind Atlas"));
    m_notebook->AddPage(m_panelCyclones, _("Cyclones"));
    m_notebook->AddPage(m_panelInfo, _("Information"));

    // --- Standard tab layout ----------------------------------------------
    {
        wxBoxSizer* sizer = new wxBoxSizer(wxVERTICAL);

        // Overlay type
        wxBoxSizer* rowOverlay = new wxBoxSizer(wxHORIZONTAL);
        rowOverlay->Add(new wxStaticText(m_panelStandard, wxID_ANY, _("Overlay Type")),
                        0, wxALL | wxALIGN_CENTER_VERTICAL, 5);
        m_cOverlayType = new wxChoice(m_panelStandard, wxID_ANY);
        m_cOverlayType->Append(_("Wind"));
        m_cOverlayType->Append(_("Wind Atlas"));
        m_cOverlayType->Append(_("Cyclones"));
        m_cOverlayType->Append(_("Current"));
        m_cOverlayType->Append(_("Pressure"));
        m_cOverlayType->Append(_("Sea Surface Temp"));
        m_cOverlayType->Append(_("Air Temp"));
        m_cOverlayType->Append(_("Cloud"));
        m_cOverlayType->Append(_("Precipitation"));
        m_cOverlayType->Append(_("Relative Humidity"));
        m_cOverlayType->Append(_("Lightning"));
        m_cOverlayType->Append(_("Sea Depth"));
        rowOverlay->Add(m_cOverlayType, 1, wxALL | wxEXPAND, 5);
        sizer->Add(rowOverlay, 0, wxEXPAND);

        // Units
        wxBoxSizer* rowUnits = new wxBoxSizer(wxHORIZONTAL);
        rowUnits->Add(new wxStaticText(m_panelStandard, wxID_ANY, _("Units")),
                      0, wxALL | wxALIGN_CENTER_VERTICAL, 5);
        m_cUnits = new wxChoice(m_panelStandard, wxID_ANY);
        rowUnits->Add(m_cUnits, 1, wxALL | wxEXPAND, 5);
        sizer->Add(rowUnits, 0, wxEXPAND);

        // Blend Mode
        wxBoxSizer* rowBlend = new wxBoxSizer(wxHORIZONTAL);
        rowBlend->Add(new wxStaticText(m_panelStandard, wxID_ANY, _("Blend Mode")),
                      0, wxALL | wxALIGN_CENTER_VERTICAL, 5);
        m_cBlendMode = new wxChoice(m_panelStandard, wxID_ANY);
        m_cBlendMode->Append(_("Single"));
        m_cBlendMode->Append(_("Weighted"));
        m_cBlendMode->Append(_("Mean"));
        m_cBlendMode->Append(_("Median"));
        m_cBlendMode->Append(_("Max"));
        m_cBlendMode->Append(_("Min"));
        m_cBlendMode->Append(_("Additive"));
        rowBlend->Add(m_cBlendMode, 1, wxALL | wxEXPAND, 5);
        sizer->Add(rowBlend, 0, wxEXPAND);

        // Overlay visibility + transparency
        m_cbShowOverlay = new wxCheckBox(m_panelStandard, wxID_ANY, _("Show overlay"));
        sizer->Add(m_cbShowOverlay, 0, wxALL, 5);

        m_sTransparency = new wxSlider(m_panelStandard, wxID_ANY,
                                       100, 0, 100,
                                       wxDefaultPosition, wxDefaultSize,
                                       wxSL_HORIZONTAL | wxSL_LABELS);
        sizer->Add(new wxStaticText(m_panelStandard, wxID_ANY, _("Transparency")), 0, wxALL, 5);
        sizer->Add(m_sTransparency, 0, wxALL | wxEXPAND, 5);

        // IsoBars
        m_cbShowIsoBars = new wxCheckBox(m_panelStandard, wxID_ANY, _("Show isobars"));
        sizer->Add(m_cbShowIsoBars, 0, wxALL, 5);

        wxBoxSizer* rowIso = new wxBoxSizer(wxHORIZONTAL);
        rowIso->Add(new wxStaticText(m_panelStandard, wxID_ANY, _("Iso spacing")),
                    0, wxALL | wxALIGN_CENTER_VERTICAL, 5);
        m_sIsoSpacing = new wxSpinCtrl(m_panelStandard, wxID_ANY);
        rowIso->Add(m_sIsoSpacing, 0, wxALL, 5);
        rowIso->Add(new wxStaticText(m_panelStandard, wxID_ANY, _("Iso step")),
                    0, wxALL | wxALIGN_CENTER_VERTICAL, 5);
        m_cIsoStep = new wxChoice(m_panelStandard, wxID_ANY);
        m_cIsoStep->Append(_("1"));
        m_cIsoStep->Append(_("2"));
        m_cIsoStep->Append(_("5"));
        rowIso->Add(m_cIsoStep, 0, wxALL, 5);
        sizer->Add(rowIso, 0, wxEXPAND);

        // Numbers
        m_cbShowNumbers = new wxCheckBox(m_panelStandard, wxID_ANY, _("Show numbers"));
        sizer->Add(m_cbShowNumbers, 0, wxALL, 5);

        wxBoxSizer* rowNum = new wxBoxSizer(wxHORIZONTAL);
        rowNum->Add(new wxStaticText(m_panelStandard, wxID_ANY, _("Number size")),
                    0, wxALL | wxALIGN_CENTER_VERTICAL, 5);
        m_sNumberSize = new wxSpinCtrl(m_panelStandard, wxID_ANY);
        rowNum->Add(m_sNumberSize, 0, wxALL, 5);
        sizer->Add(rowNum, 0, wxEXPAND);

        // Arrows
        m_cbShowArrows = new wxCheckBox(m_panelStandard, wxID_ANY, _("Show arrows"));
        sizer->Add(m_cbShowArrows, 0, wxALL, 5);

        wxBoxSizer* rowArrows = new wxBoxSizer(wxHORIZONTAL);
        rowArrows->Add(new wxStaticText(m_panelStandard, wxID_ANY, _("Width")),
                       0, wxALL | wxALIGN_CENTER_VERTICAL, 5);
        m_sArrowWidth = new wxSpinCtrl(m_panelStandard, wxID_ANY);
        rowArrows->Add(m_sArrowWidth, 0, wxALL, 5);

        rowArrows->Add(new wxStaticText(m_panelStandard, wxID_ANY, _("Spacing")),
                       0, wxALL | wxALIGN_CENTER_VERTICAL, 5);
        m_sArrowSpacing = new wxSpinCtrl(m_panelStandard, wxID_ANY);
        rowArrows->Add(m_sArrowSpacing, 0, wxALL, 5);

        rowArrows->Add(new wxStaticText(m_panelStandard, wxID_ANY, _("Size")),
                       0, wxALL | wxALIGN_CENTER_VERTICAL, 5);
        m_sArrowSize = new wxSpinCtrl(m_panelStandard, wxID_ANY);
        rowArrows->Add(m_sArrowSize, 0, wxALL, 5);

        rowArrows->Add(new wxStaticText(m_panelStandard, wxID_ANY, _("Mode")),
                       0, wxALL | wxALIGN_CENTER_VERTICAL, 5);
        m_cArrowMode = new wxChoice(m_panelStandard, wxID_ANY);
        m_cArrowMode->Append(_("Barbs"));
        m_cArrowMode->Append(_("Length"));
        rowArrows->Add(m_cArrowMode, 0, wxALL, 5);

        sizer->Add(rowArrows, 0, wxEXPAND);

        // Color
        sizer->Add(new wxStaticText(m_panelStandard, wxID_ANY, _("Color")), 0, wxALL, 5);
        m_cpOverlayColor = new wxColourPickerCtrl(m_panelStandard, wxID_ANY);
        sizer->Add(m_cpOverlayColor, 0, wxALL, 5);

        m_panelStandard->SetSizer(sizer);
    }

    // --- Wind Atlas tab ----------------------------------------------------
    {
        wxBoxSizer* sizer = new wxBoxSizer(wxVERTICAL);

        m_cbWindAtlasEnable = new wxCheckBox(m_panelWindAtlas, wxID_ANY, _("Enable Wind Atlas"));
        sizer->Add(m_cbWindAtlasEnable, 0, wxALL, 5);

        wxBoxSizer* rowSize = new wxBoxSizer(wxHORIZONTAL);
        rowSize->Add(new wxStaticText(m_panelWindAtlas, wxID_ANY, _("Size")),
                     0, wxALL | wxALIGN_CENTER_VERTICAL, 5);
        m_sWindAtlasSize = new wxSpinCtrl(m_panelWindAtlas, wxID_ANY);
        rowSize->Add(m_sWindAtlasSize, 0, wxALL, 5);
        sizer->Add(rowSize, 0, wxEXPAND);

        wxBoxSizer* rowSpacing = new wxBoxSizer(wxHORIZONTAL);
        rowSpacing->Add(new wxStaticText(m_panelWindAtlas, wxID_ANY, _("Spacing")),
                        0, wxALL | wxALIGN_CENTER_VERTICAL, 5);
        m_sWindAtlasSpacing = new wxSpinCtrl(m_panelWindAtlas, wxID_ANY);
        rowSpacing->Add(m_sWindAtlasSpacing, 0, wxALL, 5);
        sizer->Add(rowSpacing, 0, wxEXPAND);

        sizer->Add(new wxStaticText(m_panelWindAtlas, wxID_ANY, _("Opacity")), 0, wxALL, 5);
        m_sWindAtlasOpacity = new wxSlider(m_panelWindAtlas, wxID_ANY,
                                           100, 0, 100,
                                           wxDefaultPosition, wxDefaultSize,
                                           wxSL_HORIZONTAL | wxSL_LABELS);
        sizer->Add(m_sWindAtlasOpacity, 0, wxALL | wxEXPAND, 5);

        m_panelWindAtlas->SetSizer(sizer);
    }

    // --- Cyclones tab ------------------------------------------------------
    {
        wxBoxSizer* sizer = new wxBoxSizer(wxVERTICAL);

        m_cbCyclonesEnable = new wxCheckBox(m_panelCyclones, wxID_ANY, _("Enable Cyclones"));
        sizer->Add(m_cbCyclonesEnable, 0, wxALL, 5);

        // Timeline
        wxBoxSizer* rowTime = new wxBoxSizer(wxHORIZONTAL);
        rowTime->Add(new wxStaticText(m_panelCyclones, wxID_ANY, _("Center day")),
                     0, wxALL | wxALIGN_CENTER_VERTICAL, 5);
        m_sCycloneCenterDay = new wxSpinCtrl(m_panelCyclones, wxID_ANY);
        rowTime->Add(m_sCycloneCenterDay, 0, wxALL, 5);

        rowTime->Add(new wxStaticText(m_panelCyclones, wxID_ANY, _("Day span")),
                     0, wxALL | wxALIGN_CENTER_VERTICAL, 5);
        m_sCycloneDaySpan = new wxSpinCtrl(m_panelCyclones, wxID_ANY);
        rowTime->Add(m_sCycloneDaySpan, 0, wxALL, 5);
        sizer->Add(rowTime, 0, wxEXPAND);

        // Intensity thresholds
        wxBoxSizer* rowInt = new wxBoxSizer(wxHORIZONTAL);
        rowInt->Add(new wxStaticText(m_panelCyclones, wxID_ANY, _("Min wind")),
                    0, wxALL | wxALIGN_CENTER_VERTICAL, 5);
        m_sMinWind = new wxSpinCtrl(m_panelCyclones, wxID_ANY);
        rowInt->Add(m_sMinWind, 0, wxALL, 5);

        rowInt->Add(new wxStaticText(m_panelCyclones, wxID_ANY, _("Max pressure")),
                    0, wxALL | wxALIGN_CENTER_VERTICAL, 5);
        m_sMaxPressure = new wxSpinCtrl(m_panelCyclones, wxID_ANY);
        rowInt->Add(m_sMaxPressure, 0, wxALL, 5);
        sizer->Add(rowInt, 0, wxEXPAND);

        // ENSO
        sizer->Add(new wxStaticText(m_panelCyclones, wxID_ANY, _("ENSO visibility")), 0, wxALL, 5);
        m_cbElNino      = new wxCheckBox(m_panelCyclones, wxID_ANY, _("El Niño"));
        m_cbLaNina      = new wxCheckBox(m_panelCyclones, wxID_ANY, _("La Niña"));
        m_cbNeutral     = new wxCheckBox(m_panelCyclones, wxID_ANY, _("Neutral"));
        m_cbUnknownENSO = new wxCheckBox(m_panelCyclones, wxID_ANY, _("Unknown"));

        sizer->Add(m_cbElNino,      0, wxALL, 5);
        sizer->Add(m_cbLaNina,      0, wxALL, 5);
        sizer->Add(m_cbNeutral,     0, wxALL, 5);
        sizer->Add(m_cbUnknownENSO, 0, wxALL, 5);

        // Storm-type mask
        sizer->Add(new wxStaticText(m_panelCyclones, wxID_ANY, _("Storm types")), 0, wxALL, 5);
        m_cbCycloneTropical    = new wxCheckBox(m_panelCyclones, wxID_ANY, _("Tropical"));
        m_cbCycloneSubTropical = new wxCheckBox(m_panelCyclones, wxID_ANY, _("Subtropical"));
        m_cbCycloneExtraTropical = new wxCheckBox(m_panelCyclones, wxID_ANY, _("Extratropical"));
        m_cbCycloneWave        = new wxCheckBox(m_panelCyclones, wxID_ANY, _("Wave"));
        m_cbCycloneRemanent    = new wxCheckBox(m_panelCyclones, wxID_ANY, _("Remnant"));
        m_cbCycloneUnknown     = new wxCheckBox(m_panelCyclones, wxID_ANY, _("Unknown"));

        sizer->Add(m_cbCycloneTropical,    0, wxALL, 5);
        sizer->Add(m_cbCycloneSubTropical, 0, wxALL, 5);
        sizer->Add(m_cbCycloneExtraTropical, 0, wxALL, 5);
        sizer->Add(m_cbCycloneWave,        0, wxALL, 5);
        sizer->Add(m_cbCycloneRemanent,    0, wxALL, 5);
        sizer->Add(m_cbCycloneUnknown,     0, wxALL, 5);

        // Basins
        sizer->Add(new wxStaticText(m_panelCyclones, wxID_ANY, _("Basins")), 0, wxALL, 5);
        m_cbEPA = new wxCheckBox(m_panelCyclones, wxID_ANY, _("Eastern Pacific"));
        m_cbWPA = new wxCheckBox(m_panelCyclones, wxID_ANY, _("Western Pacific"));
        m_cbSPA = new wxCheckBox(m_panelCyclones, wxID_ANY, _("South Pacific"));
        m_cbATL = new wxCheckBox(m_panelCyclones, wxID_ANY, _("Atlantic"));
        m_cbNIO = new wxCheckBox(m_panelCyclones, wxID_ANY, _("North Indian Ocean"));
        m_cbSHE = new wxCheckBox(m_panelCyclones, wxID_ANY, _("Southern Hemisphere"));

        sizer->Add(m_cbEPA, 0, wxALL, 5);
        sizer->Add(m_cbWPA, 0, wxALL, 5);
        sizer->Add(m_cbSPA, 0, wxALL, 5);
        sizer->Add(m_cbATL, 0, wxALL, 5);
        sizer->Add(m_cbNIO, 0, wxALL, 5);
        sizer->Add(m_cbSHE, 0, wxALL, 5);

        // All-times
        m_cbAllTimesCyclones = new wxCheckBox(m_panelCyclones, wxID_ANY, _("All-times mode"));
        sizer->Add(m_cbAllTimesCyclones, 0, wxALL, 5);

        m_panelCyclones->SetSizer(sizer);
    }

    // --- Information tab ---------------------------------------------------
    {
        wxBoxSizer* sizer = new wxBoxSizer(wxVERTICAL);
        m_htmlInformation = new wxHtmlWindow(m_panelInfo, wxID_ANY);
        sizer->Add(m_htmlInformation, 1, wxALL | wxEXPAND, 5);
        m_panelInfo->SetSizer(sizer);
    }

    // --- Top-level sizer ---------------------------------------------------
    {
        wxBoxSizer* topsizer = new wxBoxSizer(wxVERTICAL);

        // Version + data dir (optional)
        wxStaticText* version = new wxStaticText(this, wxID_ANY,
            wxString::Format(_("Version %d.%d.%d.%d"),
                             PLUGIN_VERSION_MAJOR,
                             PLUGIN_VERSION_MINOR,
                             PLUGIN_VERSION_PATCH,
                             PLUGIN_VERSION_TWEAK));
        topsizer->Add(version, 0, wxALL, 5);

        topsizer->Add(m_notebook, 1, wxALL | wxEXPAND, 5);

        // Buttons
        wxBoxSizer* btns = new wxBoxSizer(wxHORIZONTAL);
        wxButton* btnOK     = new wxButton(this, wxID_OK, _("OK"));
        wxButton* btnCancel = new wxButton(this, wxID_CANCEL, _("Cancel"));
        wxButton* btnAbout  = new wxButton(this, wxID_ANY, _("About"));
        btns->Add(btnOK,     0, wxALL, 5);
        btns->Add(btnCancel, 0, wxALL, 5);
        btns->Add(btnAbout,  0, wxALL, 5);
        topsizer->Add(btns, 0, wxALIGN_RIGHT);

        SetSizerAndFit(topsizer);

        // Bind buttons
        btnOK->Bind(wxEVT_BUTTON, &ClimatologyConfigDialog::OnEnabled, this);
        btnCancel->Bind(wxEVT_BUTTON, &ClimatologyConfigDialog::OnClose, this);
        btnAbout->Bind(wxEVT_BUTTON, &ClimatologyConfigDialog::OnAboutAuthor, this);
    }

    // Theme
    ApplyTheme();

    // Load persisted settings into model + UI
    LoadSettings();
    SyncStandardTabFromModel();
    SyncWindAtlasTabFromModel();
    SyncCyclonesTabFromModel();

    // Bind generic updates
    m_cOverlayType->Bind(wxEVT_CHOICE, &ClimatologyConfigDialog::OnUpdate, this);
    m_cUnits->Bind(wxEVT_CHOICE, &ClimatologyConfigDialog::OnUpdate, this);
    m_cBlendMode->Bind(wxEVT_CHOICE, &ClimatologyConfigDialog::OnUpdate, this);

    m_cbShowOverlay->Bind(wxEVT_CHECKBOX, &ClimatologyConfigDialog::OnUpdate, this);
    m_sTransparency->Bind(wxEVT_SLIDER, &ClimatologyConfigDialog::OnUpdateScroll, this);

    m_cbShowIsoBars->Bind(wxEVT_CHECKBOX, &ClimatologyConfigDialog::OnUpdateOverlayConfig, this);
    m_sIsoSpacing->Bind(wxEVT_SPINCTRL, &ClimatologyConfigDialog::OnUpdateSpin, this);
    m_cIsoStep->Bind(wxEVT_CHOICE, &ClimatologyConfigDialog::OnUpdate, this);

    m_cbShowNumbers->Bind(wxEVT_CHECKBOX, &ClimatologyConfigDialog::OnUpdate, this);
    m_sNumberSize->Bind(wxEVT_SPINCTRL, &ClimatologyConfigDialog::OnUpdateSpin, this);

    m_cbShowArrows->Bind(wxEVT_CHECKBOX, &ClimatologyConfigDialog::OnUpdate, this);
    m_sArrowWidth->Bind(wxEVT_SPINCTRL, &ClimatologyConfigDialog::OnUpdateSpin, this);
    m_sArrowSpacing->Bind(wxEVT_SPINCTRL, &ClimatologyConfigDialog::OnUpdateSpin, this);
    m_sArrowSize->Bind(wxEVT_SPINCTRL, &ClimatologyConfigDialog::OnUpdateSpin, this);
    m_cArrowMode->Bind(wxEVT_CHOICE, &ClimatologyConfigDialog::OnUpdate, this);
    m_cpOverlayColor->Bind(wxEVT_COLOURPICKER_CHANGED, &ClimatologyConfigDialog::OnUpdateColor, this);

    // Cyclone bindings
    m_cbCyclonesEnable->Bind(wxEVT_CHECKBOX, &ClimatologyConfigDialog::OnUpdateCyclones, this);
    m_sCycloneCenterDay->Bind(wxEVT_SPINCTRL, &ClimatologyConfigDialog::OnUpdateCyclonesSpin, this);
    m_sCycloneDaySpan->Bind(wxEVT_SPINCTRL, &ClimatologyConfigDialog::OnUpdateCyclonesSpin, this);
    m_sMinWind->Bind(wxEVT_SPINCTRL, &ClimatologyConfigDialog::OnCycloneFilterChanged, this);
    m_sMaxPressure->Bind(wxEVT_SPINCTRL, &ClimatologyConfigDialog::OnCycloneFilterChanged, this);

    m_cbElNino->Bind(wxEVT_CHECKBOX, &ClimatologyConfigDialog::OnCycloneFilterChanged, this);
    m_cbLaNina->Bind(wxEVT_CHECKBOX, &ClimatologyConfigDialog::OnCycloneFilterChanged, this);
    m_cbNeutral->Bind(wxEVT_CHECKBOX, &ClimatologyConfigDialog::OnCycloneFilterChanged, this);
    m_cbUnknownENSO->Bind(wxEVT_CHECKBOX, &ClimatologyConfigDialog::OnCycloneFilterChanged, this);

    m_cbCycloneTropical->Bind(wxEVT_CHECKBOX, &ClimatologyConfigDialog::OnCycloneFilterChanged, this);
    m_cbCycloneSubTropical->Bind(wxEVT_CHECKBOX, &ClimatologyConfigDialog::OnCycloneFilterChanged, this);
    m_cbCycloneExtraTropical->Bind(wxEVT_CHECKBOX, &ClimatologyConfigDialog::OnCycloneFilterChanged, this);
    m_cbCycloneWave->Bind(wxEVT_CHECKBOX, &ClimatologyConfigDialog::OnCycloneFilterChanged, this);
    m_cbCycloneRemanent->Bind(wxEVT_CHECKBOX, &ClimatologyConfigDialog::OnCycloneFilterChanged, this);
    m_cbCycloneUnknown->Bind(wxEVT_CHECKBOX, &ClimatologyConfigDialog::OnCycloneFilterChanged, this);

    m_cbEPA->Bind(wxEVT_CHECKBOX, &ClimatologyConfigDialog::OnCycloneFilterChanged, this);
    m_cbWPA->Bind(wxEVT_CHECKBOX, &ClimatologyConfigDialog::OnCycloneFilterChanged, this);
    m_cbSPA->Bind(wxEVT_CHECKBOX, &ClimatologyConfigDialog::OnCycloneFilterChanged, this);
    m_cbATL->Bind(wxEVT_CHECKBOX, &ClimatologyConfigDialog::OnCycloneFilterChanged, this);
    m_cbNIO->Bind(wxEVT_CHECKBOX, &ClimatologyConfigDialog::OnCycloneFilterChanged, this);
    m_cbSHE->Bind(wxEVT_CHECKBOX, &ClimatologyConfigDialog::OnCycloneFilterChanged, this);

    m_cbAllTimesCyclones->Bind(wxEVT_CHECKBOX, &ClimatologyConfigDialog::OnCycloneFilterChanged, this);
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

void ClimatologyConfigDialog::ApplyTheme()
{
    // DimeWindow recolors the dialog to match OpenCPN's current theme.
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
    m_displayParams.windAtlas.enabled = (bool)pConf->Read("Enabled", false);
    m_displayParams.windAtlas.size    = (double)pConf->Read("Size1", 120L);
    m_displayParams.windAtlas.spacing = (double)pConf->Read("Spacing1", 80L);
    m_displayParams.windAtlas.opacity = (double)pConf->Read("Opacity", 205L) / 255.0;

    // Cyclones (persistent appearance)
    pConf->SetPath("/PlugIns/Climatology/Cyclones");
    m_cycloneParams.enabled      = (bool)pConf->Read("Enabled", false);
    m_cycloneParams.centerDay    = (int)pConf->Read("CenterDay", 180L);
    m_cycloneParams.daySpan      = (int)pConf->Read("CycloneDaySpan", 30L);
    m_cycloneParams.minWind      = (double)pConf->Read("MinWindSpeed", 35L);
    m_cycloneParams.maxPressure  = (double)pConf->Read("MaxPressure", 1080L);
    m_cycloneParams.allTimesCyclones = (bool)pConf->Read("AllTimesCyclones", false);

    // ENSO + basins + stateMask could be persisted similarly if desired.
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
    pConf->Write("Opacity",  (long)(m_displayParams.windAtlas.opacity * 255.0));

    pConf->SetPath("/PlugIns/Climatology/Cyclones");
    pConf->Write("Enabled",        m_cycloneParams.enabled);
    pConf->Write("CenterDay",      (long)m_cycloneParams.centerDay);
    pConf->Write("CycloneDaySpan", (long)m_cycloneParams.daySpan);
    pConf->Write("MinWindSpeed",   (long)m_cycloneParams.minWind);
    pConf->Write("MaxPressure",    (long)m_cycloneParams.maxPressure);
    pConf->Write("AllTimesCyclones", m_cycloneParams.allTimesCyclones);
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

    m_displayParams.showArrows[idx]   = m_cbShowArrows->GetValue();
    m_displayParams.arrowWidth[idx]   = m_sArrowWidth->GetValue();
    m_displayParams.arrowSpacing[idx] = m_sArrowSpacing->GetValue();
    m_displayParams.arrowSize[idx]    = m_sArrowSize->GetValue();
    m_displayParams.arrowMode[idx]    = m_cArrowMode->GetSelection();

    m_displayParams.color[idx] = m_cpOverlayColor->GetColour();
}

// ---------------------------------------------------------------------------
// UI <-> model sync: Wind Atlas tab
// ---------------------------------------------------------------------------

void ClimatologyConfigDialog::SyncWindAtlasTabFromModel()
{
    m_cbWindAtlasEnable->SetValue(m_displayParams.windAtlas.enabled);
    m_sWindAtlasSize->SetValue((int)m_displayParams.windAtlas.size);
    m_sWindAtlasSpacing->SetValue((int)m_displayParams.windAtlas.spacing);
    m_sWindAtlasOpacity->SetValue((int)(m_displayParams.windAtlas.opacity * 100.0));
}

void ClimatologyConfigDialog::SyncWindAtlasTabToModel()
{
    m_displayParams.windAtlas.enabled = m_cbWindAtlasEnable->GetValue();
    m_displayParams.windAtlas.size    = (double)m_sWindAtlasSize->GetValue();
    m_displayParams.windAtlas.spacing = (double)m_sWindAtlasSpacing->GetValue();
    m_displayParams.windAtlas.opacity = m_sWindAtlasOpacity->GetValue() / 100.0;
}

// ---------------------------------------------------------------------------
// UI <-> model sync: Cyclones tab (CycloneParams)
// ---------------------------------------------------------------------------

void ClimatologyConfigDialog::SyncCyclonesTabFromModel()
{
    m_cbCyclonesEnable->SetValue(m_cycloneParams.enabled);
    m_sCycloneCenterDay->SetValue(m_cycloneParams.centerDay);
    m_sCycloneDaySpan->SetValue(m_cycloneParams.daySpan);

    m_sMinWind->SetValue((int)m_cycloneParams.minWind);
    m_sMaxPressure->SetValue((int)m_cycloneParams.maxPressure);

    m_cbElNino->SetValue(m_cycloneParams.showElNino);
    m_cbLaNina->SetValue(m_cycloneParams.showLaNina);
    m_cbNeutral->SetValue(m_cycloneParams.showNeutral);
    m_cbUnknownENSO->SetValue(m_cycloneParams.showNotAvailable);

    m_cbEPA->SetValue(m_cycloneParams.showEPA);
    m_cbWPA->SetValue(m_cycloneParams.showWPA);
    m_cbSPA->SetValue(m_cycloneParams.showSPA);
    m_cbATL->SetValue(m_cycloneParams.showATL);
    m_cbNIO->SetValue(m_cycloneParams.showNIO);
    m_cbSHE->SetValue(m_cycloneParams.showSHE);

    m_cbAllTimesCyclones->SetValue(m_cycloneParams.allTimesCyclones);
}

void ClimatologyConfigDialog::SyncCyclonesTabToModel()
{
    m_cycloneParams.enabled   = m_cbCyclonesEnable->GetValue();
    m_cycloneParams.centerDay = m_sCycloneCenterDay->GetValue();
    m_cycloneParams.daySpan   = m_sCycloneDaySpan->GetValue();

    m_cycloneParams.minWind     = (double)m_sMinWind->GetValue();
    m_cycloneParams.maxPressure = (double)m_sMaxPressure->GetValue();

    m_cycloneParams.showElNino       = m_cbElNino->GetValue();
    m_cycloneParams.showLaNina       = m_cbLaNina->GetValue();
    m_cycloneParams.showNeutral      = m_cbNeutral->GetValue();
    m_cycloneParams.showNotAvailable = m_cbUnknownENSO->GetValue();

    m_cycloneParams.showEPA = m_cbEPA->GetValue();
    m_cycloneParams.showWPA = m_cbWPA->GetValue();
    m_cycloneParams.showSPA = m_cbSPA->GetValue();
    m_cycloneParams.showATL = m_cbATL->GetValue();
    m_cycloneParams.showNIO = m_cbNIO->GetValue();
    m_cycloneParams.showSHE = m_cbSHE->GetValue();

    m_cycloneParams.allTimesCyclones = m_cbAllTimesCyclones->GetValue();
}

// ---------------------------------------------------------------------------
// CycloneFilterParams builder (runtime filter)
// ---------------------------------------------------------------------------

int ClimatologyConfigDialog::GetCycloneStateMask() const
{
    int mask = 0;
    if (m_cbCycloneTropical && m_cbCycloneTropical->GetValue())
        mask |= 1 << 0;
    if (m_cbCycloneSubTropical && m_cbCycloneSubTropical->GetValue())
        mask |= 1 << 1;
    if (m_cbCycloneExtraTropical && m_cbCycloneExtraTropical->GetValue())
        mask |= 1 << 2;
    if (m_cbCycloneWave && m_cbCycloneWave->GetValue())
        mask |= 1 << 3;
    if (m_cbCycloneRemanent && m_cbCycloneRemanent->GetValue())
        mask |= 1 << 4;
    if (m_cbCycloneUnknown && m_cbCycloneUnknown->GetValue())
        mask |= 1 << 5;
    return mask;
}

CycloneFilterParams ClimatologyConfigDialog::GetCycloneFilterParams() const
{
    CycloneFilterParams p;

    p.statemask = GetCycloneStateMask();

    p.minwindspeed = m_sMinWind->GetValue();
    p.maxpressure  = m_sMaxPressure->GetValue();

    // Date range (optional; if you wire date pickers to filter)
    p.startDate = m_dpStart ? m_dpStart->GetValue() : wxDateTime();
    p.endDate   = m_dpEnd   ? m_dpEnd->GetValue()   : wxDateTime();

    p.allowElNino       = m_cbElNino->GetValue();
    p.allowLaNina       = m_cbLaNina->GetValue();
    p.allowNeutral      = m_cbNeutral->GetValue();
    p.allowNotAvailable = m_cbUnknownENSO->GetValue();

    return p;
}

// ---------------------------------------------------------------------------
// Event handlers
// ---------------------------------------------------------------------------

void ClimatologyConfigDialog::OnPageChanged(wxNotebookEvent& event)
{
    if (event.GetSelection() == 3 && m_htmlInformation) {
        m_htmlInformation->LoadFile(ClimatologyDataDirectory() + _("ClimatologyInformation.html"));
    }
    event.Skip();
}

void ClimatologyConfigDialog::OnUpdate()
{
    SyncStandardTabToModel();
    SyncWindAtlasTabToModel();
    SyncCyclonesTabToModel();

    // Trigger redraw via parent
    if (m_parent)
        m_parent->RefreshRedraw();

    m_refreshTimer.Start(200, true);
}

void ClimatologyConfigDialog::OnUpdateOverlayConfig(wxCommandEvent& event)
{
    OnUpdate();
}

void ClimatologyConfigDialog::OnUpdateCyclones(wxCommandEvent& event)
{
    SyncCyclonesTabToModel();
    if (m_parent)
        m_parent->RefreshRedraw();
}

void ClimatologyConfigDialog::OnCycloneFilterChanged(wxCommandEvent& event)
{
    if (!m_parent)
        return;

    CycloneFilterParams params = GetCycloneFilterParams();
    // Assuming ClimatologyDialog forwards this to ClimatologyOverlayFactory
    m_parent->SetCycloneFilter(params);
    m_parent->RefreshRedraw();
}

void ClimatologyConfigDialog::OnPaintKey(wxPaintEvent& event)
{
    wxWindow* window = dynamic_cast<wxWindow*>(event.GetEventObject());
    if (!window)
        return;

    wxPaintDC dc(window);

    double value = 0.0;
    window->GetName().ToDouble(&value);

    wxColour c = ClimatologyOverlayFactory::GetGraphicColor(OVERLAY_WIND, value);
    dc.SetBackground(c);
    dc.Clear();
}

void ClimatologyConfigDialog::OnEnabled(wxCommandEvent& event)
{
    Save();
    if (m_parent)
        m_parent->RefreshRedraw();
    EndModal(wxID_OK);
}

void ClimatologyConfigDialog::OnAboutAuthor(wxCommandEvent& event)
{
    wxLaunchDefaultBrowser(_("https://opencpn.org"));
}

void ClimatologyConfigDialog::OnRefreshTimer(wxTimerEvent& event)
{
    if (m_parent)
        m_parent->RefreshRedraw();
}
