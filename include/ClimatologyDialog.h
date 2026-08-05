/******************************************************************************
 * ClimatologyDialog.cpp  —  Modern hand-written first dialog
 ******************************************************************************/

#include <wx/statline.h>
#include <wx/wxprec.h>
#ifndef WX_PRECOMP
    #include <wx/wx.h>
#endif
#include <wx/progdlg.h>


#include "ClimatologyDialog.h"
#include "ClimatologyConfigDialog.h"
#include "ClimatologyEnums.h"
#include "ClimatologyRenderParams.h"
#include "StandardDisplayParams.h"
#include "CycloneFilterParams.h"
#include "CycloneUtils.h"
#include "CycloneStructs.h"


#include "ocpn_plugin.h"   // for DimeWindow

class ClimatologyOverlayFactory;


wxBEGIN_EVENT_TABLE(ClimatologyDialog, wxDialog)
    EVT_CLOSE(ClimatologyDialog::OnClose)
wxEND_EVENT_TABLE()

// ---------------------------------------------------------------------------
// Constructor
// ---------------------------------------------------------------------------
ClimatologyDialog::ClimatologyDialog(wxWindow*                  parent,
                                     ClimatologyOverlayFactory* factory,
                                     wxWindowID                 id,
                                     const wxString&            title,
                                     const wxPoint&             pos,
                                     const wxSize&              size)
    : wxDialog(parent, id, title, pos, size,
               wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER)
    , m_factory(factory)
{
    DimeWindow(this);

    // -----------------------------------------------------------------------
    // Initialize StandardDisplayParams (modern)
    // -----------------------------------------------------------------------
    m_displayParams.overlayType   = OVERLAY_WIND;
    m_displayParams.alpha         = 0.0;
    m_displayParams.smooth        = true;
    m_displayParams.numberSpacing = 20.0;
    m_displayParams.mode          = DisplayMode::SingleScaled;

    for (int i = 0; i < NUM_OVERLAYS; ++i) {
        m_displayParams.alpha[i]           = 1.0;
        m_displayParams.units[i]           = 0;
        m_displayParams.showOverlayType[i] = false;
        m_displayParams.showArrows[i]      = false;
        m_displayParams.showNumbers[i]     = false;
        m_displayParams.arrowWidth[i]      = 1;
        m_displayParams.arrowSpacing[i]    = 20;
        m_displayParams.arrowSize[i]       = 12;
        m_displayParams.arrowMode[i]       = 0;
        m_displayParams.isoSpacing[i]      = 0;
        m_displayParams.isoStep[i]         = 0;
        m_displayParams.calibrationFactor[i] = 1.0;
        m_displayParams.calibrationOffset[i] = 0.0;
    }

    m_displayParams.windAtlas.enabled = false;
    m_displayParams.cyclone.enabled   = false;

    // -----------------------------------------------------------------------
    // Initialize ClimatologyRenderParams (modern)
    // -----------------------------------------------------------------------
    m_renderParams.vp              = nullptr;
    m_renderParams.dc              = nullptr;
    m_renderParams.useGL           = true;
    m_renderParams.useShaders      = true;
    m_renderParams.dcContextValid  = false;
    m_renderParams.glContextValid  = false;

    m_renderParams.overlayType     = OVERLAY_WIND;
    m_renderParams.overlayEnabled  = false;
    m_renderParams.overlayMap      = true;
    m_renderParams.overlayInterpolation = true;

    m_renderParams.currentMonth      = 0;
    m_renderParams.nextMonth         = 0;
    m_renderParams.monthInterpolation = 1.0;

    m_renderParams.showArrows   = false;
    m_renderParams.showNumbers  = false;
    m_renderParams.showIsoBars  = false;
    m_renderParams.smoothing    = true;

    m_renderParams.showCyclones = false;

    m_renderParams.showEPA = true;
    m_renderParams.showWPA = true;
    m_renderParams.showSPA = true;
    m_renderParams.showATL = true;
    m_renderParams.showNIO = true;
    m_renderParams.showSHE = true;

    m_renderParams.showElNino  = true;
    m_renderParams.showLaNina  = true;
    m_renderParams.showNeutral = true;

    // -----------------------------------------------------------------------
    // Initialize CycloneFilterParams (modern)
    // -----------------------------------------------------------------------
    m_cycloneFilter.stateMask         = 0xFFFFFFFF;
    m_cycloneFilter.minWind           = 0.0;
    m_cycloneFilter.maxPressure       = 2000.0;
    m_cycloneFilter.allowElNino       = true;
    m_cycloneFilter.allowLaNina       = true;
    m_cycloneFilter.allowNeutral      = true;
    m_cycloneFilter.allowNotAvailable = true;

    m_cycloneFilter.startDate = wxDateTime(1, wxDateTime::Jan, 1850);
    m_cycloneFilter.endDate   = wxDateTime(31, wxDateTime::Dec, 2100);

    // -----------------------------------------------------------------------
    // Build UI
    // -----------------------------------------------------------------------
    wxBoxSizer* topSizer = new wxBoxSizer(wxVERTICAL);

    topSizer->Add(BuildTimelineBar(), 0, wxEXPAND | wxALL, 8);
    topSizer->Add(new wxStaticLine(this), 0, wxEXPAND | wxLEFT | wxRIGHT, 8);
    topSizer->Add(BuildOverlayList(), 1, wxEXPAND | wxALL, 8);

    wxStdDialogButtonSizer* btnSizer = new wxStdDialogButtonSizer();
    wxButton* closeBtn = new wxButton(this, wxID_CLOSE);
    btnSizer->AddButton(closeBtn);
    btnSizer->Realize();

    topSizer->Add(btnSizer, 0, wxALIGN_RIGHT | wxALL, 8);

    SetSizerAndFit(topSizer);
    SetMinSize(wxSize(420, 480));

    closeBtn->Bind(wxEVT_BUTTON, [this](wxCommandEvent&){ Hide(); });

    // -----------------------------------------------------------------------
    // Push initial params to factory
    // -----------------------------------------------------------------------
    if (m_factory) {
        m_factory->SetParams(m_displayParams);
        m_factory->SetCycloneFilter(m_cycloneFilter);
    }
}

// ---------------------------------------------------------------------------
// Build timeline bar
// ---------------------------------------------------------------------------
wxSizer* ClimatologyDialog::BuildTimelineBar()
{
    wxBoxSizer* s = new wxBoxSizer(wxHORIZONTAL);

    m_monthChoice = new wxChoice(this, wxID_ANY);
    for (int i = 0; i < 12; ++i)
        m_monthChoice->Append(wxDateTime::GetMonthName((wxDateTime::Month)i));
    m_monthChoice->SetSelection(0);

    m_daySpin = new wxSpinCtrl(this, wxID_ANY, wxEmptyString,
                               wxDefaultPosition, wxDefaultSize,
                               wxSP_ARROW_KEYS, 1, 31, 15);

    m_allCheck  = new wxCheckBox(this, wxID_ANY, _("All"));
    m_nowButton = new wxButton(this, wxID_ANY, _("Now"));

    s->Add(new wxStaticText(this, wxID_ANY, _("Month")), 0, wxALL | wxALIGN_CENTER_VERTICAL, 5);
    s->Add(m_monthChoice, 0, wxALL | wxALIGN_CENTER_VERTICAL, 5);
    s->Add(new wxStaticText(this, wxID_ANY, _("Day")), 0, wxALL | wxALIGN_CENTER_VERTICAL, 5);
    s->Add(m_daySpin, 0, wxALL | wxALIGN_CENTER_VERTICAL, 5);
    s->Add(m_allCheck, 0, wxALL | wxALIGN_CENTER_VERTICAL, 5);
    s->Add(m_nowButton, 0, wxALL | wxALIGN_CENTER_VERTICAL, 5);

    m_monthChoice->Bind(wxEVT_CHOICE, &ClimatologyDialog::OnMonthChanged, this);
    m_daySpin->Bind(wxEVT_SPINCTRL, &ClimatologyDialog::OnDayChanged, this);
    m_allCheck->Bind(wxEVT_CHECKBOX, &ClimatologyDialog::OnAllCheckbox, this);
    m_nowButton->Bind(wxEVT_BUTTON, [this](wxCommandEvent&){
        wxDateTime now = wxDateTime::Now();
        m_monthChoice->SetSelection(now.GetMonth());
        m_daySpin->SetValue(now.GetDay());
		m_displayParams.cyclone.currentMonth = now.GetMonth();
		m_displayParams.cyclone.centerDay    = now.GetDay();
        PushParamsToFactoryAndRender();
    });

    return s;
}

// ---------------------------------------------------------------------------
// Build overlay list
// ---------------------------------------------------------------------------
wxSizer* ClimatologyDialog::BuildOverlayList()
{
    wxBoxSizer* s = new wxBoxSizer(wxVERTICAL);

    auto addRow = [&](OverlayType id, const wxString& label, bool hasConfig) {
        wxBoxSizer* row = new wxBoxSizer(wxHORIZONTAL);

        OverlayRowWidgets& w = m_rows[id];

        w.check   = new wxCheckBox(this, wxID_ANY, label);
        w.readout = new wxStaticText(this, wxID_ANY, wxEmptyString);

        row->Add(w.check,   0, wxALL | wxALIGN_CENTER_VERTICAL, 5);
        row->AddStretchSpacer(1);
        row->Add(w.readout, 0, wxALL | wxALIGN_CENTER_VERTICAL, 5);

        if (hasConfig) {
            w.configBtn = new wxButton(this, wxID_ANY, _("Config…"));
            row->Add(w.configBtn, 0, wxALL | wxALIGN_CENTER_VERTICAL, 5);
            w.configBtn->Bind(wxEVT_BUTTON, &ClimatologyDialog::OnCyclonesConfig, this);
        }

        w.check->Bind(wxEVT_CHECKBOX, &ClimatologyDialog::OnOverlayCheck, this);

        s->Add(row, 0, wxEXPAND);
    };

    addRow(OVERLAY_WIND,        _("Wind"),        false);
    addRow(OVERLAY_CURRENT,     _("Current"),     false);
    addRow(OVERLAY_PRESSURE,    _("Pressure"),    false);
    addRow(OVERLAY_SEA_TEMP,    _("Sea Temp"),    false);
    addRow(OVERLAY_AIR_TEMP,    _("Air Temp"),    false);
    addRow(OVERLAY_CLOUD,       _("Cloud Cover"), false);
    addRow(OVERLAY_PRECIP,      _("Precipitation"), false);
    addRow(OVERLAY_RH,          _("Relative Humidity"), false);
    addRow(OVERLAY_LIGHTNING,   _("Lightning"),   false);
    addRow(OVERLAY_SEA_DEPTH,   _("Sea Depth"),   false);
    addRow(OVERLAY_CYCLONES,    _("Cyclones"),    true);

    // Quick ENSO / basin toggles row (optional)
    wxBoxSizer* cycRow = new wxBoxSizer(wxHORIZONTAL);
    m_allTimesCheck = new wxCheckBox(this, wxID_ANY, _("All-times cyclones"));
    cycRow->Add(m_allTimesCheck, 0, wxALL | wxALIGN_CENTER_VERTICAL, 5);

    m_elNinoCheck  = new wxCheckBox(this, wxID_ANY, _("El Niño"));
    m_laNinaCheck  = new wxCheckBox(this, wxID_ANY, _("La Niña"));
    m_neutralCheck = new wxCheckBox(this, wxID_ANY, _("Neutral"));

    cycRow->Add(m_elNinoCheck,  0, wxALL | wxALIGN_CENTER_VERTICAL, 5);
    cycRow->Add(m_laNinaCheck,  0, wxALL | wxALIGN_CENTER_VERTICAL, 5);
    cycRow->Add(m_neutralCheck, 0, wxALL | wxALIGN_CENTER_VERTICAL, 5);

    s->Add(cycRow, 0, wxEXPAND);

    return s;
}

// ---------------------------------------------------------------------------
// Event handlers
// ---------------------------------------------------------------------------
void ClimatologyDialog::OnMonthChanged(wxCommandEvent& evt)
{
    m_displayParams.cyclone.currentMonth = m_monthChoice->GetSelection();
    PushParamsToFactoryAndRender();
}

void ClimatologyDialog::OnDayChanged(wxSpinEvent& evt)
{
	m_displayParams.cyclone.centerDay = m_daySpin->GetValue();
    PushParamsToFactoryAndRender();
}

void ClimatologyDialog::OnAllCheckbox(wxCommandEvent& evt)
{
    bool all = m_allCheck->GetValue();
    for (int i = 0; i < NUM_OVERLAYS; ++i) {
        m_displayParams.showOverlayType[i] = all;
        if (m_rows[i].check)
            m_rows[i].check->SetValue(all);
    }
    PushParamsToFactoryAndRender();
}

void ClimatologyDialog::OnOverlayCheck(wxCommandEvent& evt)
{
    wxCheckBox* cb = dynamic_cast<wxCheckBox*>(evt.GetEventObject());
    if (!cb)
        return;

    bool val = cb->GetValue();

    for (int i = 0; i < NUM_OVERLAYS; ++i) {
        if (m_rows[i].check == cb) {
            m_displayParams.showOverlayType[i] = val;
            break;
        }
    }

    PushParamsToFactoryAndRender();
}

void ClimatologyDialog::OnCyclonesConfig(wxCommandEvent& evt)
{
    // Second 4-tab config dialog; now requires parent + factory
    ClimatologyConfigDialog cfg(this, m_factory);
    cfg.ShowModal();
    PushParamsToFactoryAndRender();
}

void ClimatologyDialog::OnClose(wxCloseEvent& evt)
{
    Hide();
}

// ---------------------------------------------------------------------------
// Cursor readouts
// ---------------------------------------------------------------------------
void ClimatologyDialog::UpdateCursorReadout(OverlayType id, const wxString& text)
{
    if (id < 0 || id >= NUM_OVERLAYS)
        return;
    if (m_rows[id].readout)
        m_rows[id].readout->SetLabel(text);
}

void ClimatologyDialog::ClearCursorReadouts()
{
    for (int i = 0; i < NUM_OVERLAYS; ++i)
        if (m_rows[i].readout)
            m_rows[i].readout->SetLabel(wxEmptyString);
}

// ---------------------------------------------------------------------------
// PushParamsToFactoryAndRender
// ---------------------------------------------------------------------------
void ClimatologyDialog::PushParamsToFactoryAndRender()
{
    if (!m_factory)
        return;

    // StandardDisplayParams → ClimatologyRenderParams
    m_renderParams.overlayType    = m_displayParams.overlayType;
    m_renderParams.overlayEnabled = m_displayParams.showOverlayType[m_displayParams.overlayType];

    m_renderParams.units = m_displayParams.units[m_displayParams.overlayType];
    m_renderParams.mode  = m_displayParams.mode;

    m_renderParams.showArrows   = m_displayParams.showArrows[m_displayParams.overlayType];
    m_renderParams.arrowWidth   = m_displayParams.arrowWidth[m_displayParams.overlayType];
    m_renderParams.arrowSpacing = m_displayParams.arrowSpacing[m_displayParams.overlayType];
    m_renderParams.arrowSize    = m_displayParams.arrowSize[m_displayParams.overlayType];
    m_renderParams.arrowMode    = m_displayParams.arrowMode[m_displayParams.overlayType];

    m_renderParams.showNumbers = m_displayParams.showNumbers[m_displayParams.overlayType];
    m_renderParams.numberSize  = m_displayParams.numberSpacing;

    m_renderParams.showIsoBars    = (m_displayParams.isoSpacing[m_displayParams.overlayType] > 0);
    m_renderParams.isoBarSpacing  = m_displayParams.isoSpacing[m_displayParams.overlayType];
    m_renderParams.isoBarStep     = m_displayParams.isoStep[m_displayParams.overlayType];

    m_renderParams.smoothing = m_displayParams.smooth;
    m_renderParams.windAtlas = m_displayParams.windAtlas;

    // Cyclone quick toggles
    m_renderParams.showCyclones  = m_displayParams.cyclone.enabled;
	m_renderParams.cycloneParams = m_displayParams.cyclone;


    m_renderParams.showElNino  = m_displayParams.cyclone.showElNino;
    m_renderParams.showLaNina  = m_displayParams.cyclone.showLaNina;
    m_renderParams.showNeutral = m_displayParams.cyclone.showNeutral;

    m_renderParams.showEPA = m_displayParams.cyclone.showEPA;
    m_renderParams.showWPA = m_displayParams.cyclone.showWPA;
    m_renderParams.showSPA = m_displayParams.cyclone.showSPA;
    m_renderParams.showATL = m_displayParams.cyclone.showATL;
    m_renderParams.showNIO = m_displayParams.cyclone.showNIO;
    m_renderParams.showSHE = m_displayParams.cyclone.showSHE;

    // CycloneFilterParams → factory
    m_factory->SetCycloneFilter(m_cycloneFilter);

    // StandardDisplayParams → factory
    m_factory->SetParams(m_displayParams);

    // Trigger render
    m_factory->Render(m_renderParams);
}

// ---------------------------------------------------------------------------
// ApplyRenderParams (external → dialog)
// ---------------------------------------------------------------------------
void ClimatologyDialog::ApplyRenderParams(const ClimatologyRenderParams& p)
{
    // Overlay type + visibility
 //   m_displayParams.overlayType = p.overlayType;
//    m_displayParams.showOverlayType[p.overlayType] = p.overlayEnabled;

//    m_displayParams.units[p.overlayType] = p.units;
    m_displayParams.mode                 = p.mode;

//    m_displayParams.showArrows[p.overlayType]   = p.showArrows;
//    m_displayParams.arrowWidth[p.overlayType]   = p.arrowWidth;
//    m_displayParams.arrowSpacing[p.overlayType] = p.arrowSpacing;
//    m_displayParams.arrowSize[p.overlayType]    = p.arrowSize;
//    m_displayParams.arrowMode[p.overlayType]    = p.arrowMode;

//   m_displayParams.showNumbers[p.overlayType] = p.showNumbers;
//    m_displayParams.numberSpacing              = p.numberSize;

//    m_displayParams.isoSpacing[p.overlayType] = p.isoBarSpacing;
//    m_displayParams.isoStep[p.overlayType]    = p.isoBarStep;

    m_displayParams.smooth    = p.smoothing;
    m_displayParams.windAtlas = p.windAtlas;

    // Cyclone timeline
    m_displayParams.cyclone.enabled = p.showCyclones;
	m_displayParams.cyclone         = p.cycloneParams;

    m_displayParams.cyclone.showElNino  = p.showElNino;
    m_displayParams.cyclone.showLaNina  = p.showLaNina;
    m_displayParams.cyclone.showNeutral = p.showNeutral;

    m_displayParams.cyclone.showEPA = p.showEPA;
    m_displayParams.cyclone.showWPA = p.showWPA;
    m_displayParams.cyclone.showSPA = p.showSPA;
    m_displayParams.cyclone.showATL = p.showATL;
    m_displayParams.cyclone.showNIO = p.showNIO;
    m_displayParams.cyclone.showSHE = p.showSHE;

    // Update basic UI
//    if (m_monthChoice)
//        m_monthChoice->SetSelection(p.currentMonth);
//    if (m_daySpin)
//        m_daySpin->SetValue(p.centerDay);

    for (int i = 0; i < NUM_OVERLAYS; ++i)
        if (m_rows[i].check)
            m_rows[i].check->SetValue(m_displayParams.showOverlayType[i]);

    PushParamsToFactoryAndRender();
}
