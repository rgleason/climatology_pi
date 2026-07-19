/******************************************************************************
 * ClimatologyDialog.h  —  Modern hand-written first dialog
 ******************************************************************************/

#ifndef CLIMATOLOGY_DIALOG_H_GUARD
#define CLIMATOLOGY_DIALOG_H_GUARD

#include <wx/wx.h>
#include <wx/choice.h>
#include <wx/slider.h>
#include <wx/checkbox.h>
#include <wx/stattext.h>
#include <wx/button.h>
#include <wx/sizer.h>
#include <wx/spinctrl.h>

#include <array>

#include "ClimatologyEnums.h"
#include "ClimatologyRenderParams.h"
#include "StandardDisplayParams.h"
#include "CycloneFilterParams.h"
#include "CycloneParams.h"


class ClimatologyOverlayFactory;
class ClimatologyConfigDialog;

// ---------------------------------------------------------------------------
// Overlay row widgets
// ---------------------------------------------------------------------------
struct OverlayRowWidgets {
    wxCheckBox*   check     = nullptr;
    wxStaticText* readout   = nullptr;
    wxButton*     configBtn = nullptr;  // only used for CYCLONES
};

// ---------------------------------------------------------------------------
//  ClimatologyDialog
// ---------------------------------------------------------------------------
class ClimatologyDialog final : public wxDialog
{
public:
    ClimatologyDialog(wxWindow*                  parent,
                      ClimatologyOverlayFactory* factory,
                      wxWindowID                 id    = wxID_ANY,
                      const wxString&            title = _("Climatology"),
                      const wxPoint&             pos   = wxDefaultPosition,
                      const wxSize&              size  = wxDefaultSize);

    ~ClimatologyDialog() override = default;

    // Plugin → dialog
    void UpdateCursorReadout(OverlayType id, const wxString& text);
    void ClearCursorReadouts();

    void PushParamsToFactoryAndRender();
    void ApplyRenderParams(const ClimatologyRenderParams& p);

    // Dialog → plugin
    const ClimatologyRenderParams& GetRenderParams()  const { return m_renderParams;  }
    const StandardDisplayParams&   GetDisplayParams() const { return m_displayParams; }

private:
    // UI builders
    wxSizer* BuildTimelineBar();
    wxSizer* BuildOverlayList();

    // Event handlers
    void OnAllCheckbox(wxCommandEvent& evt);
    void OnOverlayCheck(wxCommandEvent& evt);
    void OnCyclonesConfig(wxCommandEvent& evt);
    void OnClose(wxCloseEvent& evt);

    void OnMonthChanged(wxCommandEvent& evt);
    void OnDayChanged(wxSpinEvent& evt);

private:
    ClimatologyOverlayFactory*  m_factory      = nullptr;
    ClimatologyRenderParams     m_renderParams;
    StandardDisplayParams       m_displayParams;
    CycloneFilterParams         m_cycloneFilter;

    // Timeline controls
    wxChoice*   m_monthChoice    = nullptr;
    wxSpinCtrl* m_daySpin        = nullptr;
    wxCheckBox* m_allCheck       = nullptr;
    wxButton*   m_nowButton      = nullptr;

    // Cyclone / ENSO / basin toggles (quick access)
    wxCheckBox* m_allTimesCheck = nullptr;

    wxCheckBox* m_elNinoCheck  = nullptr;
    wxCheckBox* m_laNinaCheck  = nullptr;
    wxCheckBox* m_neutralCheck = nullptr;

    wxCheckBox* m_epaCheck = nullptr;
    wxCheckBox* m_wpaCheck = nullptr;
    wxCheckBox* m_spaCheck = nullptr;
    wxCheckBox* m_atlCheck = nullptr;
    wxCheckBox* m_nioCheck = nullptr;
    wxCheckBox* m_sheCheck = nullptr;

    // Overlay rows
    std::array<OverlayRowWidgets, NUM_OVERLAYS> m_rows;

    wxDECLARE_EVENT_TABLE();
    wxDECLARE_NO_COPY_CLASS(ClimatologyDialog);
};

#endif  // CLIMATOLOGY_DIALOG_H_GUARD
