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

    m_cursorlat = m_cursorlon = 0;
    
    DimeWindow( this );
    PopulateTrackingControls();

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

void ClimatologyDialog::UpdateTrackingControls()
{
    if(!g_pOverlayFactory || !IsShown())
        return;

    m_tWind->SetValue(GetValue(ClimatologyOverlaySettings::WIND));
    m_tWindDir->SetValue(GetValue(ClimatologyOverlaySettings::WIND, DIRECTION));
    m_tCurrent->SetValue(GetValue(ClimatologyOverlaySettings::CURRENT));
    m_tCurrentDir->SetValue(GetValue(ClimatologyOverlaySettings::CURRENT, DIRECTION));
    m_tPressure->SetValue(GetValue(ClimatologyOverlaySettings::SLP));
    m_tSeaTemperature->SetValue(GetValue(ClimatologyOverlaySettings::SST));
    m_tAirTemperature->SetValue(GetValue(ClimatologyOverlaySettings::AT));
    m_tCloudCover->SetValue(GetValue(ClimatologyOverlaySettings::CLOUD));
    m_tPrecipitation->SetValue(GetValue(ClimatologyOverlaySettings::PRECIPITATION));
    m_tRelativeHumidity->SetValue(GetValue(ClimatologyOverlaySettings::RELATIVE_HUMIDITY));
    m_tLightning->SetValue(GetValue(ClimatologyOverlaySettings::LIGHTNING));
    m_tSeaDepth->SetValue(GetValue(ClimatologyOverlaySettings::SEADEPTH));
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
    double val = g_pOverlayFactory->getCurCalibratedValue
        (coord, index, m_cursorlat, m_cursorlon);
    if(isnan(val))
        return "";
    return wxString::Format("%.2f", val);
}



void ClimatologyDialog::OnAll( wxCommandEvent& event )
{
    // Timeline controls are now managed by the global timeline widget

    g_pOverlayFactory->m_bAllTimes = event.IsChecked();

    UpdateTrackingControls();
//    g_pOverlayFactory->m_bUpdateCyclones = true;
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
    RefreshRedraw();                     // Reload the visibility options
}



void ClimatologyDialog::UpdateFromTimeline(const wxDateTime &selectedTime)
{
    if(!selectedTime.IsValid())
        return;
        
    // Update the internal timeline to match the selected time
    int day = selectedTime.GetDayOfYear();
    if(g_pOverlayFactory) {
        wxDateTime &timeline = g_pOverlayFactory->m_CurrentTimeline;
        timeline.SetToYearDay(day);
    }
    
    // Update tracking controls and refresh
    UpdateTrackingControls();
    RefreshRedraw();
}
