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

    Now();

    m_cursorlat = m_cursorlon = 0;

    {
#include "now.xpm"
    m_bpNow->SetBitmapLabel(now);
    }
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

climatology::ClimatologyRenderState
ClimatologyDialog::CaptureRenderState() const
{
    climatology::ClimatologyRenderState state;
    wxCheckBox *field_controls[] = {
        m_cbWind, m_cbCurrent, m_cbPressure, m_cbSeaTemperature,
        m_cbAirTemperature, m_cbCloudCover, m_cbPrecipitation,
        m_cbRelativeHumidity, m_cbLightning, m_cbSeaDepth};
    for(std::size_t index = 0; index < climatology::kDisplayFieldCount;
        ++index) {
        const ClimatologyOverlaySettings::OverlayDataSettings &source =
            m_cfgdlg->m_Settings.Settings[index];
        climatology::FieldRenderState &field = state.fields[index];
        field.visible = field_controls[index]->GetValue();
        field.enabled = source.m_bEnabled;
        field.units = source.m_Units;
        field.overlay_map = source.m_bOverlayMap;
        field.overlay_transparency = source.m_iOverlayTransparency;
        field.overlay_interpolation = source.m_bOverlayInterpolation;
        field.isobars = source.m_bIsoBars;
        field.isobar_spacing = source.m_iIsoBarSpacing;
        field.isobar_step = source.m_iIsoBarStep;
        field.numbers = source.m_bNumbers;
        field.numbers_spacing = source.m_iNumbersSpacing;
        field.direction_arrows = source.m_bDirectionArrows;
        field.arrow_length_type = source.m_iDirectionArrowsLengthType;
        field.arrow_width = source.m_iDirectionArrowsWidth;
        field.arrow_red = source.m_cDirectionArrowsColor.Red();
        field.arrow_green = source.m_cDirectionArrowsColor.Green();
        field.arrow_blue = source.m_cDirectionArrowsColor.Blue();
        field.arrow_alpha = source.m_cDirectionArrowsColor.Alpha();
        field.arrow_size = source.m_iDirectionArrowsSize;
        field.arrow_spacing = source.m_iDirectionArrowsSpacing;
    }
    state.all_times = m_cbAll->GetValue();
    state.show_wind_atlas = m_cbWind->GetValue();
    state.show_cyclones = m_cbCyclones->GetValue();
    state.wind_atlas.enabled = m_cfgdlg->m_cbWindAtlasEnable->GetValue();
    state.wind_atlas.size = m_cfgdlg->m_sWindAtlasSize->GetValue();
    state.wind_atlas.spacing = m_cfgdlg->m_sWindAtlasSpacing->GetValue();
    state.wind_atlas.opacity = m_cfgdlg->m_sWindAtlasOpacity->GetValue();

    climatology::CycloneFilterState &cyclones = state.cyclone_filter;
    cyclones.tropical = m_cfgdlg->m_cbTropical->GetValue();
    cyclones.subtropical = m_cfgdlg->m_cbSubTropical->GetValue();
    cyclones.extratropical = m_cfgdlg->m_cbExtraTropical->GetValue();
    cyclones.remnant = m_cfgdlg->m_cbRemanent->GetValue();
    cyclones.minimum_wind_knots = m_cfgdlg->m_sMinWindSpeed->GetValue();
    cyclones.maximum_pressure_hpa = m_cfgdlg->m_sMaxPressure->GetValue();
    cyclones.start_utc = m_cfgdlg->m_dPStart->GetValue().GetTicks();
    cyclones.end_utc = m_cfgdlg->m_dPEnd->GetValue().GetTicks();
    cyclones.include_enso_unavailable =
        m_cfgdlg->m_cbNotAvailable->GetValue();
    cyclones.include_el_nino = m_cfgdlg->m_cbElNino->GetValue();
    cyclones.include_la_nina = m_cfgdlg->m_cbLaNina->GetValue();
    cyclones.include_neutral = m_cfgdlg->m_cbNeutral->GetValue();
    cyclones.day_span = m_cfgdlg->m_sCycloneDaySpan->GetValue();
    return state;
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

void ClimatologyDialog::DisableIsoBars(int setting)
{
    m_cfgdlg->DisableIsoBars(setting);
    RefreshRedraw();
}

void ClimatologyDialog::DisableCyclonesForPerformance()
{
    m_cbCyclones->SetValue(false);
    RefreshRedraw();
    wxMessageDialog dialog(
        this, _("Computer too slow to render cyclones, disabling theater"),
        _("Climatology"), wxOK | wxICON_WARNING);
    dialog.ShowModal();
}

void ClimatologyDialog::ApplyDatasetAvailability(
    const climatology::DatasetAvailability &availability)
{
    wxCheckBox *controls[] = {
        m_cbWind, m_cbCurrent, m_cbPressure, m_cbSeaTemperature,
        m_cbAirTemperature, m_cbCloudCover, m_cbPrecipitation,
        m_cbRelativeHumidity, m_cbLightning, m_cbSeaDepth};
    for(std::size_t field = 0;
        field < static_cast<std::size_t>(climatology::DatasetField::Count);
        ++field)
        controls[field]->Enable(availability.fields[field]);
    m_cbCyclones->Enable(availability.cyclones);
    m_cfgdlg->m_cbElNino->Enable(availability.enso);
    m_cfgdlg->m_cbLaNina->Enable(availability.enso);
    m_cfgdlg->m_cbNeutral->Enable(availability.enso);
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
//    pPlugIn->GetOverlayFactory()->m_bUpdateCyclones = true;
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
//    pPlugIn->GetOverlayFactory()->m_bUpdateCyclones = true;
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
//    g_pOverlayFactory->m_bUpdateCyclones = true;
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
    RefreshRedraw();                     // Reload the visibility options
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
    UpdateTrackingControls();
}
