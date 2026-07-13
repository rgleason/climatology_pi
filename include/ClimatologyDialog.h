/******************************************************************************
 *
 * Project:  OpenCPN
 * Purpose:  Climatology Plugin
 * Author:   Sean D'Epagnier
 *
 ***************************************************************************
 *   Copyright (C) 2016 by Sean D'Epagnier                                 *
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
 * ClimatologyDialog.h  —  Modern hand-written first dialog
 *
 * Replaces all wxFormBuilder / legacy boilerplate.
 * Uses Bind() throughout; layout built entirely in code.
 *
 * Responsibilities of this dialog:
 *   • Timeline bar  : month choice + day slider + "All" checkbox +
 *                     "Now" button + full-width day-of-year slider
 *   • Overlay list  : 11 rows, each with enable checkbox + text-only
 *                     cursor readout; Cyclones row adds a "Config…" button
 *                     that will open the second (Cyclones config) dialog.
 *   • Outputs       : ClimatologyRenderParams  — consumed by the renderer
 *                     StandardDisplayParams    — OpenCPN display contract
 *
 * The dialog hides rather than closes, preserving all state.
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
#include <wx/scrolwin.h>

#include <array>

#include "ClimatologyRenderParams.h"
#include "StandardDisplayParams.h"
#include "OverlayTypes.h"
#include "CycloneFilterParams.h"

// ---------------------------------------------------------------------------
// Forward declarations
// ---------------------------------------------------------------------------
class ClimatologyOverlayFactory;

// ---------------------------------------------------------------------------
// Overlay index enum
//   Keep in sync with ClimatologyOverlayFactory's own enum / constants.
// ---------------------------------------------------------------------------
// Use canonical OverlayType enum    #include "OverlayTypes.h"

// ---------------------------------------------------------------------------
// Internal bundle holding every widget for one overlay row.
// ---------------------------------------------------------------------------
struct OverlayRowWidgets {
    wxCheckBox*   check     = nullptr;  ///< enable / disable overlay
    wxStaticText* readout   = nullptr;  ///< text-only cursor value
    wxButton*     configBtn = nullptr;  ///< non-null only for CYCLONES
};

// ---------------------------------------------------------------------------
//  ClimatologyDialog
// ---------------------------------------------------------------------------
class ClimatologyDialog final : public wxDialog
{
public:
    // ctor / dtor -----------------------------------------------------------
    ClimatologyDialog(wxWindow*                  parent,
                      ClimatologyOverlayFactory* factory,
                      wxWindowID                 id    = wxID_ANY,
                      const wxString&            title = _("Climatology"),
                      const wxPoint&             pos   = wxDefaultPosition,
                      const wxSize&              size  = wxDefaultSize);

    ~ClimatologyDialog() override = default;

    // -- Plugin → dialog interface ------------------------------------------

    /// Called by the plugin whenever the cursor rests over the chart.
    /// Pass an empty string to reset the readout to its idle state.
	void UpdateCursorReadout(OverlayType id, const wxString& text);

    /// Convenience: clear all readouts at once (cursor left the chart).
    void ClearCursorReadouts();
	
	// Called when dialog is changed: overlay checkbox, timeline slider, “All”, “Now”,  month/day, DOY
	//  dialog’s “commit + redraw” function.   Synchronizes SetParam + SetCycloneFilter
	//Push updated parameters downstream and trigger a render.  User driven changes.
	void PushParamsToFactoryAndRender();

    /// Apply an externally loaded parameter set (e.g. after loading a preset).
	// Factory → Dialog   “load UI from external state” function. Programatic changes. 
	// Update the dialog UI from externally loaded parameters without causing a render.
    void ApplyRenderParams(const ClimatologyRenderParams& p);

    // -- Dialog → plugin interface ------------------------------------------

    const ClimatologyRenderParams& GetRenderParams()  const { return m_renderParams;  }
    const StandardDisplayParams&   GetDisplayParams() const { return m_displayParams; }

private:
    // -----------------------------------------------------------------------
    // UI construction helpers — each returns the sizer it populates
    // -----------------------------------------------------------------------
    wxSizer* BuildTimelineBar();
    wxSizer* BuildOverlayList();

    // -----------------------------------------------------------------------
    // Event handlers — timeline
    // -----------------------------------------------------------------------
    // void OnMonthChoice    (wxCommandEvent& evt);
    // void OnDaySlider      (wxCommandEvent& evt);
       void OnAllCheckbox    (wxCommandEvent& evt);
    // void OnNowButton      (wxCommandEvent& evt);
    // void OnTimelineSlider (wxCommandEvent& evt);

    // -----------------------------------------------------------------------
    // Event handlers — overlay list
    // -----------------------------------------------------------------------
    void OnOverlayCheck   (wxCommandEvent& evt);
    void OnCyclonesConfig (wxCommandEvent& evt);

    // -----------------------------------------------------------------------
    // Event handlers — dialog lifecycle
    // -----------------------------------------------------------------------
    void OnClose (wxCloseEvent& evt);

    // -----------------------------------------------------------------------
    // Internal synchronisation helpers
    // -----------------------------------------------------------------------

    /// Push m_renderParams.dayOfYear into month/day/timeline controls.
    // void SyncControlsFromDOY();

    /// Read month + day controls, write result into m_renderParams.dayOfYear
    /// AND into the full-width timeline slider.
    // void SyncDOYFromMonthDay();

    /// Enable/disable timeline controls according to the "All" checkbox.
   // void UpdateTimelineSensitivity();

    /// Days in a given month (1-based); uses year=2000 (leap) as reference.
    // static int DaysInMonth(int month, int year = 2000);

    /// Convert (month 1-based, day 1-based) → day-of-year 1-based.
    //  static int MonthDayToDOY(int month, int day);

    /// Convert day-of-year 1-based → { month 1-based, day 1-based }.
    // static std::pair<int,int> DOYToMonthDay(int doy);

    // -----------------------------------------------------------------------
    // Data members
    // -----------------------------------------------------------------------
    ClimatologyOverlayFactory*  m_factory      = nullptr;
	ClimatologyRenderParams     m_renderParams;    // from canonical header
	StandardDisplayParams       m_displayParams;   // from canonical header
	CycloneFilterParams         m_cycloneFilter;   // optional, if you expose date filters

    // Timeline controls
 //   wxChoice*   m_monthChoice    = nullptr;  ///< month selector
 //   wxSlider*   m_daySlider      = nullptr;  ///< day-within-month, 1-31
    wxCheckBox* m_allCheck       = nullptr;  ///< "All" (full-year aggregate)
 //   wxButton*   m_nowBtn         = nullptr;  ///< jump to today's DOY
 //   wxSlider*   m_timelineSlider = nullptr;  ///< full-width DOY slider 1-365

    // Overlay rows — indexed by Climatology Overlay
	std::array<OverlayRowWidgets, NUM_OVERLAYS> m_rows;
	
    // -----------------------------------------------------------------------
    // Constants
    // -----------------------------------------------------------------------
 //   static constexpr int kDOYMin = 1;
 //   static constexpr int kDOYMax = 365;

    wxDECLARE_NO_COPY_CLASS(ClimatologyDialog);
};

#endif  // CLIMATOLOGY_DIALOG_H_GUARD
