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

#include "piDC.h"

#include "ocpn_plugin.h"   // MUST be first

#include <wx/string.h>
#include <wx/colour.h>
#include <list>

#define ZONE_SIZE 8
#define MAX_LAT 88
#define LATITUDE_ZONES (2*MAX_LAT/ZONE_SIZE)
#define LONGITUDE_ZONES (360/ZONE_SIZE)

class wxWindow;

struct PlugIn_ViewPort;

// forward declarations for the new context types
class ClimatologyOverlayFactory;
class ClimatologyRenderParams;

class PlotLineSeg
{
public:
    PlotLineSeg(double _lat1, double _lon1, double _lat2, double _lon2, double _contour)
        : lat1(_lat1), lon1(_lon1), lat2(_lat2), lon2(_lon2), contour(_contour) {}
    double lat1, lon1, lat2, lon2;
    double contour;
};

class ParamCache
{
public:
    ParamCache() : values(NULL), m_step(0) {}
    ~ParamCache() { delete [] values; }

    void Initialize(double step);
    bool Read(double lat, double lon, double &value);

    double *values;
    double m_step;
    double m_lat;
};

class ContourText
{
public:
    wxString text;
    int w, h;
    int lastx, lasty;
};

class IsoBarMap
{
public:
    IsoBarMap(wxString name,
              double spacing,
              double step,
              const ClimatologyOverlayFactory& factory,
              int overlayType,
              const ClimatologyRenderParams&   renderParams);

    virtual ~IsoBarMap();

    bool Recompute(wxWindow *parent);

    void Plot(piDC *dc, PlugIn_ViewPort &vp);
    void PlotGL(PlugIn_ViewPort &vp);

    bool m_bNeedsRecompute, m_bComputing;

protected:
    double m_Spacing, m_Step, m_PoleAccuracy;

private:
    virtual double CalcParameter(double lat, double lon) = 0;
    double Parameter(double lat, double lon);

    void PlotRegion(std::list<PlotLineSeg> &region,
                    double lat1, double lon1, double lat2, double lon2,
                    int maxdepth);

    void BuildParamCache(ParamCache &cache, double lat);
    double CachedParameter(double lat, double lon);

    bool Interpolate(double x1, double x2, double y1, double y2, bool lat,
                     double lonval, double &rx, double &ry);

    void ClearMap();
    ContourText ContourCacheData(double value);

	void DrawContour(piDC *dc, PlugIn_ViewPort &VP, double contour, double lat, double lon);
	
    void DrawContourGL(PlugIn_ViewPort &VP, double contour, double lat, double lon);

    ParamCache m_Cache[2];
    std::list<PlotLineSeg> m_map[LATITUDE_ZONES][LONGITUDE_ZONES];

    double m_MinContour, m_MaxContour;
    int m_contourcachesize;
    ContourText *m_contourcache;

    int lastx, lasty;

    wxString m_Name;
    bool m_bPolar;
    wxColour m_Color;

    // unified‑grid / rendering context
    const ClimatologyOverlayFactory& m_factory;
    int                              m_setting;       // overlay id (any scalar overlay)
    const ClimatologyRenderParams&   m_renderParams;
};


