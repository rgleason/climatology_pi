/******************************************************************************
 *
 * Project:  OpenCPN
 * Purpose:  Isobars
 * Author:   Sean D'Epagnier
 *
 ***************************************************************************
 *   Copyright (C) 2014 by Sean D'Epagnier   *
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
 
#pragma once
 
#include "ocpn_plugin.h"
#include "IsoBarMap.h"

#include <GL/glew.h>
#include <wx/glcanvas.h>
#include <wx/progdlg.h>
#include <cmath>

#include "ClimatologyEnums.h"
#include "ClimatologyOverlayFactory.h"
#include "ClimatologyRenderParams.h"

// ------------------------------------------------------------
// Parameter cache helpers
// ------------------------------------------------------------

void ParamCache::Initialize(double step)
{
    if (m_step != step) {
        m_step = step;
        delete [] values;
        int size = 360 / step;
        values = new double[size];
    }
    m_lat = 100; // invalidate
}

bool ParamCache::Read(double lat, double lon, double& value)
{
    if (lat != m_lat)
        return false;

    lon += 180;
    if (lon > 360) lon -= 360;
    if (lon < 0 || lon >= 360)
        return false;

    double div = lon / m_step;
    if (div != floor(div))
        return false;

    value = values[(int)div];
    return true;
}


// missing helper from original code
// static inline double square(double x)
//{
//    return x * x;
//}


void IsoBarMap::BuildParamCache(ParamCache& cache, double lat)
{
    int i = 0;
    for (double lon = -180; lon < 180; lon += m_Step, i++)
        cache.values[i] = CalcParameter(lat, lon);

    cache.m_lat = lat;
}


// ------------------------------------------------------------
// Constructor / destructor
// ------------------------------------------------------------

IsoBarMap::IsoBarMap(wxString name,
                     double spacing,
                     double step,
                     const ClimatologyOverlayFactory& factory,
                     int overlayType,
                     const ClimatologyRenderParams& renderParams)
    : m_bNeedsRecompute(false),
      m_bComputing(false),
      m_Spacing(spacing),
      m_Step(step),
      m_PoleAccuracy(1e-4),
      m_MinContour(NAN),
      m_MaxContour(NAN),
      m_contourcachesize(0),
      m_contourcache(nullptr),
      lastx(0),
      lasty(0),
      m_Name(name),
      m_bPolar(false),
      m_Color(*wxBLACK),
      m_factory(factory),
      m_setting(overlayType),          // <- any overlay type, not just pressure
      m_renderParams(renderParams)
{
}




IsoBarMap::~IsoBarMap()
{
    ClearMap();
}

// ------------------------------------------------------------
// Unified-grid parameter lookup
// ------------------------------------------------------------

double IsoBarMap::Parameter(double lat, double lon)
{
    double ret = m_factory.getCurValue(
        SCALAR,
        m_setting,        // <- overlayType passed in constructor
        lat,
        lon,
        m_renderParams
    );

    if (std::isnan(ret))
        return NAN;

    if (std::isnan(m_MinContour) || ret < m_MinContour)
        m_MinContour = ret;

    if (std::isnan(m_MaxContour) || ret > m_MaxContour)
        m_MaxContour = ret;

    return ret;
}


double IsoBarMap::CachedParameter(double lat, double lon)
{
    double value;
    if (!m_Cache[0].Read(lat, lon, value) &&
        !m_Cache[1].Read(lat, lon, value))
    {
        value = CalcParameter(lat, lon);
    }
    return value;
}

// ------------------------------------------------------------
// Interpolation logic (unchanged)
// ------------------------------------------------------------

bool IsoBarMap::Interpolate(double x1, double x2, double y1, double y2,
                            bool lat, double lonval,
                            double& rx, double& ry)
{
    if (fabs(x1 - x2) < m_PoleAccuracy) {
        rx = NAN;
        return true;
    }

    if (m_bPolar) {
        if (y1 - y2 > 180) y2 += 360;
        if (y2 - y1 > 180) y1 += 360;
    }

    y1 /= m_Spacing;
    y2 /= m_Spacing;

    double fy1 = floor(y1), fy2 = floor(y2);
    if (fy1 == fy2) {
        rx = NAN;
        return true;
    }

    if (fabs(fy1 - fy2) > 1)
        return false;

    if (y1 > y2) {
        std::swap(y1, y2);
        std::swap(x1, x2);
    }

    ry = floor(y2);

    for (;;) {
        rx = (x1*(y2-ry) - x2*(y1-ry)) / (y2 - y1);

        if (fabs(x1 - x2) < m_PoleAccuracy)
            return true;

        double p = lat ? Parameter(rx, lonval)
                       : Parameter(lonval, rx);

        if (std::isnan(p))
            return true;

        if (m_bPolar && p - ry*m_Spacing < -180)
            p += 360;

        p /= m_Spacing;
        double err = p - ry;

        if (fabs(err) < 1e-5 || p == y1 || p == y2)
            return true;

        if (err < 0) {
            if (p < y1)
                return false;
            x1 = rx;
            y1 = p;
        } else {
            if (p > y2)
                return false;
            x2 = rx;
            y2 = p;
        }
    }
}

// ------------------------------------------------------------
// PlotRegion (unchanged except unified Parameter())
// ------------------------------------------------------------

void IsoBarMap::PlotRegion(std::list<PlotLineSeg>& region,
                           double lat1, double lon1,
                           double lat2, double lon2,
                           int maxdepth)
{
    if (maxdepth == 0)
        return;

    double p1 = CachedParameter(lat1, lon1);
    double p2 = CachedParameter(lat1, lon2);
    double p3 = CachedParameter(lat2, lon1);
    double p4 = CachedParameter(lat2, lon2);

    if (std::isnan(p1) || std::isnan(p2) ||
        std::isnan(p3) || std::isnan(p4))
        return;

    double ry1, ry2, ry3, ry4;
    double lon3, lon4, lat3, lat4;

    if (!Interpolate(lon1, lon2, p1, p2, false, lat1, lon3, ry1) ||
        !Interpolate(lon1, lon2, p3, p4, false, lat2, lon4, ry2))
    {
        double mid = (lon1 + lon2) / 2;
        PlotRegion(region, lat1, lon1, lat2, mid, maxdepth - 1);
        PlotRegion(region, lat1, mid, lat2, lon2, maxdepth - 1);
        return;
    }

    if (!Interpolate(lat1, lat2, p1, p3, true, lon1, lat3, ry3) ||
        !Interpolate(lat1, lat2, p2, p4, true, lon2, lat4, ry4))
    {
        double mid = (lat1 + lat2) / 2;
        PlotRegion(region, lat1, lon1, mid, lon2, maxdepth - 1);
        PlotRegion(region, mid, lon1, lat2, lon2, maxdepth - 1);
        return;
    }

    ry1 *= m_Spacing;
    ry2 *= m_Spacing;
    ry3 *= m_Spacing;
    ry4 *= m_Spacing;

    // Determine which edges produce contour segments
    int mask = ((std::isnan(lat4)*2 + std::isnan(lat3))*2 +
                std::isnan(lon4))*2 + std::isnan(lon3);

    switch (mask) {
    case 3:
        region.emplace_back(lat3, lon1, lat4, lon2, ry3);
        break;
    case 5:
        region.emplace_back(lat2, lon4, lat4, lon2, ry2);
        break;
    case 6:
        region.emplace_back(lat1, lon3, lat4, lon2, ry1);
        break;
    case 9:
        region.emplace_back(lat3, lon1, lat2, lon4, ry2);
        break;
    case 10:
        region.emplace_back(lat3, lon1, lat1, lon3, ry1);
        break;
    case 12:
        region.emplace_back(lat1, lon3, lat2, lon4, ry1);
        break;
    case 0:
		double midLat, midLon;
		midLat = (lat1 + lat2) / 2;
		midLon = (lon1 + lon2) / 2;
        PlotRegion(region, lat1, lon1, midLat, midLon, maxdepth - 1);
        PlotRegion(region, lat1, midLon, midLat, lon2, maxdepth - 1);
        PlotRegion(region, midLat, lon1, lat2, midLon, maxdepth - 1);
        PlotRegion(region, midLat, midLon, lat2, lon2, maxdepth - 1);
        break;
    default:
        break;
    }
}

// ------------------------------------------------------------
// Recompute (unchanged except unified Parameter())
// ------------------------------------------------------------

bool IsoBarMap::Recompute(wxWindow* parent)
{
    ClearMap();
    m_bComputing = true;

    wxProgressDialog* progressdialog = nullptr;
    wxDateTime start = wxDateTime::Now();

    int cachepage = 0;
    m_Cache[0].Initialize(m_Step);
    m_Cache[1].Initialize(m_Step);

    double min = -MAX_LAT, max = MAX_LAT;

    BuildParamCache(m_Cache[cachepage], min);

    for (double lat = min; lat + m_Step <= max; lat += m_Step)
    {
        if (m_bNeedsRecompute) {
            if (progressdialog) progressdialog->Destroy();
            return false;
        }

        wxDateTime now = wxDateTime::Now();
        if (progressdialog) {
            if ((now - start).GetMilliseconds() > 1200) {
                if (!progressdialog->Update(lat - min)) {
                    progressdialog->Destroy();
                    return false;
                }
                start = now;
            }
        } else if ((now - start).GetMilliseconds() > 500) {
            if (lat < (max - min) / 2) {
                progressdialog = new wxProgressDialog(
                    _("Building Isobar Map"),
                    m_Name,
                    max - min + 1,
                    parent,
                    wxPD_ELAPSED_TIME | wxPD_CAN_ABORT
                );
                progressdialog->Update(0);
            }
        }

        cachepage = !cachepage;
        BuildParamCache(m_Cache[cachepage], lat + m_Step);

        int latind = floor((lat + MAX_LAT) / ZONE_SIZE);
        if (latind > LATITUDE_ZONES - 1)
            latind = LATITUDE_ZONES - 1;

        for (double lon = -180; lon + m_Step <= 180; lon += m_Step)
        {
            int lonind = floor((lon + 180) / ZONE_SIZE);
            PlotRegion(m_map[latind][lonind],
                       lat, lon,
                       lat + m_Step, lon + m_Step,
                       4);
        }
    }

    if (progressdialog) progressdialog->Destroy();

    m_bComputing = false;

    m_MinContour = floor(m_MinContour / m_Spacing) * m_Spacing;
    m_MaxContour = ceil(m_MaxContour / m_Spacing) * m_Spacing;

    if (std::isnan(m_MaxContour) || std::isnan(m_MinContour))
        m_contourcachesize = 0;
    else
        m_contourcachesize = (m_MaxContour - m_MinContour) / m_Spacing + 1;

    delete [] m_contourcache;
    m_contourcache = new ContourText[m_contourcachesize];

    for (int i = 0; i < m_contourcachesize; i++)
        m_contourcache[i] = ContourCacheData(m_MinContour + i*m_Spacing);

    return true;
}

// ------------------------------------------------------------
// Rendering helpers (unchanged except unified data)
// ------------------------------------------------------------


static void DrawLineSegGL(PlugIn_ViewPort& VP,
                          double lat1, double lon1,
                          double lat2, double lon2)
{
    wxPoint r1, r2;
    GetCanvasPixLL(&VP, &r1, lat1, lon1);
    GetCanvasPixLL(&VP, &r2, lat2, lon2);

    glBegin(GL_LINES);
    glVertex2f(r1.x, r1.y);
    glVertex2f(r2.x, r2.y);
    glEnd();
}

void DrawLineSeg(piDC* dc, PlugIn_ViewPort& VP,
                 double lat1, double lon1,
                 double lat2, double lon2)
{
    wxPoint r1, r2;
    GetCanvasPixLL(&VP, &r1, lat1, lon1);
    GetCanvasPixLL(&VP, &r2, lat2, lon2);

    if (dc && dc->GetDC())
        dc->GetDC()->DrawLine(r1.x, r1.y, r2.x, r2.y);
}

void IsoBarMap::ClearMap()
{
    for (int latind = 0; latind < LATITUDE_ZONES; latind++)
        for (int lonind = 0; lonind < LONGITUDE_ZONES; lonind++)
            m_map[latind][lonind].clear();

    delete [] m_contourcache;
    m_contourcache = nullptr;

    m_MinContour = m_MaxContour = NAN;
    m_contourcachesize = 0;
}

ContourText IsoBarMap::ContourCacheData(double value)
{
    ContourText t;
    t.text.Printf("%.0f", value);
    t.w = t.h = 0;
    t.lastx = 0;
    t.lasty = 0;
    return t;
}

void IsoBarMap::DrawContour(piDC *dc, PlugIn_ViewPort& VP,
                            double contour,
                            double lat, double lon)
{
    int index = (contour - m_MinContour) / m_Spacing;
    if (index < 0 || index >= m_contourcachesize)
        return;

    wxPoint r;
    GetCanvasPixLL(&VP, &r, lat, lon);

    ContourText& ct = m_contourcache[index];

    double dist1 = square(r.x - ct.lastx) + square(r.y - ct.lasty);
    double dist2 = square(r.x - lastx) + square(r.y - lasty);

    if (dist1 < 100000 || dist2 < 40000)
        return;

    ct.lastx = lastx = r.x;
    ct.lasty = lasty = r.y;

    wxDC* wdc = dc ? dc->GetDC() : nullptr;
    if (!wdc)
        return;

    if (ct.w == 0)
        wdc->GetTextExtent(ct.text, &ct.w, &ct.h);

    wdc->DrawText(ct.text, r.x - ct.w/2, r.y - ct.h/2);
}

// ------------------------------------------------------------
// GL rendering
// ------------------------------------------------------------

void IsoBarMap::PlotGL(PlugIn_ViewPort& vp)
{
    glLineWidth(3.0f);
    glColor4ub(m_Color.Red(), m_Color.Green(), m_Color.Blue(), m_Color.Alpha());

    int startlatind = floor((vp.lat_min + MAX_LAT) / ZONE_SIZE);
    if (startlatind < 0) startlatind = 0;

    int endlatind = floor((vp.lat_max + MAX_LAT) / ZONE_SIZE);
    if (endlatind > LATITUDE_ZONES - 1) endlatind = LATITUDE_ZONES - 1;

    double lon_min = vp.lon_min;
    if (lon_min < -180) lon_min += 360;
    else if (lon_min >= 180) lon_min -= 360;

    int startlonind = floor((lon_min + 180) / ZONE_SIZE);
    if (startlonind < 0) startlonind = LONGITUDE_ZONES - 1;
    if (startlonind > LONGITUDE_ZONES - 1) startlonind = 0;

    double lon_max = vp.lon_max;
    if (lon_max < -180) lon_max += 360;
    else if (lon_max >= 180) lon_max -= 360;

    int endlonind = floor((lon_max + 180) / ZONE_SIZE);
    if (endlonind < 0) endlonind = LONGITUDE_ZONES - 1;
    if (endlonind > LONGITUDE_ZONES - 1) endlonind = 0;

    for (int latind = startlatind; latind <= endlatind; latind++)
        for (int lonind = startlonind;; lonind++)
        {
            if (lonind > LONGITUDE_ZONES - 1)
                lonind = 0;

            for (auto& seg : m_map[latind][lonind]) {
                DrawLineSegGL(vp, seg.lat1, seg.lon1, seg.lat2, seg.lon2);
                DrawContour(nullptr, vp,
                            seg.contour,
                            (seg.lat1 + seg.lat2) / 2,
                            (seg.lon1 + seg.lon2) / 2);
            }

            if (lonind == endlonind)
                break;
        }
}
