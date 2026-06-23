#pragma once
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
 *
 */

// Always pull in the real OpenGL headers first

#include <GL/glew.h>

// do NOT include <GL/gl.h> or <GL/glu.h> here;
// glew will pull what it needs.
#ifndef APIENTRYP
#define APIENTRYP APIENTRY *
#endif

#if !defined(GL_CLAMP_TO_EDGE)
#define GL_CLAMP_TO_EDGE 0x812F
#endif

// All ARB multitexture and rectangle texture legacy defines removed.
// All legacy function pointer typedefs removed.
// All glTexCoord2d / glTexEnvfv redeclarations removed.
