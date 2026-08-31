/******************************************************************************
 * ClimatologyDialog.cpp  —  Modern hand-written first dialog
 ******************************************************************************/

// Otherwise MSVC will misorder wx includes (wx/wxprec.h)
// and the plugin API symbols will not resolve.
#include "ocpn_plugin_guarded.h"

// wxWidgets includes (only in .cpp)
#include <wx/wxprec.h>
#ifndef WX_PRECOMP
    #include <wx/wx.h>
#endif
#include <wx/statline.h>
#include <wx/progdlg.h>
#include <wx/sizer.h>
#include <wx/spinctrl.h>
#include <wx/choice.h>
#include <wx/timer.h>
#include <wx/html/htmlwin.h>

#include "ClimatologyDialog.h"
#include "ClimatologyOverlayFactory.h"
#include "ClimatologyConfigDialog.h"

#include "ClimatologyEnums.h"
#include "CycloneFilterParams.h"
#include "CycloneUtils.h"
#include "CycloneStructs.h"

extern ClimatologyOverlayFactory* g_pOverlayFactory;


wxBEGIN_EVENT_TABLE(ClimatologyDialog, wxDialog)
    EVT_CLOSE(ClimatologyDialog::OnClose)
wxEND_EVENT_TABLE()


// ---------------------------------------------------------------------------
// Constructor
// ---------------------------------------------------------------------------

ClimatologyDialog::ClimatologyDialog(wxWindow* parent,
                                     climatology_pi* plugin,
                                     ClimatologyOverlayFactory* factory,
                                     wxWindowID id,
                                     const wxString& title,
                                     const wxPoint& pos,
                                     const wxSize& size)
    : wxDialog(parent, id, title, pos, size,
               wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER)
    , m_factory(factory)
    , m_pi(plugin)      
{

    DimeWindow(this);

    // -----------------------------------------------------------------------
    // Initialize StandardDisplayParams (modern)
    // -----------------------------------------------------------------------
    m_displayParams.overlayType   = OVERLAY_WIND;
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

    // ----------------------------------------------------------
    // Initialize ClimatologyRenderParams (modern)
    // ----------------------------------------------------------
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
	// Initialize CycloneFilterParams (modern unified model)
	// -----------------------------------------------------------------------

	// ENSO: allow all by default
	m_cycloneFilter.ensoFilter = {
		CycloneENSO::EL_NINO,
		CycloneENSO::LA_NINA,
		CycloneENSO::NEUTRAL,
		CycloneENSO::NA
	};

	// Storm types: allow all by default
	m_cycloneFilter.stateFilter = {
		CycloneState::TROPICAL,
		CycloneState::SUBTROPICAL,
		CycloneState::EXTRATROPICAL,
		CycloneState::WAVE,
		CycloneState::REMANENT,
		CycloneState::UNKNOWN
	};

	// Basins: allow all by default
	m_cycloneFilter.basinFilter = {
		CycloneBasin::EPA,
		CycloneBasin::WPA,
		CycloneBasin::SPA,
		CycloneBasin::ATL,
		CycloneBasin::NIO,
		CycloneBasin::SHE
	};

	// Months: allow all (empty = all months)
	m_cycloneFilter.monthFilter.clear();

	// Year range
	m_cycloneFilter.yearMin = 1850;
	m_cycloneFilter.yearMax = 2100;

	// Intensity thresholds
	m_cycloneFilter.windMin     = 0.0;
	m_cycloneFilter.windMax     = 999.0;
	m_cycloneFilter.pressureMin = 0.0;
	m_cycloneFilter.pressureMax = 2000.0;

	// Date range
	m_cycloneFilter.startDate = wxDateTime(1, wxDateTime::Jan, 1850);
	m_cycloneFilter.endDate   = wxDateTime(31, wxDateTime::Dec, 2100);

	// Timeline window
	m_cycloneFilter.daySpan = 30;


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

void ClimatologyDialog::OnCyclonesConfig(wxCommandEvent& event)
{
    ClimatologyConfigDialog dlg(this);
    dlg.ShowModal();
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

    // -----------------------------------------------------------------------
    // StandardDisplayParams → ClimatologyRenderParams
    // -----------------------------------------------------------------------
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

    // Cyclone appearance
    m_renderParams.showCyclones  = m_displayParams.cyclone.enabled;
    m_renderParams.cycloneParams = m_displayParams.cyclone;

    // CycloneFilterParams → factory
    m_factory->SetCycloneFilter(m_cycloneFilter);

    // StandardDisplayParams → factory
    m_factory->SetParams(m_displayParams);

    // Trigger render
    m_factory->Render(m_renderParams);
}


void ClimatologyDialog::BuildRenderParams(PlugIn_ViewPort* vp,
                                          piDC* dc,
                                          ClimatologyRenderParams& p)
{
    int overlayIndex = p.overlayType;   // or m_params.overlayType
    g_pOverlayFactory->BuildRenderParams(overlayIndex, vp, dc, p);
}



// ---------------------------------------------------------------------------
// ApplyRenderParams (external → dialog)
// Synchronize dialog model with an incoming ClimatologyRenderParams snapshot.
// ---------------------------------------------------------------------------
void ClimatologyDialog::ApplyRenderParams(const ClimatologyRenderParams& p)
{
    // ============================================================
    // 1. Update StandardDisplayParams (appearance + overlay settings)
    // ============================================================

    int idx = p.overlayType;

    // Overlay type + visibility
    m_displayParams.overlayType = p.overlayType;
    m_displayParams.showOverlayType[idx] = p.overlayEnabled;

    // Units
    m_displayParams.units[idx] = p.units;

    // Blend mode
    m_displayParams.mode = p.mode;

    // IsoBars
    m_displayParams.showIsoBars[idx] = p.showIsoBars;
    m_displayParams.isoSpacing[idx]  = p.isoBarSpacing;
    m_displayParams.isoStep[idx]     = p.isoBarStep;

    // Numbers
    m_displayParams.showNumbers[idx] = p.showNumbers;
    m_displayParams.numberSize[idx]  = p.numberSize;

    // Arrows
    m_displayParams.showArrows[idx]   = p.showArrows;
    m_displayParams.arrowWidth[idx]   = p.arrowWidth;
    m_displayParams.arrowSpacing[idx] = p.arrowSpacing;
    m_displayParams.arrowSize[idx]    = p.arrowSize;
    m_displayParams.arrowMode[idx]    = p.arrowMode;

    // Color + alpha
    m_displayParams.color[idx] = p.color;
    m_displayParams.alpha[idx] = p.alpha;

    // Smoothing
    m_displayParams.smooth = p.smoothing;

    // Wind Atlas
    m_displayParams.windAtlas = p.windAtlas;

    // ============================================================
    // 2. Update CycloneParams (appearance only)
    // ============================================================

    m_displayParams.cyclone.enabled = p.showCyclones;
    m_displayParams.cyclone         = p.cycloneParams;

    // ============================================================
    // 3. Update CycloneFilterParams (logical filtering)
    // ============================================================

    m_cycloneFilter = p.cycloneFilter;

    // ============================================================
    // 4. Update basic UI overlay checkboxes
    // ============================================================

    for (int i = 0; i < NUM_OVERLAYS; ++i)
        if (m_rows[i].check)
            m_rows[i].check->SetValue(m_displayParams.showOverlayType[i]);

    // ============================================================
    // 5. Push updated model → factory → renderer
    // ============================================================

    PushParamsToFactoryAndRender();
}

