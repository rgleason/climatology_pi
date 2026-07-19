/******************************************************************************
 *
 * Project:  OpenCPN
 * Purpose:  Climatology Plugin
 * Author:   Sean D'Epagnier
 *
 ***************************************************************************
 *   Copyright (C) 2013 by Sean D'Epagnier                                 *
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
 *   51 Franklin Street, Fifth Floor, Boston, MA 02110-1301,  USA.             *
 ***************************************************************************
 */
#pragma once

#define _USE_MATH_DEFINES
#include <cmath>
#include <limits>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// Provide NAN / INFINITY if missing
#ifndef NAN
#define NAN std::numeric_limits<double>::quiet_NaN()
#endif

#ifndef INFINITY
#define INFINITY std::numeric_limits<double>::infinity()
#endif

// ---- Math helpers that are NOT part of <cmath> ----

static inline double square(double x)
{
    return x * x;
}

static inline double rad2deg(double radians)
{
    return 180.0 * radians / M_PI;
}

static inline double positive_degrees(double degrees)
{
    while (degrees < 0)
        degrees += 360;
    while (degrees >= 360)
        degrees -= 360;
    return degrees;
}
