#include <wx/wx.h>
#include <wx/dialog.h>
#include <wx/sizer.h>
#include <wx/statline.h>
#include <wx/choice.h>
#include <wx/spinctrl.h>
#include <wx/checkbox.h>
#include <wx/textctrl.h>
#include <wx/button.h>

#include "ClimatologyDialog.h"
#include "ClimatologyOverlayFactory.h"
#include "StandardDisplayParams.h"
#include "ClimatologyRenderParams.h"
#include "CycloneTypes.h"



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
    // ============================================================
    // 1. Initialize StandardDisplayParams (modern)
    // ============================================================
    m_displayParams.overlayType   = OVERLAY_WIND;
    m_displayParams.opacity       = 100;
    m_displayParams.transparency  = 0;
    m_displayParams.smooth        = true;
    m_displayParams.numberSpacing = 20;

    for (int i = 0; i < NUM_OVERLAYS; ++i)
    {
        m_displayParams.units[i]            = 0;
        m_displayParams.showOverlayType[i]  = false;
        m_displayParams.showArrows[i]       = false;
        m_displayParams.showNumbers[i]      = false;
        m_displayParams.arrowWidth[i]       = 1;
        m_displayParams.arrowSpacing[i]     = 20;
        m_displayParams.arrowSize[i]        = 12;
        m_displayParams.arrowMode[i]        = 0;
        m_displayParams.isoSpacing[i]       = 0;
        m_displayParams.isoStep[i]          = 0;
        m_displayParams.calibrationFactor[i] = 1.0;
        m_displayParams.calibrationOffset[i] = 0.0;
    }

    // Wind Atlas + Cyclones (modern)
    m_displayParams.windAtlas.enabled = false;
    m_displayParams.cyclone.enabled   = false;

    // ============================================================
    // 2. Initialize ClimatologyRenderParams (modern)
    // ============================================================
    m_renderParams.vp              = nullptr;
    m_renderParams.useGL           = true;
    m_renderParams.useShaders      = true;
    m_renderParams.dc              = nullptr;
    m_renderParams.dcContextValid  = false;
    m_renderParams.glContextValid  = false;

    m_renderParams.overlayType     = OVERLAY_WIND;
    m_renderParams.overlayEnabled  = false;
    m_renderParams.overlayMap      = true;

    m_renderParams.currentMonth      = 0;
    m_renderParams.nextMonth         = 0;
    m_renderParams.monthInterpolation = 1.0;
	
	m_renderParams.showArrows 		= false;
	m_renderParams.showArrows 		= false;
    m_renderParams.showNumbers         = false;
    m_renderParams.showIsoBars         = false;

    // Cyclones (modern)
    m_renderParams.showCyclones = false;

    // Basin toggles (modern)
    m_renderParams.showEPA = true;
    m_renderParams.showWPA = true;
    m_renderParams.showSPA = true;
    m_renderParams.showATL = true;
    m_renderParams.showNIO = true;
    m_renderParams.showSHE = true;

    // ENSO toggles (modern)
    m_renderParams.showElNino  = true;
    m_renderParams.showLaNina  = true;
    m_renderParams.showNeutral = true;

    // ============================================================
    // 3. Initialize CycloneFilterParams (modern)
    // ============================================================
    m_cycloneFilter.statemask        = 0xFFFFFFFF;
    m_cycloneFilter.minwindspeed     = 0;
    m_cycloneFilter.maxpressure      = 2000;
    m_cycloneFilter.allowElNino      = true;
    m_cycloneFilter.allowLaNina      = true;
    m_cycloneFilter.allowNeutral     = true;
    m_cycloneFilter.allowNotAvailable = true;

    m_cycloneFilter.startDate = wxDateTime(1, wxDateTime::Jan, 1850);
    m_cycloneFilter.endDate   = wxDateTime(31, wxDateTime::Dec, 2100);

    // ============================================================
    // 4. Build dialog UI (modern)
    // ============================================================
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

    Bind(wxEVT_CLOSE_WINDOW, &ClimatologyDialog::OnClose, this);
    closeBtn->Bind(wxEVT_BUTTON, [this](wxCommandEvent&){ Hide(); });

    // ============================================================
    // 5. Push initial params to factory
    // ============================================================
    if (m_factory)
    {
        m_factory->SetParams(m_displayParams);
        m_factory->SetCycloneFilter(m_cycloneFilter);
    }
}
