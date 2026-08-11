/******************************************************************************
 * ClimatologyDialog.h — Modern hand‑written first dialog
 ******************************************************************************/

#ifndef __CLIMATOLOGY_DIALOG_H__
#define __CLIMATOLOGY_DIALOG_H__

// Forward declarations for wx
class wxDialog;
class wxWindow;
class wxStaticLine;
class wxProgressDialog;
class wxChoice;
class wxSpinCtrl;
class wxCheckBox;
class wxButton;
class wxStaticText;
class wxSizer;

// Forward declarations for plugin classes
class ClimatologyOverlayFactory;
class ClimatologyConfigDialog;

// Includes needed for member variables
#include "ClimatologyRenderParams.h"
#include "StandardDisplayParams.h"
#include "CycloneFilterParams.h"
#include "ClimatologyEnums.h"

extern ClimatologyOverlayFactory* g_pOverlayFactory;


// ---------------------------------------------------------------------------
// Overlay row widgets
// ---------------------------------------------------------------------------
struct OverlayRowWidgets {
    wxCheckBox*   check      = nullptr;
    wxStaticText* readout    = nullptr;
    wxButton*     configBtn  = nullptr;
};

// ---------------------------------------------------------------------------
// ClimatologyDialog class declaration
// ---------------------------------------------------------------------------
class ClimatologyDialog : public wxDialog
{

public:
    ClimatologyDialog(wxWindow* parent,
                      ClimatologyOverlayFactory* factory,
                      wxWindowID id,
                      const wxString& title,
                      const wxPoint& pos,
                      const wxSize& size);
					  
//	void UpdateFollowState(int mode, bool enabled) override;   Removed for Step 0 add in step 5
//    void OnTideCurrentClick(TCClickInfo info) override;   Removed for Step 0 add in step 5
//  	void UpdateFollowState(int mode, bool enabled);
//    void OnTideCurrentClick(TCClickInfo info);

    // Event handlers
    void OnClose(wxCloseEvent& evt);
    void OnMonthChanged(wxCommandEvent& evt);
    void OnDayChanged(wxSpinEvent& evt);
    void OnAllCheckbox(wxCommandEvent& evt);
    void OnOverlayCheck(wxCommandEvent& evt);
    void OnCyclonesConfig(wxCommandEvent& evt);

    // External updates
    void UpdateCursorReadout(OverlayType id, const wxString& text);
    void ClearCursorReadouts();
    void PushParamsToFactoryAndRender();
    void ApplyRenderParams(const ClimatologyRenderParams& p);
	
	void BuildRenderParams(PlugIn_ViewPort* vp,
                       piDC* dc,
                       ClimatologyRenderParams& p);
	
	// To permit ClimatologyConfigDialog to modify ClimatologyDialog data  (parents data)
	CycloneParams& GetCycloneParams() { return m_cycloneParams; }
	CycloneFilterParams& GetCycloneFilter() { return m_cycloneFilter; }


private:
    // UI builders
    wxSizer* BuildTimelineBar();
    wxSizer* BuildOverlayList();

    // Core objects
    ClimatologyOverlayFactory* m_factory;

    // Parameters
	 // Model references (no legacy structs)
	StandardDisplayParams m_displayParams;

	// Last selected overlay type index (UI state)
	int m_lastOverlayType = OVERLAY_WIND;

    ClimatologyRenderParams   m_renderParams;
    	
	// --- Cyclone unified model (owned by ClimatologyDialog) ---
	CycloneParams        m_cycloneParams;
	CycloneFilterParams  m_cycloneFilter;

    // Timeline widgets
    wxChoice*     m_monthChoice = nullptr;
    wxSpinCtrl*   m_daySpin     = nullptr;
    wxCheckBox*   m_allCheck    = nullptr;
    wxButton*     m_nowButton   = nullptr;

    // ENSO toggles
    wxCheckBox*   m_elNinoCheck   = nullptr;
    wxCheckBox*   m_laNinaCheck   = nullptr;
    wxCheckBox*   m_neutralCheck  = nullptr;
    wxCheckBox*   m_allTimesCheck = nullptr;

    // Overlay rows
    OverlayRowWidgets m_rows[NUM_OVERLAYS];
	
	wxDECLARE_EVENT_TABLE();
};


#endif // __CLIMATOLOGY_DIALOG_H__
