/******************************************************************************
 *
 * Project:  OpenCPN
 * Purpose:  Iso Bars
 * Author:   Sean D'Epagnier
 *
 ***************************************************************************
 *   Copyright (C) 2013 by Sean D'Epagnier   *
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
#pragma message("Using IsoBarMap.h from: " __FILE__)

#include <wx/wxprec.h>
#ifndef WX_PRECOMP
#include <wx/wx.h>
#endif


//=== Forward declarations =====================================================
class wxWindow;
class wxColour;
class piDC;                 // forward declare instead of including piDC.h
struct PlugIn_ViewPort;

class ClimatologyOverlayFactory;
struct ClimatologyRenderParams;


//=== Lightweight includes =====================================================
#include <wx/string.h>
#include <list>

//=== Constants ================================================================
#define ZONE_SIZE 8
#define MAX_LAT 88
#define LATITUDE_ZONES (2*MAX_LAT/ZONE_SIZE)
#define LONGITUDE_ZONES (360/ZONE_SIZE)

//=============================================================================
// PlotLineSeg
//=============================================================================
class PlotLineSeg
{
public:
    PlotLineSeg(double _lat1, double _lon1, double _lat2, double _lon2, double _contour)
        : lat1(_lat1), lon1(_lon1), lat2(_lat2), lon2(_lon2), contour(_contour) {}

    double lat1, lon1, lat2, lon2;
    double contour;
};

//=============================================================================
// ParamCache
//=============================================================================
class ParamCache
{
public:
    ParamCache() : values(nullptr), m_step(0) {}
    ~ParamCache() { delete [] values; }

    void Initialize(double step);
    bool Read(double lat, double lon, double &value);

    double* values;
    double  m_step;
    double  m_lat;
};

//=============================================================================
// ContourText
//=============================================================================
class ContourText
{
public:
    wxString text;
    int w, h;
    int lastx, lasty;
};

//=============================================================================
// IsoBarMap
//=============================================================================
class IsoBarMap
{
public:
    IsoBarMap(const wxString& name,
              double spacing,
              double step,
              const ClimatologyOverlayFactory& factory,
              int overlayType,
              const ClimatologyRenderParams& renderParams);

    virtual ~IsoBarMap();

    bool Recompute(wxWindow* parent);

    void PlotGL(PlugIn_ViewPort& vp);
    virtual void PlotDC(piDC* dc, PlugIn_ViewPort& vp);

    bool m_bNeedsRecompute;
    bool m_bComputing;

protected:
    double m_Spacing;
    double m_Step;
    double m_PoleAccuracy;
    const ClimatologyOverlayFactory& m_factory;
    int                              m_setting;
    const ClimatologyRenderParams&   m_renderParams;

private:
    virtual double CalcParameter(double lat, double lon) = 0;
    double Parameter(double lat, double lon);

    void PlotRegion(std::list<PlotLineSeg>& region,
                    double lat1, double lon1,
                    double lat2, double lon2,
                    int maxdepth);

    void BuildParamCache(ParamCache& cache, double lat);
    double CachedParameter(double lat, double lon);

    bool Interpolate(double x1, double x2,
                     double y1, double y2,
                     bool lat,
                     double lonval,
                     double& rx, double& ry);

    void ClearMap();
    ContourText ContourCacheData(double value);

    void DrawContour(piDC* dc, PlugIn_ViewPort& vp,
                     double contour, double lat, double lon);

    void DrawContourGL(PlugIn_ViewPort& vp,
                       double contour, double lat, double lon);

    ParamCache m_Cache[2];
    std::list<PlotLineSeg> m_map[LATITUDE_ZONES][LONGITUDE_ZONES];

    double       m_MinContour;
    double       m_MaxContour;
    int          m_contourcachesize;
    ContourText* m_contourcache;

    int      lastx, lasty;
    wxString m_Name;
    bool     m_bPolar;
    wxColour m_Color;


};
