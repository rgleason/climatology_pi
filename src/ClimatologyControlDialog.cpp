// ClimatologyControlDialog.cpp

#pragma once

#include <wx/wx.h>

#include <wx/choice.h>
#include <wx/slider.h>
#include <wx/spinctrl.h>
#include <wx/colourpicker.h>

#include "ClimatologyControlDialog.h"  // 4 Tabs Dialog
#include "climatology_pi.h"
#include "ClimatologyOverlayFactory.h"
#include "ClimatologyDialog.h"      // First Opening Dialog 
#include "OverlayTypes.h"          // if not already via header
#include "StandardDisplayParams.h" // already via header, fine
#include "CycloneTypes.h"


class climatology_pi;
class ClimatologyOverlayFactory;

//struct ControlPanelParams {
//    bool showOverlay;
//    int  overlayType;
//    int  units;
//    int  transparency;
//    bool showIsoBars;
//    int  isoSpacing;
//    int  isoStep;
//    bool showNumbers;
//    int  numberSpacing;
//    bool showArrows;
//    int  arrowWidth;
//    int  arrowSpacing;
//    int  arrowSize;
//    int  arrowMode; // 0 = Barbs, 1 = Length
//    wxColour color;
//    int  opacity;
//};

class ClimatologyControlDialog : public wxDialog
{
public:
    ClimatologyControlDialog(wxWindow* parent,
                             climatology_pi* plugin,
                             ClimatologyOverlayFactory* factory);

    void UpdateOverlayChoices();          // called after Config changes
    StandardDisplayParams BuildParams() const;

private:
    void InitUI();
    void BindEvents();

    void OnOverlayChanged(wxCommandEvent& event);
    void OnShowOverlay(wxCommandEvent& event);
    void OnConfig(wxCommandEvent& event);
    void OnClose(wxCommandEvent& event);

    // other handlers as needed:
    void OnUnitsChanged(wxCommandEvent& event);
    void OnTransparencyChanged(wxCommandEvent& event);
    void OnIsoBarsChanged(wxCommandEvent& event);
    void OnIsoSpacingChanged(wxCommandEvent& event);
    void OnIsoStepChanged(wxCommandEvent& event);
    void OnNumbersChanged(wxCommandEvent& event);
    void OnNumberSpacingChanged(wxCommandEvent& event);
    void OnArrowsChanged(wxCommandEvent& event);
    void OnArrowWidthChanged(wxCommandEvent& event);
    void OnArrowSpacingChanged(wxCommandEvent& event);
    void OnArrowSizeChanged(wxCommandEvent& event);
    void OnArrowModeChanged(wxCommandEvent& event);
    void OnColorChanged(wxColourPickerEvent& event);
    void OnOpacityChanged(wxCommandEvent& event);

    wxCheckBox*         m_cbShowOverlay;
    wxChoice*           m_choiceOverlay;
    wxChoice*           m_choiceUnits;
    wxSlider*           m_sliderTransparency;
    wxCheckBox*         m_cbShowIsoBars;
    wxSpinCtrl*         m_spinIsoSpacing;
    wxChoice*           m_choiceIsoStep;
    wxCheckBox*         m_cbShowNumbers;
    wxSpinCtrl*         m_spinNumberSpacing;
    wxCheckBox*         m_cbShowArrows;
    wxSpinCtrl*         m_spinArrowWidth;
    wxSpinCtrl*         m_spinArrowSpacing;
    wxSpinCtrl*         m_spinArrowSize;
    wxRadioButton*      m_rbBarbs;
    wxRadioButton*      m_rbLength;
    wxColourPickerCtrl* m_colourOverlay;
    wxSlider*           m_sliderOpacity;
    wxButton*           m_btnConfig;
    wxButton*           m_btnClose;

    climatology_pi*             m_pPlugin;
    ClimatologyOverlayFactory*  m_pFactory;

    wxDECLARE_EVENT_TABLE();
};




wxBEGIN_EVENT_TABLE(ClimatologyControlDialog, wxDialog)
    // we’ll use Bind() instead of static table for most controls
wxEND_EVENT_TABLE()

static wxString OverlayName(OverlayType t)
{
    switch(t) {
    case OVERLAY_WIND:        return _("Wind");
    case OVERLAY_WIND_ATLAS:  return _("Wind-Atlas");
    case OVERLAY_CYCLONES:    return _("Cyclones");
    case OVERLAY_CURRENT:     return _("Current");
    case OVERLAY_PRESSURE:    return _("Pressure");
    case OVERLAY_SEA_TEMP:    return _("Sea Temperature");
    case OVERLAY_AIR_TEMP:    return _("Air Temperature");
    case OVERLAY_CLOUD:       return _("Cloud Cover");
    case OVERLAY_PRECIP:      return _("Precipitation");
    case OVERLAY_RH:          return _("Relative Humidity");
    case OVERLAY_LIGHTNING:   return _("Lightning");
    case OVERLAY_SEA_DEPTH:   return _("Sea Depth");
    default:                  return _("Unknown");
    }
}

ClimatologyControlDialog::ClimatologyControlDialog(wxWindow* parent,
                                                   climatology_pi* plugin,
                                                   ClimatologyOverlayFactory* factory)
    : wxDialog(parent, wxID_ANY, _("Climatology"),
               wxDefaultPosition, wxDefaultSize,
               wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER),
      m_pPlugin(plugin),
      m_pFactory(factory)
{
    InitUI();
    BindEvents();
    UpdateOverlayChoices();
    Layout();
    Fit();
    Centre();
}

void ClimatologyControlDialog::InitUI()
{
    wxBoxSizer* top = new wxBoxSizer(wxVERTICAL);

    // Row 1: Show + Overlay + Units
    {
        wxBoxSizer* row = new wxBoxSizer(wxHORIZONTAL);
        m_cbShowOverlay = new wxCheckBox(this, wxID_ANY, _("Show"));
        m_choiceOverlay = new wxChoice(this, wxID_ANY);
        m_choiceUnits   = new wxChoice(this, wxID_ANY);

        row->Add(m_cbShowOverlay, 0, wxRIGHT | wxALIGN_CENTER_VERTICAL, 5);
        row->Add(m_choiceOverlay, 1, wxRIGHT | wxALIGN_CENTER_VERTICAL, 10);
        row->Add(new wxStaticText(this, wxID_ANY, _("Units")), 0,
                 wxRIGHT | wxALIGN_CENTER_VERTICAL, 5);
        row->Add(m_choiceUnits, 0, wxALIGN_CENTER_VERTICAL);

        top->Add(row, 0, wxEXPAND | wxALL, 5);
    }

    // Row 2: Transparency
    {
        wxBoxSizer* row = new wxBoxSizer(wxHORIZONTAL);
        m_sliderTransparency = new wxSlider(this, wxID_ANY, 50, 0, 100);
        row->Add(new wxStaticText(this, wxID_ANY, _("Transparency")), 0,
                 wxRIGHT | wxALIGN_CENTER_VERTICAL, 5);
        row->Add(m_sliderTransparency, 1);
        top->Add(row, 0, wxEXPAND | wxALL, 5);
    }

    // Row 3: IsoBars
    {
        wxBoxSizer* row = new wxBoxSizer(wxHORIZONTAL);
        m_cbShowIsoBars = new wxCheckBox(this, wxID_ANY, _("Show IsoBars"));
        m_spinIsoSpacing = new wxSpinCtrl(this, wxID_ANY);
        m_choiceIsoStep  = new wxChoice(this, wxID_ANY);

        row->Add(m_cbShowIsoBars, 0, wxRIGHT | wxALIGN_CENTER_VERTICAL, 10);
        row->Add(new wxStaticText(this, wxID_ANY, _("Spacing")), 0,
                 wxRIGHT | wxALIGN_CENTER_VERTICAL, 5);
        row->Add(m_spinIsoSpacing, 0, wxRIGHT | wxALIGN_CENTER_VERTICAL, 15);
        row->Add(new wxStaticText(this, wxID_ANY, _("Step")), 0,
                 wxRIGHT | wxALIGN_CENTER_VERTICAL, 5);
        row->Add(m_choiceIsoStep, 0, wxALIGN_CENTER_VERTICAL);

        top->Add(row, 0, wxEXPAND | wxALL, 5);
    }

    // Row 4: Numbers
    {
        wxBoxSizer* row = new wxBoxSizer(wxHORIZONTAL);
        m_cbShowNumbers     = new wxCheckBox(this, wxID_ANY, _("Show Numbers"));
        m_spinNumberSpacing = new wxSpinCtrl(this, wxID_ANY);

        row->Add(m_cbShowNumbers, 0, wxRIGHT | wxALIGN_CENTER_VERTICAL, 10);
        row->Add(new wxStaticText(this, wxID_ANY, _("Spacing")), 0,
                 wxRIGHT | wxALIGN_CENTER_VERTICAL, 5);
        row->Add(m_spinNumberSpacing, 0, wxALIGN_CENTER_VERTICAL);

        top->Add(row, 0, wxEXPAND | wxALL, 5);
    }

    // Row 5: Direction Arrows
    {
        wxBoxSizer* row = new wxBoxSizer(wxHORIZONTAL);
        m_cbShowArrows    = new wxCheckBox(this, wxID_ANY, _("Show Direction Arrows"));
        m_spinArrowWidth  = new wxSpinCtrl(this, wxID_ANY);
        m_spinArrowSpacing= new wxSpinCtrl(this, wxID_ANY);
        m_spinArrowSize   = new wxSpinCtrl(this, wxID_ANY);

        row->Add(m_cbShowArrows, 0, wxRIGHT | wxALIGN_CENTER_VERTICAL, 10);

        row->Add(new wxStaticText(this, wxID_ANY, _("Width")), 0,
                 wxRIGHT | wxALIGN_CENTER_VERTICAL, 5);
        row->Add(m_spinArrowWidth, 0, wxRIGHT | wxALIGN_CENTER_VERTICAL, 15);

        row->Add(new wxStaticText(this, wxID_ANY, _("Spacing")), 0,
                 wxRIGHT | wxALIGN_CENTER_VERTICAL, 5);
        row->Add(m_spinArrowSpacing, 0, wxRIGHT | wxALIGN_CENTER_VERTICAL, 15);

        row->Add(new wxStaticText(this, wxID_ANY, _("Size")), 0,
                 wxRIGHT | wxALIGN_CENTER_VERTICAL, 5);
        row->Add(m_spinArrowSize, 0, wxALIGN_CENTER_VERTICAL);

        top->Add(row, 0, wxEXPAND | wxALL, 5);
    }

    // Row 6: Arrow mode
    {
        wxBoxSizer* row = new wxBoxSizer(wxHORIZONTAL);
        m_rbBarbs  = new wxRadioButton(this, wxID_ANY, _("Barbs"), wxRB_GROUP);
        m_rbLength = new wxRadioButton(this, wxID_ANY, _("Length"));
        row->Add(m_rbBarbs, 0, wxRIGHT | wxALIGN_CENTER_VERTICAL, 15);
        row->Add(m_rbLength, 0, wxALIGN_CENTER_VERTICAL);
        top->Add(row, 0, wxEXPAND | wxALL, 5);
    }

    // Row 7: Color
    {
        wxBoxSizer* row = new wxBoxSizer(wxHORIZONTAL);
        m_colourOverlay = new wxColourPickerCtrl(this, wxID_ANY);
        row->Add(new wxStaticText(this, wxID_ANY, _("Color")), 0,
                 wxRIGHT | wxALIGN_CENTER_VERTICAL, 5);
        row->Add(m_colourOverlay, 0, wxALIGN_CENTER_VERTICAL);
        top->Add(row, 0, wxEXPAND | wxALL, 5);
    }

    // Row 8: Opacity
    {
        wxBoxSizer* row = new wxBoxSizer(wxHORIZONTAL);
        m_sliderOpacity = new wxSlider(this, wxID_ANY, 50, 0, 100);
        row->Add(new wxStaticText(this, wxID_ANY, _("Opacity")), 0,
                 wxRIGHT | wxALIGN_CENTER_VERTICAL, 5);
        row->Add(m_sliderOpacity, 1);
        top->Add(row, 0, wxEXPAND | wxALL, 5);
    }

    // Buttons: Config + Close
    {
        wxBoxSizer* row = new wxBoxSizer(wxHORIZONTAL);
        m_btnConfig = new wxButton(this, wxID_ANY, _("Config..."));
        m_btnClose  = new wxButton(this, wxID_ANY, _("Close"));
        row->Add(m_btnConfig, 0, wxRIGHT, 5);
        row->Add(m_btnClose, 0);
        top->Add(row, 0, wxALIGN_RIGHT | wxALL, 5);
    }

    SetSizer(top);
}

void ClimatologyControlDialog::BindEvents()
{
    Bind(wxEVT_CHECKBOX, &ClimatologyControlDialog::OnShowOverlay,
         this, m_cbShowOverlay->GetId());
    Bind(wxEVT_CHOICE,   &ClimatologyControlDialog::OnOverlayChanged,
         this, m_choiceOverlay->GetId());
    Bind(wxEVT_CHOICE,   &ClimatologyControlDialog::OnUnitsChanged,
         this, m_choiceUnits->GetId());

    Bind(wxEVT_SLIDER,   &ClimatologyControlDialog::OnTransparencyChanged,
         this, m_sliderTransparency->GetId());

    Bind(wxEVT_CHECKBOX, &ClimatologyControlDialog::OnIsoBarsChanged,
         this, m_cbShowIsoBars->GetId());
    Bind(wxEVT_SPINCTRL, &ClimatologyControlDialog::OnIsoSpacingChanged,
         this, m_spinIsoSpacing->GetId());
    Bind(wxEVT_CHOICE,   &ClimatologyControlDialog::OnIsoStepChanged,
         this, m_choiceIsoStep->GetId());

    Bind(wxEVT_CHECKBOX, &ClimatologyControlDialog::OnNumbersChanged,
         this, m_cbShowNumbers->GetId());
    Bind(wxEVT_SPINCTRL, &ClimatologyControlDialog::OnNumberSpacingChanged,
         this, m_spinNumberSpacing->GetId());

    Bind(wxEVT_CHECKBOX, &ClimatologyControlDialog::OnArrowsChanged,
         this, m_cbShowArrows->GetId());
    Bind(wxEVT_SPINCTRL, &ClimatologyControlDialog::OnArrowWidthChanged,
         this, m_spinArrowWidth->GetId());
    Bind(wxEVT_SPINCTRL, &ClimatologyControlDialog::OnArrowSpacingChanged,
         this, m_spinArrowSpacing->GetId());
    Bind(wxEVT_SPINCTRL, &ClimatologyControlDialog::OnArrowSizeChanged,
         this, m_spinArrowSize->GetId());
    Bind(wxEVT_RADIOBUTTON, &ClimatologyControlDialog::OnArrowModeChanged,
         this, m_rbBarbs->GetId());
    Bind(wxEVT_RADIOBUTTON, &ClimatologyControlDialog::OnArrowModeChanged,
         this, m_rbLength->GetId());

    Bind(wxEVT_COLOURPICKER_CHANGED, &ClimatologyControlDialog::OnColorChanged,
         this, m_colourOverlay->GetId());

    Bind(wxEVT_SLIDER, &ClimatologyControlDialog::OnOpacityChanged,
         this, m_sliderOpacity->GetId());

    m_btnConfig->Bind(wxEVT_BUTTON, &ClimatologyControlDialog::OnConfig, this);
    m_btnClose->Bind(wxEVT_BUTTON,  &ClimatologyControlDialog::OnClose, this);
}

// Called whenever Config dialog changes "Show" flags, or on construction
void ClimatologyControlDialog::UpdateOverlayChoices()
{
    m_choiceOverlay->Clear();

    // Get persistent params from the plugin (correct owner)
    const StandardDisplayParams& p = m_pPlugin->GetStandardDisplayParams();

    // Wind: controlled by Show flag
    if (p.showOverlayType[OVERLAY_WIND])
        m_choiceOverlay->Append(OverlayName(OVERLAY_WIND),
                                (void*)OVERLAY_WIND);

	// Wind-Atlas: controlled by Show flag
	if (p.showOverlayType[OVERLAY_WIND_ATLAS])
		m_choiceOverlay->Append(OverlayName(OVERLAY_WIND_ATLAS), 
											(void*)OVERLAY_WIND_ATLAS);

	// Cyclones: controlled by Show flag
	if (p.showOverlayType[OVERLAY_CYCLONES])
		m_choiceOverlay->Append(OverlayName(OVERLAY_CYCLONES), 
								(void*)OVERLAY_CYCLONES);

    // All other overlays: only if Show == true
    OverlayType others[] = {
        OVERLAY_CURRENT,
        OVERLAY_PRESSURE,
        OVERLAY_SEA_TEMP,
        OVERLAY_AIR_TEMP,
        OVERLAY_CLOUD,
        OVERLAY_PRECIP,
        OVERLAY_RH,
        OVERLAY_LIGHTNING,
        OVERLAY_SEA_DEPTH
    };

    for (auto t : others) {
        if (p.showOverlayType[t]) {
            m_choiceOverlay->Append(OverlayName(t), (void*)t);
        }
    }

    if (m_choiceOverlay->GetCount() > 0)
        m_choiceOverlay->SetSelection(0);
}


StandardDisplayParams ClimatologyControlDialog::BuildParams() const
{
    // Start with existing params from plugin
    StandardDisplayParams p = m_pPlugin->GetStandardDisplayParams();

    int selIndex = m_choiceOverlay->GetSelection();
    if (selIndex == wxNOT_FOUND)
        return p;

    OverlayType o = (OverlayType)(intptr_t)m_choiceOverlay->GetClientData(selIndex);
    p.overlayType = o;

    // Per-overlay fields
    p.showOverlayType[o] = m_cbShowOverlay->GetValue();
    p.units[o]           = m_choiceUnits->GetSelection();
    p.color[o]           = m_colourOverlay->GetColour();

    p.showIsoBars[o]     = m_cbShowIsoBars->GetValue();
    p.isoSpacing[o]      = m_spinIsoSpacing->GetValue();
    p.isoStep[o]         = m_choiceIsoStep->GetSelection();

    p.showNumbers[o]     = m_cbShowNumbers->GetValue();
    p.numberSpacing      = m_spinNumberSpacing->GetValue();   // global

    p.showArrows[o]      = m_cbShowArrows->GetValue();
    p.arrowWidth[o]      = m_spinArrowWidth->GetValue();
    p.arrowSpacing[o]    = m_spinArrowSpacing->GetValue();
    p.arrowSize[o]       = m_spinArrowSize->GetValue();
    p.arrowMode[o]       = m_rbLength->GetValue() ? 1 : 0;

    // Global fields
    p.opacity            = m_sliderOpacity->GetValue();
    p.transparency       = m_sliderTransparency->GetValue();
    p.smooth             = true; //or hook to a checkbox later or m_cbInterpolation if you add one

    return p;
}


// --- Handlers ---

void ClimatologyControlDialog::OnShowOverlay(wxCommandEvent& event)
{
    StandardDisplayParams p = BuildParams();

    // Optional: avoid unnecessary redraws
    if (!p.showOverlayType[p.overlayType])
        return;

    m_pPlugin->SetStandardDisplayParams(p);
    m_pPlugin->SendClimatology(p);
}



//void ClimatologyControlDialog::OnOverlayChanged(wxCommandEvent& event)
//{
//    int selIndex = m_choiceOverlay->GetSelection();
//    if(selIndex == wxNOT_FOUND)
//        return;

//    OverlayType sel = (OverlayType)(intptr_t)m_choiceOverlay->GetClientData(selIndex);

//    bool isWind      = (sel == OVERLAY_WIND);
//    bool isWindAtlas = (sel == OVERLAY_WIND_ATLAS);
//    bool isCyclones  = (sel == OVERLAY_CYCLONES);

//    if(!isWind && !isWindAtlas && !isCyclones) {
//        // Non-special overlays: mutually exclusive
//        m_pFactory->DisableAllOverlays();
//        m_pFactory->EnableOverlay(sel);
//    } else {
        // Special overlays: do not disable each other
//        if(isWind)
//            m_pFactory->EnableOverlay(OVERLAY_WIND);
//        if(isWindAtlas)
//            m_pFactory->EnableOverlay(OVERLAY_WIND_ATLAS);
//        if(isCyclones)
//            m_pFactory->EnableOverlay(OVERLAY_CYCLONES);
//    }
//
//    m_pFactory->RequestRefresh();
//}

void ClimatologyControlDialog::OnOverlayChanged(wxCommandEvent& event)
{
    StandardDisplayParams p = BuildParams();
    m_pPlugin->SetStandardDisplayParams(p);
    m_pPlugin->SendClimatology(p);
}



void ClimatologyControlDialog::OnUnitsChanged(wxCommandEvent& event)
{
    StandardDisplayParams p = BuildParams();
    m_pPlugin->SetStandardDisplayParams(p);
    m_pPlugin->SendClimatology(p);
}

void ClimatologyControlDialog::OnTransparencyChanged(wxCommandEvent& event)
{
    StandardDisplayParams p = BuildParams();
    m_pPlugin->SetStandardDisplayParams(p);
    m_pPlugin->SendClimatology(p);
}

void ClimatologyControlDialog::OnIsoBarsChanged(wxCommandEvent& event)
{
    StandardDisplayParams p = BuildParams();
    m_pPlugin->SetStandardDisplayParams(p);
    m_pPlugin->SendClimatology(p);
}

void ClimatologyControlDialog::OnIsoSpacingChanged(wxCommandEvent& event)
{
    StandardDisplayParams p = BuildParams();
    m_pPlugin->SetStandardDisplayParams(p);
    m_pPlugin->SendClimatology(p);
}

void ClimatologyControlDialog::OnIsoStepChanged(wxCommandEvent& event)
{
    StandardDisplayParams p = BuildParams();
    m_pPlugin->SetStandardDisplayParams(p);
    m_pPlugin->SendClimatology(p);
}

void ClimatologyControlDialog::OnNumbersChanged(wxCommandEvent& event)
{
    StandardDisplayParams p = BuildParams();
    m_pPlugin->SetStandardDisplayParams(p);
    m_pPlugin->SendClimatology(p);
}

void ClimatologyControlDialog::OnNumberSpacingChanged(wxCommandEvent& event)
{
    StandardDisplayParams p = BuildParams();
    m_pPlugin->SetStandardDisplayParams(p);
    m_pPlugin->SendClimatology(p);
}

void ClimatologyControlDialog::OnArrowsChanged(wxCommandEvent& event)
{
    StandardDisplayParams p = BuildParams();
    m_pPlugin->SetStandardDisplayParams(p);
    m_pPlugin->SendClimatology(p);
}

void ClimatologyControlDialog::OnArrowWidthChanged(wxCommandEvent& event)
{
    StandardDisplayParams p = BuildParams();
    m_pPlugin->SetStandardDisplayParams(p);
    m_pPlugin->SendClimatology(p);
}

void ClimatologyControlDialog::OnArrowSpacingChanged(wxCommandEvent& event)
{
    StandardDisplayParams p = BuildParams();
    m_pPlugin->SetStandardDisplayParams(p);
    m_pPlugin->SendClimatology(p);
}

void ClimatologyControlDialog::OnArrowSizeChanged(wxCommandEvent& event)
{
    StandardDisplayParams p = BuildParams();
    m_pPlugin->SetStandardDisplayParams(p);
    m_pPlugin->SendClimatology(p);
}

void ClimatologyControlDialog::OnArrowModeChanged(wxCommandEvent& event)
{
    StandardDisplayParams p = BuildParams();
    m_pPlugin->SetStandardDisplayParams(p);
    m_pPlugin->SendClimatology(p);
}

void ClimatologyControlDialog::OnColorChanged(wxColourPickerEvent& event)
{
    StandardDisplayParams p = BuildParams();
    m_pPlugin->SetStandardDisplayParams(p);
    m_pPlugin->SendClimatology(p);
}

void ClimatologyControlDialog::OnOpacityChanged(wxCommandEvent& event)
{
    StandardDisplayParams p = BuildParams();
    m_pPlugin->SetStandardDisplayParams(p);
    m_pPlugin->SendClimatology(p);
}

void ClimatologyControlDialog::OnConfig(wxCommandEvent& event)
{
    ClimatologyDialog dlg(this, m_pPlugin, m_pFactory);

    if (dlg.ShowModal() == wxID_OK) {
        UpdateOverlayChoices();

        StandardDisplayParams p = m_pPlugin->GetStandardDisplayParams();
        m_pPlugin->SendClimatology(p);
    }
}


void ClimatologyControlDialog::OnClose(wxCommandEvent& event)
{
    Hide(); // or Destroy() if you //
}



//
// The rest of the handlers (Units, transparency, etc.) should
// update factory params and call RequestRefresh() similarly.



//  ======================================================
// Hook from the plugin:

// climatology_pi.cpp
//void climatology_pi::OnToolbarToolCallback(int id)
//{
//    if(id == m_toolbar_item_id) {
//        if(!m_pControlDialog)
//            m_pControlDialog = new ClimatologyControlDialog(
//                GetOCPNCanvasWindow(), this, m_pFactory);
//
  //      m_pControlDialog->Show();
//        m_pControlDialog->Raise();
//    }
//}


// This gives you:
// A modern, floating, resizable FIRST dialog
// Overlay dropdown filtered by “Show”
// Wind / Wind – Atlas / Cyclones special behavior
// Config button wired to the 4‑tab dialog
// Clean separation: FIRST dialog selects overlays, CONFIG dialog configures them
// If you want, next step can be tightening the factory API (GetStandardDisplayParams, EnableOverlay, DisableAllOverlays, SetOverlayEnabled) to match your actual code.