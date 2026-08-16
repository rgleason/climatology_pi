#pragma once


#include <wx/wxprec.h>
#ifndef WX_PRECOMP
#include <wx/wx.h>
#endif

#include <wx/string.h>
#include "ClimatologyDefs.h"   // for OverlayType enum

// ---- Unit conversion ----
double ConvertSpeed(double v, int unit);
double ConvertPressure(double v, int unit);
double ConvertTemperature(double v, int unit);
double ConvertPrecip(double v, int unit);
double ConvertDepth(double v, int unit);

// ---- Unit formatting ----
wxString FormatSpeed(double v, int unit);
wxString FormatPressure(double v, int unit);
wxString FormatTemp(double v, int unit);
wxString FormatPrecip(double v, int unit);
wxString FormatDepth(double v, int unit);

// ---- Unified dispatchers ----
double ConvertValue(OverlayType t, double v, int unit);
wxString FormatValue(OverlayType t, double v, int unit);

// ---- Unit names ----
wxString UnitName(int unit);

// ---- Valid unit initialization ----
void InitValidUnits(std::vector<int> validUnits[]);