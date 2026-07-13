/******************************************************************************
 *
 * Project:  OpenCPN
 * Purpose:  Climatology Plugin
 * Author:   Sean D'Epagnier
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
#include <wx/timer.h>

#include "CycloneFilterParams.h"
#include "IsoBarMap.h"

// Forward declarations
class ClimatologyDialog;
class ClimatologyIsoBarMap;

// ---------------------------------------------------------------------------
// Overlay settings (per–data-type configuration)
// ---------------------------------------------------------------------------

struct ClimatologyOverlaySettings
{
    // Calibration helpers (per setting)
    double CalibrationOffset(int setting);
    double CalibrationFactor(int setting);

    double CalibrateValue(int setting, double v)
    {
        return CalibrationFactor(setting) * (v + CalibrationOffset(setting));
    }

    // Persistence
    void Load();
    void Save();

    // Canonical list of data-type settings
    enum SettingsType {
        WIND,
        CURRENT,
        SLP,
        SST,
        AT,
        CLOUD,
        PRECIPITATION,
        RELATIVE_HUMIDITY,
        LIGHTNING,
        SEADEPTH,
        SETTINGS_COUNT
    };

    // Unit enums (legacy, used by config dialog)
    enum Units0 { KNOTS, M_S, MPH, KPH };
    enum Units1 { MILLIBARS, MMHG };
    enum Units2 {
        MM_DAY, IN_DAY,
        MM_MONTH, M_MONTH, IN_MONTH, FT_MONTH,
        M_YEAR, IN_YEAR, FT_YEAR
    };
    enum Units3 { CELCIUS, FAHRENHEIT };
    enum Units4 { PERCENTAGE };
    enum Units6 { METERS, FEET };

    struct OverlayDataSettings {
        // Units + enable
        int  m_Units;
        bool m_bEnabled;

        // Overlay map
        bool m_bOverlayMap;
        int  m_iOverlayTransparency;
        bool m_bOverlayInterpolation;

        // IsoBars
        bool               m_bIsoBars;
        int                m_iIsoBarSpacing;
        int                m_iIsoBarStep;
        ClimatologyIsoBarMap* m_pIsobars[13]; // 12 months + all‑months

        // Numbers
        bool   m_bNumbers;
        double m_iNumbersSpacing;

        // Direction arrows
        bool    m_bDirectionArrows;
        int     m_iDirectionArrowsLengthType;
        int     m_iDirectionArrowsWidth;
        wxColour m_cDirectionArrowsColor;
        int     m_iDirectionArrowsSize;
        int     m_iDirectionArrowsSpacing;
    };

    OverlayDataSettings Settings[SETTINGS_COUNT];
};

// ---------------------------------------------------------------------------
// Configuration dialog (manual wxWidgets UI)
// ---------------------------------------------------------------------------

class ClimatologyConfigDialog : public wxDialog
{
public:
    explicit ClimatologyConfigDialog(ClimatologyDialog* parent);
    ~ClimatologyConfigDialog();

    // Human‑readable name for a setting index
    wxString SettingName(int setting);

    // Disable IsoBars for a given setting (and update UI)
    void DisableIsoBars(int setting);

    // Data‑type choice handler (overlay type changed)
    void OnDataTypeChoice(wxCommandEvent& event);

    // Public settings object (used by plugin + dialog)
    ClimatologyOverlaySettings m_Settings;

    // Save all settings to config file
    void Save();

private:
    // Load/save from wxFileConfig
    void LoadSettings();
    void SaveSettings();

    // Per‑setting UI <-> model sync
    void SetDataTypeSettings(int settings);
    void ReadDataTypeSettings(int settings);
    void PopulateUnits(int settings);

    // Notebook page changed
    void OnPageChanged(wxNotebookEvent& event);

    // Generic update fan‑out
    void OnUpdate();
    void OnUpdate(wxCommandEvent& event)        { OnUpdate(); }
    void OnUpdateSpin(wxSpinEvent& event)       { OnUpdate(); }
    void OnUpdateColor(wxColourPickerEvent& e)  { OnUpdate(); }
    void OnUpdateScroll(wxScrollEvent& event)   { OnUpdate(); }

    // Overlay‑specific updates
    void OnUpdateOverlayConfig(wxCommandEvent& event);
    void OnUpdateIsobar();
    void OnUpdateSpinIsobar(wxSpinEvent& event) { OnUpdateIsobar(); }
    void OnUpdateIsobar(wxCommandEvent& event)  { OnUpdateIsobar(); }

    // Cyclone filter + ENSO updates
    void OnUpdateCyclones();
    void OnCycloneFilterChanged(wxCommandEvent& event);
    CycloneFilterParams GetCycloneFilterParams() const;
    int GetCycloneStateMask() const;

    void OnUpdateCyclonesDate(wxDateEvent& event) { OnUpdateCyclones(); }
    void OnUpdateCyclonesSpin(wxSpinEvent& event) { OnUpdateCyclones(); }
    void OnUpdateCyclones(wxCommandEvent& event)  { OnUpdateCyclones(); }

    // Legend / key painting
    void OnPaintKey(wxPaintEvent& event);

    // Enable/disable plugin
    void OnEnabled(wxCommandEvent& event);

    // About / author info
    void OnAboutAuthor(wxCommandEvent& event);

    // Close handler
    void OnClose(wxCommandEvent& event) { Hide(); }

    // Periodic refresh (e.g., progress / status)
    void OnRefreshTimer(wxTimerEvent& event);

    int               m_lastdatatype;
    ClimatologyDialog* pParent;
    wxTimer           m_refreshTimer;

    // Cyclone filter UI controls
    wxSpinCtrl*       m_sMinWind;
    wxSpinCtrl*       m_sMaxPressure;

    wxDatePickerCtrl* m_dpStart;
    wxDatePickerCtrl* m_dpEnd;

    wxCheckBox*       m_cbCycloneTropical;
    wxCheckBox*       m_cbCycloneSubTropical;
    wxCheckBox*       m_cbCycloneExtraTropical;
    wxCheckBox*       m_cbCycloneWave;
    wxCheckBox*       m_cbCycloneRemanent;
    wxCheckBox*       m_cbCycloneUnknown;

    wxCheckBox*       m_cbElNino;
    wxCheckBox*       m_cbLaNina;
    wxCheckBox*       m_cbNeutral;
    wxCheckBox*       m_cbUnknownENSO;

    wxDECLARE_EVENT_TABLE();
};

#endif // CLIMATOLOGY_CONFIG_DIALOG_H
