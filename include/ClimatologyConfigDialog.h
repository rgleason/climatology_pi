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


// --- Forward declarations (wxWidgets) ---
class wxNotebook;
class wxPanel;
class wxChoice;
class wxSpinCtrl;
class wxCheckBox;
class wxSlider;
class wxTimer;
class wxHtmlWindow;
class wxColourPickerEvent;
class wxColourPickerCtrl;
class wxDateEvent;
class wxDatePickerCtrl;
class wxSpinEvent;
class wxStaticText;
class wxSizer;
class wxDateEvent;
class wxCommandEvent;


// --- Forward declarations (plugin classes) ---
class ClimatologyOverlayFactory;
class ClimatologyDialog;

// --- Includes required for member variables ---
#include "DisplayMode.h"
#include "StandardDisplayParams.h"
#include "CycloneParams.h"
#include "CycloneFilterParams.h"

// ---------------------------------------------------------------------------
// Class declaration
// ---------------------------------------------------------------------------

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
     
    ClimatologyConfigDialog(wxWindow* parent,
                            ClimatologyOverlayFactory* factory);

    ClimatologyConfigDialog(ClimatologyDialog* parent,
                            StandardDisplayParams& displayParams,
                            CycloneParams& cycloneParams);

    ~ClimatologyConfigDialog();

    // Persist all settings to wxFileConfig.
    // Writes StandardDisplayParams and CycloneParams to the config file.
	CycloneFilterParams GetCycloneFilterParams() const;
		
    void Save();
   // --- Persistence helpers ----------------------------------------------
    void LoadSettings();
    void SaveSettings();
	
	// No cycloneParams stored here anymore
    // No GetCycloneParams() here
    // No m_parent->anything() here

	// Construct the dialog for 4-tab Config cyclones
    // ClimatologyConfigDialog(wxWindow* parent, ClimatologyOverlayFactory* factory);


private:
 
    ClimatologyDialog* m_parent;

    // Model references (no legacy structs)
    StandardDisplayParams m_displayParams;
	
	// --- Internal helpers -------------------------------------------------
    int  GetCycloneStateMask() const;
    void ApplyTheme();   // wraps DimeWindow(this)

    // Owner
       wxTimer                m_refreshTimer;

     // Last selected overlay type index (UI state)
	int m_lastOverlayType = OVERLAY_WIND;

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

 	void OnUpdateCyclones(wxEvent& event);
	void OnUpdateCyclonesSpin(wxSpinEvent& event);
	void OnUpdateCyclonesDate(wxDateEvent& event);
	void OnCycloneFilterChanged(wxCommandEvent& event);
    void OnCyclonesDateChanged(wxDateEvent& event);

    void OnPaintKey(wxPaintEvent& event);
    void OnEnabled(wxCommandEvent& event);
    void OnAboutAuthor(wxCommandEvent& event);
    void OnClose(wxCommandEvent& event) { Hide(); }
    void OnRefreshTimer(wxTimerEvent& event);



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
