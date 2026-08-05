/******************************************************************************
 *
 * Project:  OpenCPN
 * Purpose:  Climatology Plugin - 4-tab configuration dialog
 * Author:   Modernized for unified-grid architecture
 *
 ***************************************************************************
 */

#ifndef CLIMATOLOGY_CONFIG_DIALOG_H
#define CLIMATOLOGY_CONFIG_DIALOG_H

#include <wx/wxprec.h>
#include <wx/fileconf.h>

#ifndef WX_PRECOMP
#include <wx/wx.h>
#endif

#include <wx/notebook.h>
#include <wx/clrpicker.h>
#include <wx/datectrl.h>
#include <wx/dateevt.h>
#include <wx/spinctrl.h>
#include <wx/checkbox.h>
#include <wx/slider.h>
#include <wx/timer.h>
#include <wx/html/htmlwin.h>

#include "DisplayMode.h"             // DisplayMode enum
#include "ClimatologyEnums.h"
#include "StandardDisplayParams.h"   // UI-facing persistent overlay params
#include "CycloneParams.h"           // Persistent cyclone appearance params
#include "CycloneFilterParams.h"     // Runtime cyclone filter params


class ClimatologyOverlayFactory;
class ClimatologyDialog;

/**
 * @brief 4-tab configuration dialog for the Climatology plugin.
 *
 * This dialog:
 *  - Edits StandardDisplayParams (per-overlay display + blend mode).
 *  - Edits CycloneParams (persistent cyclone appearance).
 *  - Produces CycloneFilterParams (runtime cyclone filter) for the factory.
 *
 * Tabs:
 *  - Standard Displays
 *  - Wind Atlas
 *  - Cyclones
 *  - Information
 */
class ClimatologyConfigDialog : public wxDialog
{
public:
	// Construct the configuration dialog.
    // @param parent          Owning ClimatologyDialog.
    // @param displayParams   Reference to global StandardDisplayParams.
    // @param cycloneParams   Reference to global CycloneParams.
     
    ClimatologyConfigDialog(ClimatologyDialog* parent,
                            StandardDisplayParams& displayParams,
                            CycloneParams& cycloneParams);

    ~ClimatologyConfigDialog();

    // Persist all settings to wxFileConfig.
    // Writes StandardDisplayParams and CycloneParams to the config file.
   
    void Save();

    // Build a CycloneFilterParams snapshot from current UI.
    // Used by ClimatologyOverlayFactory to filter cyclone tracks.
   
    CycloneFilterParams GetCycloneFilterParams() const;
	
	// Construct the dialog for 4-tab Config cyclones
    ClimatologyConfigDialog(wxWindow* parent, ClimatologyOverlayFactory* factory);


private:
    // --- Persistence helpers ----------------------------------------------
    void LoadSettings();
    void SaveSettings();

    // --- UI <-> model sync -------------------------------------------------
    void SyncStandardTabFromModel();
    void SyncStandardTabToModel();

    void SyncWindAtlasTabFromModel();
    void SyncWindAtlasTabToModel();

    void SyncCyclonesTabFromModel();
    void SyncCyclonesTabToModel();

    // --- Event handlers ----------------------------------------------------
    void OnPageChanged(wxNotebookEvent& event);

    void OnUpdate();                             // generic fan-out
    void OnUpdate(wxCommandEvent& event)        { OnUpdate(); }
    void OnUpdateSpin(wxSpinEvent& event)       { OnUpdate(); }
    void OnUpdateScroll(wxScrollEvent& event)   { OnUpdate(); }
    void OnUpdateColor(wxColourPickerEvent& e)  { OnUpdate(); }

    void OnUpdateOverlayConfig(wxCommandEvent& event);
    void OnUpdateCyclones(wxCommandEvent& event);
    void OnCycloneFilterChanged(wxCommandEvent& event);

    void OnCyclonesDateChanged(wxDateEvent& event) { OnUpdateCyclones(event); }
    void OnCyclonesSpin(wxSpinEvent& event)        { OnUpdateCyclones(event); }

    void OnPaintKey(wxPaintEvent& event);
    void OnEnabled(wxCommandEvent& event);
    void OnAboutAuthor(wxCommandEvent& event);
    void OnClose(wxCommandEvent& event) { Hide(); }
    void OnRefreshTimer(wxTimerEvent& event);

    // --- Internal helpers --------------------------------------------------
    int  GetCycloneStateMask() const;
    void ApplyTheme();   // wraps DimeWindow(this)

private:
    // Owner
    ClimatologyDialog*     m_parent;
    wxTimer                m_refreshTimer;

    // Model references (no legacy structs)
    StandardDisplayParams& m_displayParams;
    CycloneParams&         m_cycloneParams;

    // Last selected overlay type index (UI state)
    int                    m_lastOverlayType = OVERLAY_WIND;

    // --- Notebook + tabs ---------------------------------------------------
    wxNotebook* m_notebook = nullptr;

    wxPanel*    m_panelStandard  = nullptr;
    wxPanel*    m_panelWindAtlas = nullptr;
    wxPanel*    m_panelCyclones  = nullptr;
    wxPanel*    m_panelInfo      = nullptr;

    // --- Standard Displays tab controls -----------------------------------
    wxChoice*   m_cOverlayType      = nullptr;  // OVERLAY_WIND, etc.
    wxChoice*   m_cUnits            = nullptr;  // units per overlay
    wxChoice*   m_cBlendMode        = nullptr;  // DisplayMode

    wxCheckBox* m_cbShowOverlay     = nullptr;
    wxSlider*   m_sTransparency     = nullptr;

    wxCheckBox* m_cbShowIsoBars     = nullptr;
    wxSpinCtrl* m_sIsoSpacing       = nullptr;
    wxChoice*   m_cIsoStep          = nullptr;

    wxCheckBox* m_cbShowNumbers     = nullptr;
    wxSpinCtrl* m_sNumberSize       = nullptr;

    wxCheckBox* m_cbShowArrows      = nullptr;
    wxSpinCtrl* m_sArrowWidth       = nullptr;
    wxSpinCtrl* m_sArrowSpacing     = nullptr;
    wxSpinCtrl* m_sArrowSize        = nullptr;
    wxChoice*   m_cArrowMode        = nullptr;

    wxColourPickerCtrl* m_cpOverlayColor = nullptr;

    // --- Wind Atlas tab controls ------------------------------------------
    wxCheckBox* m_cbWindAtlasEnable   = nullptr;
    wxSpinCtrl* m_sWindAtlasSize      = nullptr;
    wxSpinCtrl* m_sWindAtlasSpacing   = nullptr;
    wxSlider*   m_sWindAtlasOpacity   = nullptr;

    // --- Cyclones tab controls --------------------------------------------
    wxCheckBox* m_cbCyclonesEnable    = nullptr;

    wxSpinCtrl* m_sCycloneCenterDay   = nullptr;
    wxSpinCtrl* m_sCycloneDaySpan     = nullptr;

    wxSpinCtrl* m_sMinWind            = nullptr;
    wxSpinCtrl* m_sMaxPressure        = nullptr;

    wxDatePickerCtrl* m_dpStart       = nullptr;
    wxDatePickerCtrl* m_dpEnd         = nullptr;

    // Storm-type mask checkboxes
    wxCheckBox* m_cbCycloneTropical   = nullptr;
    wxCheckBox* m_cbCycloneSubTropical= nullptr;
    wxCheckBox* m_cbCycloneExtraTropical = nullptr;
    wxCheckBox* m_cbCycloneWave       = nullptr;
    wxCheckBox* m_cbCycloneRemanent   = nullptr;
    wxCheckBox* m_cbCycloneUnknown    = nullptr;

    // ENSO visibility
    wxCheckBox* m_cbElNino            = nullptr;
    wxCheckBox* m_cbLaNina            = nullptr;
    wxCheckBox* m_cbNeutral           = nullptr;
    wxCheckBox* m_cbUnknownENSO       = nullptr;

    // Basin visibility
    wxCheckBox* m_cbEPA               = nullptr;
    wxCheckBox* m_cbWPA               = nullptr;
    wxCheckBox* m_cbSPA               = nullptr;
    wxCheckBox* m_cbATL               = nullptr;
    wxCheckBox* m_cbNIO               = nullptr;
    wxCheckBox* m_cbSHE               = nullptr;

    // All-times mode
    wxCheckBox* m_cbAllTimesCyclones  = nullptr;

    // --- Information tab ---------------------------------------------------
    wxHtmlWindow* m_htmlInformation   = nullptr;

    wxDECLARE_EVENT_TABLE();
};

#endif // CLIMATOLOGY_CONFIG_DIALOG_H
