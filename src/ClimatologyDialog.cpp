/******************************************************************************
 *
 * Project:  OpenCPN
 * Purpose:  Climatology Object
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
 *
 */

// GLEW MUST be first
// #define GLEW_STATIC
#include <GL/glew.h>
#include "gldefs.h"

#include "wx/wx.h"
#include "wx/datetime.h"
#include <wx/dir.h>
#include <wx/debug.h>
#include <wx/graphics.h>

#include <stdlib.h>
#include <math.h>
#include <time.h>

#include "climatology_pi.h"
#include "ClimatologyDialog.h"
#include "ClimatologyConfigDialog.h"

// =========================
// Formatting Helpers
// =========================

static double ConvertSpeed(double v, int unit)
{
    // 0 = knots, 1 = m/s, 2 = km/h
    switch(unit) {
    case 1: return v * 0.514444;   // knots m/s
    case 2: return v * 1.852;      // knots km/h
    default: return v;             // knots
    }
}

static wxString UnitSuffix(int unit)
{
    switch(unit) {
    case 1: return " m/s";
    case 2: return " km/h";
    default: return " kt";
    }
}

static wxString FormatSpeed(double v, int unit, bool isCurrent=false)
{
    if (!wxFinite(v))
        return "—";

    double cv = ConvertSpeed(v, unit);

    // Wind: 1 decimal, Current: 2 decimals
    return isCurrent ?
        wxString::Format("%.2f%s", cv, UnitSuffix(unit)) :
        wxString::Format("%.1f%s", cv, UnitSuffix(unit));
}

static wxString FormatDirection(double d)
{
    if (!wxFinite(d))
        return "—";

    int deg = (int)round(d) % 360;

    static const char* cardinals[] = {
        "N","NNE","NE","ENE","E","ESE","SE","SSE",
        "S","SSW","SW","WSW","W","WNW","NW","NNW"
    };

    int idx = (int)round(deg / 22.5) % 16;
    return wxString::Format("%d° (%s)", deg, cardinals[idx]);
}

static wxColour ValueColor(double v, double low, double high)
{
    if (!wxFinite(v))
        return *wxLIGHT_GREY;   // missing data

    if (v >= high)
        return wxColour(200, 0, 0);      // red = high
    if (v >= low)
        return wxColour(200, 120, 0);    // orange = medium

    return wxColour(0, 120, 0);          // green = normal
}



ClimatologyDialog::ClimatologyDialog(wxWindow *parent, climatology_pi *ppi)
#ifndef __WXOSX__
    : ClimatologyDialogBase(parent),
#else
    : ClimatologyDialogBase(parent, wxID_ANY, _("Climatology Display Control"), wxDefaultPosition, wxDefaultSize, wxCAPTION|wxCLOSE_BOX|wxRESIZE_BORDER|wxSYSTEM_MENU|wxSTAY_ON_TOP),
#endif
    pPlugIn(ppi), pParent(parent)
{
#ifdef __OCPN__ANDROID__
    GetHandle()->setStyleSheet( qtStyleSheet);
#endif
    m_cfgdlg = new ClimatologyConfigDialog(this);

    Now();

    m_cursorlat = m_cursorlon = 0;

    {
#include "now.xpm"
    m_bpNow->SetBitmapLabel(now);
    }
    DimeWindow( this );
    PopulateTrackingControls();

	// Fix Generated UI header (from wxFormBuilder or XRC) declares a control pointer
	// But generated UI (.cpp) does NOT actually create the control.
	// pointer exists in the class layout, but is never initialized.
	// Create the status label BEFORE using it
	m_tStatus = new wxStaticText(this, wxID_ANY, "Loading climatology data…");
	// Note: Move Below into wxFormBuilder so they are not lost.
	GetSizer()->Add(m_tStatus, 0, wxALL | wxALIGN_CENTER_HORIZONTAL, 5);

	// Note: Move Below into wxFormBuilder so they are not lost.
	#if 0
			// --- Fix narrow text fields and font issues ---
			wxFont f = wxFontInfo(10).Family(wxFONTFAMILY_MODERN);

			m_tWind->SetFont(f);
			m_tWindDir->SetFont(f);
			m_tCurrent->SetFont(f);
			m_tCurrentDir->SetFont(f);

			m_tPressure->SetFont(f);
			m_tSeaTemperature->SetFont(f);
			m_tAirTemperature->SetFont(f);
			m_tCloudCover->SetFont(f);
			m_tPrecipitation->SetFont(f);
			m_tRelativeHumidity->SetFont(f);
			m_tLightning->SetFont(f);
			m_tSeaDepth->SetFont(f);

			// Wider fields
			m_tWind->SetMinSize(wxSize(110, -1));
			m_tWindDir->SetMinSize(wxSize(140, -1));
			m_tCurrent->SetMinSize(wxSize(110, -1));
			m_tCurrentDir->SetMinSize(wxSize(140, -1));

			m_tPressure->SetMinSize(wxSize(110, -1));
			m_tSeaTemperature->SetMinSize(wxSize(110, -1));
			m_tAirTemperature->SetMinSize(wxSize(110, -1));
			m_tCloudCover->SetMinSize(wxSize(110, -1));
			m_tPrecipitation->SetMinSize(wxSize(110, -1));
			m_tRelativeHumidity->SetMinSize(wxSize(110, -1));
			m_tLightning->SetMinSize(wxSize(110, -1));
			m_tSeaDepth->SetMinSize(wxSize(110, -1));
	#endif
	
	// --- Auto-size text fields based on realistic maximum expected text ---
	wxClientDC dc(this);
	wxFont f = wxFontInfo(10).Family(wxFONTFAMILY_MODERN);
	dc.SetFont(f);

	// Realistic longest expected strings
	wxString sampleSpeed = "99.9 kt";        // wind/current speed
	wxString sampleDir   = "359° (NNW)";     // direction
	wxString sampleOther = "999.9";          // pressure, SST, etc.

	int wSpeed, h1;
	int wDir,   h2;
	int wOther, h3;

	dc.GetTextExtent(sampleSpeed, &wSpeed, &h1);
	dc.GetTextExtent(sampleDir,   &wDir,   &h2);
	dc.GetTextExtent(sampleOther, &wOther, &h3);

	int speedWidth = wSpeed + 10;
	int dirWidth   = wDir   + 10;
	int otherWidth = wOther + 10;

	auto setSpeed = [&](wxTextCtrl* t) {
		t->SetFont(f);
		t->SetMinSize(wxSize(speedWidth, -1));
	};

	auto setDir = [&](wxTextCtrl* t) {
		t->SetFont(f);
		t->SetMinSize(wxSize(dirWidth, -1));
	};

	auto setOther = [&](wxTextCtrl* t) {
		t->SetFont(f);
		t->SetMinSize(wxSize(otherWidth, -1));
	};

	// Apply to speed fields
	setSpeed(m_tWind);
	setSpeed(m_tCurrent);

	// Apply to direction fields
	setDir(m_tWindDir);
	setDir(m_tCurrentDir);

	// Apply to all other fields
	setOther(m_tPressure);
	setOther(m_tSeaTemperature);
	setOther(m_tAirTemperature);
	setOther(m_tCloudCover);
	setOther(m_tPrecipitation);
	setOther(m_tRelativeHumidity);
	setOther(m_tLightning);
	setOther(m_tSeaDepth);

	GetSizer()->Layout();
	Fit();

		
	// Disable controls until data loads	
	EnableAllControls(false);
	
	// Bind background cyclone loader completion event
    Bind(wxEVT_COMMAND_MENU_SELECTED,
         &ClimatologyDialog::OnCycloneReady,
         this,
         ID_CYCLONE_READY);

    // Start with cyclones disabled until background thread finishes
    m_cbCyclones->Enable(false);
    m_cbCyclones->SetValue(false);
	
	// Bind the progress event
	Bind(wxEVT_COMMAND_MENU_SELECTED,
     &ClimatologyDialog::OnCycloneProgress,
     this,
     ID_CYCLONE_PROGRESS);

	
    // run fit delayed (buggy wxwidgets)
    m_fittimer.Connect(wxEVT_TIMER, wxTimerEventHandler
                       ( ClimatologyDialog::OnFitTimer ), NULL, this);
					   
#ifdef __OCPN__ANDROID__
    GetHandle()->setAttribute(Qt::WA_AcceptTouchEvents);
    GetHandle()->grabGesture(Qt::PanGesture);
    GetHandle()->setStyleSheet( qtStyleSheet);
    Connect( wxEVT_QT_PANGESTURE,
                       (wxObjectEventFunction) (wxEventFunction) &ClimatologyDialog::OnEvtPanGesture, NULL, this );
#endif
}

bool ClimatologyDialog::Show(bool show)
{
    if (show == false) {
        pPlugIn->SendTimelineMessage( wxInvalidDateTime );
    }
    else if (g_pOverlayFactory != 0 && m_sTimeline != 0) {
        wxDateTime timeline = g_pOverlayFactory->m_CurrentTimeline;
        timeline.SetYear(wxDateTime::Now().GetYear() + ( m_sTimeline->GetValue() > 365?1:0));
        pPlugIn->SendTimelineMessage( timeline );
    }
    return ClimatologyDialogBase::Show(show);
}



void ClimatologyDialog::Save()
{
    if (m_cfgdlg) m_cfgdlg->Save();
}


ClimatologyDialog::~ClimatologyDialog()
{
}

#ifdef __OCPN__ANDROID__
void ClimatologyDialog::OnEvtPanGesture( wxQT_PanGestureEvent &event)
{
    switch(event.GetState()){
        case GestureStarted:
            m_startPos = GetPosition();
            m_startMouse = event.GetCursorPos(); //g_mouse_pos_screen;
            break;
        default:
        {
            wxPoint pos = event.GetCursorPos();
            int x = wxMax(0, pos.x + m_startPos.x - m_startMouse.x);
            int y = wxMax(0, pos.y + m_startPos.y - m_startMouse.y);
            int xmax = ::wxGetDisplaySize().x - GetSize().x;
            x = wxMin(x, xmax);
            int ymax = ::wxGetDisplaySize().y - GetSize().y;          // Some fluff at the bottom
            y = wxMin(y, ymax);

            Move(x, y);
        } break;
    }
}
#endif

void ClimatologyDialog::EnableAllControls(bool enable)
{
    // Month / Day / Timeline
    m_cMonth->Enable(enable);
    m_sDay->Enable(enable);
    m_sTimeline->Enable(enable);
    m_cbAll->Enable(enable);

    // Overlay checkboxes
    m_cbWind->Enable(enable);
    m_cbCurrent->Enable(enable);
    m_cbPressure->Enable(enable);
    m_cbSeaTemperature->Enable(enable);
    m_cbAirTemperature->Enable(enable);
    m_cbCloudCover->Enable(enable);
    m_cbPrecipitation->Enable(enable);
    m_cbRelativeHumidity->Enable(enable);
    m_cbLightning->Enable(enable);
    m_cbSeaDepth->Enable(enable);
    m_cbCyclones->Enable(enable);
}


void ClimatologyDialog::UpdateTrackingControls()
{
    if (!g_pOverlayFactory || !g_pOverlayFactory->m_bCompletedLoading)
        return;

    wxDateTime now = wxDateTime::Now();

    // --- SAFE GETTERS -------------------------------------------------------
    auto getMag = [&](int setting) {
        double v = g_pOverlayFactory->getValue(MAG, setting,
                                               m_cursorlat, m_cursorlon, &now);

        // HARD SANITIZATION FOR MAGNITUDE
        if (!wxFinite(v))
            return std::numeric_limits<double>::quiet_NaN();
        if (v < -1000 || v > 1000)   // impossible for wind/current/SST/etc.
            return std::numeric_limits<double>::quiet_NaN();

        return v;
    };

    auto getDir = [&](int setting) {
        double v = g_pOverlayFactory->getValue(DIRECTION, setting,
                                               m_cursorlat, m_cursorlon, &now);

        // HARD SANITIZATION FOR DIRECTION
        if (!wxFinite(v))
            return std::numeric_limits<double>::quiet_NaN();
        if (v < 0 || v > 360)
            return std::numeric_limits<double>::quiet_NaN();

        return v;
    };

    // --- WIND ---------------------------------------------------------------
    double wspd = getMag(ClimatologyOverlaySettings::WIND);
    double wdir = getDir(ClimatologyOverlaySettings::WIND);

    // --- CURRENT ------------------------------------------------------------
    double cspd = getMag(ClimatologyOverlaySettings::CURRENT);
    double cdir = getDir(ClimatologyOverlaySettings::CURRENT);

    // --- OTHER FIELDS -------------------------------------------------------
    double pres  = getMag(ClimatologyOverlaySettings::SLP);
    double sst   = getMag(ClimatologyOverlaySettings::SST);
    double at    = getMag(ClimatologyOverlaySettings::AT);
    double cloud = getMag(ClimatologyOverlaySettings::CLOUD);
    double rain  = getMag(ClimatologyOverlaySettings::PRECIPITATION);
    double rh    = getMag(ClimatologyOverlaySettings::RELATIVE_HUMIDITY);
    double ltg   = getMag(ClimatologyOverlaySettings::LIGHTNING);
    double depth = getMag(ClimatologyOverlaySettings::SEADEPTH);

    // --- UNIT SELECTION -----------------------------------------------------
    int windUnit    = m_cfgdlg->m_Settings.Settings[ClimatologyOverlaySettings::WIND].m_Units;
    int currentUnit = m_cfgdlg->m_Settings.Settings[ClimatologyOverlaySettings::CURRENT].m_Units;

    // --- CLEAR FIELDS BEFORE WRITING (prevents leftover bytes) -------------
    m_tWind->Clear();
    m_tWindDir->Clear();
    m_tCurrent->Clear();
    m_tCurrentDir->Clear();
    m_tPressure->Clear();
    m_tSeaTemperature->Clear();
    m_tAirTemperature->Clear();
    m_tCloudCover->Clear();
    m_tPrecipitation->Clear();
    m_tRelativeHumidity->Clear();
    m_tLightning->Clear();
    m_tSeaDepth->Clear();

    // --- UPDATE WIND/CURRENT ------------------------------------------------
    m_tWind->SetValue(FormatSpeed(wspd, windUnit));
    m_tWindDir->SetValue(FormatDirection(wdir));

    m_tCurrent->SetValue(FormatSpeed(cspd, currentUnit, true));
    m_tCurrentDir->SetValue(FormatDirection(cdir));

    // --- COLOR CODING -------------------------------------------------------
    m_tWind->SetForegroundColour(ValueColor(wspd, 10, 20));
    m_tWindDir->SetForegroundColour(wxColour(0,0,0));

    m_tCurrent->SetForegroundColour(ValueColor(cspd, 1.0, 2.0));
    m_tCurrentDir->SetForegroundColour(wxColour(0,0,0));

    m_tPressure->SetForegroundColour(ValueColor(pres, 1000, 1020));
    m_tSeaTemperature->SetForegroundColour(ValueColor(sst, 10, 28)); // Celsius thresholds
    m_tAirTemperature->SetForegroundColour(ValueColor(at, 0, 30));
    m_tCloudCover->SetForegroundColour(ValueColor(cloud, 30, 70));
    m_tPrecipitation->SetForegroundColour(ValueColor(rain, 1, 10));
    m_tRelativeHumidity->SetForegroundColour(ValueColor(rh, 40, 80));
    m_tLightning->SetForegroundColour(ValueColor(ltg, 1, 10));
    m_tSeaDepth->SetForegroundColour(ValueColor(depth, 10, 50));

    // --- UPDATE OTHER FIELDS ------------------------------------------------
    m_tPressure->SetValue(wxFinite(pres)  ? wxString::Format("%.1f hPa", pres) : "—");

    m_tSeaTemperature->SetValue(wxFinite(sst)
        ? wxString::Format("%.1f °C", sst)
        : "—");

    m_tAirTemperature->SetValue(wxFinite(at) ? wxString::Format("%.1f °C", at) : "—");
    m_tCloudCover->SetValue(wxFinite(cloud) ? wxString::Format("%.0f %%", cloud) : "—");
    m_tPrecipitation->SetValue(wxFinite(rain) ? wxString::Format("%.2f mm", rain) : "—");
    m_tRelativeHumidity->SetValue(wxFinite(rh) ? wxString::Format("%.0f %%", rh) : "—");
    m_tLightning->SetValue(wxFinite(ltg) ? wxString::Format("%.0f", ltg) : "—");
    m_tSeaDepth->SetValue(wxFinite(depth) ? wxString::Format("%.1f m", depth) : "—");
}



void ClimatologyDialog::PopulateTrackingControls()
{
    SetControlsVisible(ClimatologyOverlaySettings::WIND, m_cbWind, m_tWind, m_tWindDir);
    SetControlsVisible(ClimatologyOverlaySettings::CURRENT, m_cbCurrent, m_tCurrent, m_tCurrentDir);
    bool windorcurrent =
        m_cfgdlg->m_Settings.Settings[ClimatologyOverlaySettings::WIND].m_bEnabled
     || m_cfgdlg->m_Settings.Settings[ClimatologyOverlaySettings::CURRENT].m_bEnabled;
    m_stSpeed->Show(windorcurrent);
    m_stDirection->Show(windorcurrent);

    SetControlsVisible(ClimatologyOverlaySettings::SLP, m_cbPressure, m_tPressure);
    SetControlsVisible(ClimatologyOverlaySettings::SST, m_cbSeaTemperature, m_tSeaTemperature);
    SetControlsVisible(ClimatologyOverlaySettings::AT, m_cbAirTemperature, m_tAirTemperature);
    SetControlsVisible(ClimatologyOverlaySettings::CLOUD, m_cbCloudCover, m_tCloudCover);
    SetControlsVisible(ClimatologyOverlaySettings::PRECIPITATION, m_cbPrecipitation, m_tPrecipitation);
    SetControlsVisible(ClimatologyOverlaySettings::RELATIVE_HUMIDITY, m_cbRelativeHumidity, m_tRelativeHumidity);
    SetControlsVisible(ClimatologyOverlaySettings::LIGHTNING, m_cbLightning, m_tLightning);
    SetControlsVisible(ClimatologyOverlaySettings::SEADEPTH, m_cbSeaDepth, m_tSeaDepth);

    Refresh();
    Fit();
}

void ClimatologyDialog::RefreshRedraw()
{
    RequestRefresh( pParent );
}

void  ClimatologyDialog::SetCursorLatLon(double lat, double lon)
{
    m_cursorlat = lat;
    m_cursorlon = lon;
	
	if (!g_pOverlayFactory || !g_pOverlayFactory->m_bCompletedLoading)
        return;
	
	wxLogMessage("Climatology: SetCursorLatLon dialog: lat=%f lon=%f factory=%p completed=%d",
             lat, lon, g_pOverlayFactory, g_pOverlayFactory ? g_pOverlayFactory->m_bCompletedLoading : -1);

    UpdateTrackingControls();
}

bool ClimatologyDialog::SettingEnabled(int setting)
{
    return GetSettingControl(setting)->GetValue();
}

void ClimatologyDialog::DisableSetting(int setting)
{
    GetSettingControl(setting)->SetValue(false);
}

wxCheckBox *ClimatologyDialog::GetSettingControl(int setting)
{
    switch(setting) {
    case ClimatologyOverlaySettings::WIND:          return m_cbWind;
    case ClimatologyOverlaySettings::CURRENT:       return m_cbCurrent;
    case ClimatologyOverlaySettings::SLP:           return m_cbPressure;
    case ClimatologyOverlaySettings::SST:           return m_cbSeaTemperature;
    case ClimatologyOverlaySettings::AT:            return m_cbAirTemperature;
    case ClimatologyOverlaySettings::CLOUD:         return m_cbCloudCover;
    case ClimatologyOverlaySettings::PRECIPITATION: return m_cbPrecipitation;
    case ClimatologyOverlaySettings::RELATIVE_HUMIDITY:      return m_cbRelativeHumidity;
    case ClimatologyOverlaySettings::LIGHTNING:     return m_cbLightning;
    case ClimatologyOverlaySettings::SEADEPTH:      return m_cbSeaDepth;
    }
    return NULL;
}

void ClimatologyDialog::SetControlsVisible(ClimatologyOverlaySettings::SettingsType type,
                                           wxControl *ctrl1, wxControl *ctrl2, wxControl *ctrl3)
{
    if(m_cfgdlg->m_Settings.Settings[type].m_bEnabled) {
        ctrl1->Show();
        if(ctrl2) ctrl2->Show();
        if(ctrl3) ctrl3->Show();
    } else {
        ctrl1->Hide();
        if(ctrl2) ctrl2->Hide();
        if(ctrl3) ctrl3->Hide();
    }
}

wxString ClimatologyDialog::GetValue(int index, Coord coord)
{
    if (!g_pOverlayFactory || !g_pOverlayFactory->m_bCompletedLoading)
        return "";

    int month = (int)g_pOverlayFactory->m_CurrentTimeline.GetMonth();

    double val = g_pOverlayFactory->getCalibratedValueMonth(
        coord,          // FIRST argument: Coord
        index,          // SECOND argument: setting index
        m_cursorlat,
        m_cursorlon,
        month           // matches signature
    );

    if (isnan(val))
        return "";

    return wxString::Format("%.2f", val);
}



void ClimatologyDialog::DayMonthUpdate()
{
    wxDateTime &timeline = g_pOverlayFactory->m_CurrentTimeline;
    int nMonthDays = wxDateTime::GetNumberOfDays((wxDateTime::Month)m_cMonth->GetSelection(),
                                                    1999); // not a leap year
    if( m_sDay->GetValue() > nMonthDays)
        timeline.SetDay(1);
    else
        timeline.SetDay(m_sDay->GetValue());

    m_sDay->SetRange(1, nMonthDays);

    timeline.SetMonth((wxDateTime::Month)m_cMonth->GetSelection());

    int yearday = g_pOverlayFactory->m_CurrentTimeline.GetDayOfYear();

//  OR
//  Sean add line just below back and changed int yearday commit 15d4101
//    timeline.SetDay(0);
//    timeline.SetMonth((wxDateTime::Month)m_cMonth->GetSelection());
//    timeline.SetDay(m_sDay->GetValue());
//    int yearday = timeline.GetDayOfYear();

    if(yearday < 67) {
        yearday += 365;
    }
    m_sTimeline->SetValue(yearday);

    UpdateTrackingControls();
    wxDateTime now = timeline;
    now.SetYear(wxDateTime::Now().GetYear() + ( yearday > 365?1:0));
    pPlugIn->SendTimelineMessage(now);
    RefreshRedraw();
}

void ClimatologyDialog::OnTimeline( wxScrollEvent& event )
{
    wxDateTime &timeline = g_pOverlayFactory->m_CurrentTimeline;
    wxDateTime old = timeline;

    timeline.SetToYearDay((event.GetPosition() - 1) % 365 + 1);
    m_cMonth->SetSelection(timeline.GetMonth());


    m_sDay->SetRange(1, wxDateTime::GetNumberOfDays(timeline.GetMonth(),
                                                    1999)); // not a leap year
    m_sDay->SetValue(timeline.GetDay());

    if (old.IsSameDate(timeline))
        return;
    UpdateTrackingControls();
    wxDateTime now = timeline;
    now.SetYear(wxDateTime::Now().GetYear()+ ( event.GetPosition() > 365?1:0));
    pPlugIn->SendTimelineMessage(now);
    RefreshRedraw();
}

void ClimatologyDialog::OnTimelineDown( wxScrollEvent& event )
{
    if(event.GetPosition() >= 432)
        m_sTimeline->SetValue(event.GetPosition() - 365);
}

void ClimatologyDialog::OnTimelineUp( wxScrollEvent& event )
{
    if(event.GetPosition() <= 67)
        m_sTimeline->SetValue(event.GetPosition() + 365);
}

void ClimatologyDialog::OnAll( wxCommandEvent& event )
{
    m_cMonth->Enable(!m_cbAll->GetValue());
    m_sDay->Enable(!m_cbAll->GetValue());
    m_sTimeline->Enable(!m_cbAll->GetValue());

    g_pOverlayFactory->m_bAllTimes = event.IsChecked();

    UpdateTrackingControls();
    RefreshRedraw();
}

void ClimatologyDialog::OnNow( wxCommandEvent& event )
{
    Now();
    RefreshRedraw();
}

void ClimatologyDialog::OnUpdateDisplay( wxCommandEvent& event )
{
    if(event.IsChecked()) {
        int cursetting = 0;
        for(int i=0; i<ClimatologyOverlaySettings::SETTINGS_COUNT; i++)
            if(wxDynamicCast(event.GetEventObject(), wxCheckBox) == GetSettingControl(i)) {
                m_cfgdlg->m_cDataType->SetSelection(i);
                m_cfgdlg->OnDataTypeChoice( event );
                cursetting = i;
                break;
            }

        if(m_cfgdlg->m_Settings.Settings[cursetting].m_bOverlayMap)
            for(int i=0; i<ClimatologyOverlaySettings::SETTINGS_COUNT; i++) {
                if(i == cursetting)
                    continue;
                ClimatologyOverlaySettings::OverlayDataSettings &odc = m_cfgdlg->m_Settings.Settings[i];
                if(SettingEnabled(i) && odc.m_bOverlayMap)
                    DisableSetting(i);
            }
    }

    RefreshRedraw();
}

void ClimatologyDialog::OnConfig( wxCommandEvent& event )
{
    m_cfgdlg->Show(!m_cfgdlg->IsShown());
}

void ClimatologyDialog::OnClose( wxCloseEvent& event )
{
    pPlugIn->OnClimatologyDialogClose();
}

void ClimatologyDialog::OnCBAny( wxCommandEvent& event )
{
    RefreshRedraw();   // Reload the visibility options
}


void ClimatologyDialog::OnCycloneReady(wxCommandEvent& event)
{
    wxLogMessage("Climatology: Cyclone data ready — enabling checkbox");

    if (m_tStatus)
        m_tStatus->SetLabel("Cyclone data ready.");
		m_tStatus->SetForegroundColour(*wxGREEN);

    if (m_cbCyclones) {
        m_cbCyclones->Enable(true);
        m_cbCyclones->SetValue(true);
    }

    UpdateTrackingControls();
}


void ClimatologyDialog::OnCycloneProgress(wxCommandEvent& event)
{
    wxString msg = event.GetString();
    if (m_tStatus)
        m_tStatus->SetLabel(msg);
		m_tStatus->SetForegroundColour(*wxBLUE);
}

void ClimatologyDialog::Now()
{
    wxDateTime now = wxDateTime::Now();

    m_cMonth->SetSelection(now.GetMonth());
    m_sDay->SetValue(now.GetDay());

    int day = now.GetDayOfYear();
    if(g_pOverlayFactory) {
        wxDateTime &timeline = g_pOverlayFactory->m_CurrentTimeline;
        timeline.SetToYearDay(day + 1);
    }

    if(day <= 67)
        day += 365;
    m_sTimeline->SetValue(day);
    pPlugIn->SendTimelineMessage(now);
	
    if (!g_pOverlayFactory || !g_pOverlayFactory->m_bCompletedLoading)
        return;
	
    UpdateTrackingControls();
}
