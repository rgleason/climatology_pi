/******************************************************************************
 *
 * Project:  OpenCPN
 * Purpose:  Climatology Plugin
 * Author:   Sean D'Epagnier
 *
 ***************************************************************************
 *   Copyright (C) 2018 by Sean D'Epagnier                                 *
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

#include <GL/glew.h>
#include <wx/wx.h>
#include <wx/glcanvas.h>
#include <wx/event.h>

#include "gldefs.h"
#include <pi_shaders.h>

#include "icons.h"
#include "climatology_pi.h"
#include "ClimatologyOverlayFactory.h"
#include "ManifestLoader.hpp"
#include "DownloadManager.hpp"

class CycloneLoaderThread : public wxThread
{
public:
    CycloneLoaderThread(ClimatologyOverlayFactory* parent)
        : wxThread(wxTHREAD_DETACHED), m_parent(parent) {}

private:
    ClimatologyOverlayFactory* m_parent;

    virtual ExitCode Entry() override;
};


std::vector<DownloadFileEntry>
ConvertManifest(const std::vector<ManifestEntry>& manifest)
{
    std::vector<DownloadFileEntry> out;

	const std::string base =
    "https://raw.githubusercontent.com/rgleason/climatology_pi_data/master/";
    wxLogMessage("ConvertManifest: Using base URL = %s", base);

    for (const auto& m : manifest)
    {
        DownloadFileEntry d;
        d.filename    = m.filename;
        d.description = m.description;
        d.size        = m.size;
        d.checksum    = m.checksum;
        d.required    = m.required;

        d.url = base + m.filename;

        if (d.size == 0)
            d.required = true;

        out.push_back(d);
    }

    return out;
}


#define FAILED_FILELIST_MSG_LEN 150


double deg2rad(double degrees)
{
  return M_PI * degrees / 180.0;
}



ClimatologyOverlay::~ClimatologyOverlay()
{
    if(m_iTexture)
        glDeleteTextures( 1, &m_iTexture );
    delete m_pDCBitmap, delete[] m_pRGBA;
}

static const wxString climatology_pi = "climatology_pi: ";


double ClimatologyIsoBarMap::CalcParameter(double lat, double lon)
{
    return m_factory.getCalibratedValueMonth(MAG, m_setting, lat, lon, m_month);
}

#define CYCLONE_CACHE_SEMAPHORE_COUNT 4
ClimatologyOverlayFactory::ClimatologyOverlayFactory(ClimatologyDialog &dlg)
    : m_cyclone_cache_semaphore( CYCLONE_CACHE_SEMAPHORE_COUNT),
      m_bCompletedLoading(false),
      m_dlg(dlg),
      m_Settings(dlg.m_cfgdlg->m_Settings),
      m_cyclone_drawn_counter(0)
{
	wxLogMessage("Climatology_pi: dlg.m_cfgdlg = %p", dlg.m_cfgdlg);
	
	// REQUIRED for background thread event posting
    m_pClimatologyDialog = &dlg;

    // Ensure data directory exists
    wxFileName::Mkdir(ClimatologyDataDirectory(), wxS_DIR_DEFAULT, wxPATH_MKDIR_FULL);

    for (int m = 0; m < 13; m++) {
        m_WindData[m] = NULL;
        m_CurrentData[m] = NULL;
    }

    // Initialize timeline
    m_CurrentTimeline = wxDateTime::Now();
    if (m_CurrentTimeline.IsLeapYear() &&
        m_CurrentTimeline.GetMonth() == wxDateTime::Feb &&
        m_CurrentTimeline.GetDay() == 29)
        m_CurrentTimeline.SetDay(28);
    m_CurrentTimeline.SetYear(1999);

    m_bAllTimes = false;
	
	
    // ------------------------------------------------------------
    // 1. Initialize DownloadManager
    // ------------------------------------------------------------
    m_downloadManager = new DownloadManager(&m_dlg, ClimatologyDataDirectory());

    // ------------------------------------------------------------
    // 2. Load manifest.json
    // ------------------------------------------------------------
    wxString manifestPath = ClimatologyDataDirectory() + wxFileName::GetPathSeparator() + "manifest.json";

    ManifestLoader loader(manifestPath.ToStdString());

    std::vector<ManifestEntry> manifestEntries;
    if (!loader.Load(manifestEntries)) {
		wxLogWarning("Climatology_pi: Failed to load manifest.json — proceeding without manifest");
	    // Continue with empty list; Load() will fail gracefully
    }

	// ------------------------------------------------------------
	// 3. Convert ManifestEntry → DownloadFileEntry
	// ------------------------------------------------------------
	std::vector<DownloadFileEntry> files = ConvertManifest(manifestEntries);

	// ------------------------------------------------------------
	// 4. Pass manifest to Downloadmanager
	// ------------------------------------------------------------
    m_downloadManager->SetManifest(files);	

    // ------------------------------------------------------------
    // 5. Local data present? → Load immediately
    // ------------------------------------------------------------
	if (m_downloadManager->AllRequiredAvailable()) {
		wxLogMessage("Climatology_pi: All climatology data verified — loading now");
		
		// Load immediately; Load() decides if we’re “complete”
		Load();   
		
		if (!m_bCompletedLoading) {
			wxLogMessage("Climatology_pi: Data load failed — some files corrupt or missing");
			m_dlg.m_tStatus->SetLabel("Climatology data load failed");
			m_dlg.EnableAllControls(false);
			return;
		}

		m_dlg.m_tStatus->SetLabel("Climatology data loaded");
		m_dlg.EnableAllControls(true);
		m_dlg.UpdateTrackingControls();

		return;
	}

    // ------------------------------------------------------------
    // 6. Otherwise → start background download
    // ------------------------------------------------------------
    wxLogMessage("Climatology_pi: Missing or corrupt data — starting download manager");
    m_dlg.Bind(EVT_DM_COMPLETE, &ClimatologyOverlayFactory::OnDownloadComplete, this);
    m_downloadManager->StartBackgroundDownload(true);
    // UI stays disabled until OnDownloadComplete()
}


void ClimatologyOverlayFactory::OnDownloadComplete(wxCommandEvent& event)
{
    wxLogMessage("Climatology_pi: Download manager completed — starting data load");

    // Unbind to avoid duplicate calls
    Unbind(EVT_DM_COMPLETE, &ClimatologyOverlayFactory::OnDownloadComplete, this);

    // Run the loader
    Load();

    if (!m_bCompletedLoading) {
        wxLogMessage("Climatology_pi: Data load failed — some files corrupt or missing");
        m_dlg.m_tStatus->SetLabel("Climatology data load failed");
        m_dlg.EnableAllControls(false);
        return;
    }

    m_dlg.m_tStatus->SetLabel("Climatology data loaded");
    m_dlg.EnableAllControls(true);
    m_dlg.UpdateTrackingControls();
}


bool ClimatologyOverlayFactory::ValidateCycloneFiles()
{
    return !m_epa.empty() || !m_wpa.empty() || !m_spa.empty() ||
           !m_atl.empty() || !m_nio.empty() || !m_she.empty();
}


ClimatologyOverlayFactory::~ClimatologyOverlayFactory() = default;


void ClimatologyOverlayFactory::GetDateInterpolation(const wxDateTime *cdate,
                                                     int &month, int &nmonth, double &dpos)
{
    if(!cdate) {
        if(m_bAllTimes) {
            month = nmonth = 12;
            dpos = 1;
            return;
        }
        cdate = &m_CurrentTimeline;
    }
    
    month = cdate->GetMonth();
    int day = cdate->GetDay();
    int daysinmonth = wxDateTime::GetNumberOfDays(cdate->GetMonth());
    dpos = (day-.5) / daysinmonth;
    
    if(dpos > .5) {
        nmonth = month + 1;
        if(nmonth == 12)
            nmonth = 0;
        dpos = 1.5 - dpos;
    } else {
        nmonth = month - 1;
        if(nmonth == -1)
            nmonth = 11;
        dpos = .5 + dpos;
    }
}

bool ClimatologyOverlayFactory::InterpolateWindAtlasTime(int month, int nmonth, double dpos,
                                                         double lat, double lon,
                                                         double *directions, double *speeds,
                                                         double &gale, double &calm)
{
    if(!m_WindData[month] || !m_WindData[nmonth])
        return false;
    WindData::WindPolar *polar1 = m_WindData[month]->GetPolar(lat, positive_degrees(lon));
    WindData::WindPolar *polar2 = m_WindData[nmonth]->GetPolar(lat, positive_degrees(lon));

    if(!polar1 || !polar2)
        return false;

    gale = (dpos*polar1->gale + (1-dpos)*polar2->gale) / 100.0;
    calm = (dpos*polar1->calm + (1-dpos)*polar2->calm) / 100.0;

    int dir_cnt = m_WindData[month]->dir_cnt;

    double totald1 = 0, totald2 = 0;
    for(int i=0; i<dir_cnt; i++) {
        totald1 += polar1->directions[i];
        totald2 += polar2->directions[i];
    }

    for(int i=0; i<dir_cnt; i++) {
        double direction1 = polar1->directions[i]/m_WindData[month]->direction_resolution;
        double direction2 = polar2->directions[i]/m_WindData[nmonth]->direction_resolution;
        directions[i] = dpos*direction1 + (1-dpos)*direction2;

        double speed1 = (double)polar1->speeds[i] / m_WindData[month]->speed_multiplier;
        double speed2 = (double)polar2->speeds[i] / m_WindData[nmonth]->speed_multiplier;

        if(direction1) {
            if(direction2)
                speeds[i] = dpos*speed1 + (1-dpos)*speed2;
            else
                speeds[i] = speed1;
        } else if(direction2)
            speeds[i] = speed2;
        else
            speeds[i] = 0;
    }

    return true;
}

static double interpquad(double v1, double v2, double v3, double v4, double d1, double d2)
{
    double w1 = d1*v3 + (1-d1)*v1;
    double w2 = d1*v4 + (1-d1)*v2;
    return      d2*w2 + (1-d2)*w1;
}

bool ClimatologyOverlayFactory::InterpolateWindAtlas(wxDateTime &date,
                                                     double lat, double lon,
                                                     double *directions, double *speeds,
                                                     double &gale, double &calm)
{
    int month, nmonth;
    double dpos;
    GetDateInterpolation(&date, month, nmonth, dpos);

    double idirections[4][8], ispeeds[4][8], igale[4], icalm[4];

    double lats[2] = {floor(lat), ceil(lat)}, lons[2] = {floor(lon), ceil(lon)};
    double latd = lat - lats[0], lond = lon - lons[0];
    bool havedata[4];

    for(int lati = 0; lati<2; lati++)
        for(int loni = 0; loni<2; loni++) {
            int i = 2*lati + loni;
            havedata[i] = InterpolateWindAtlasTime(month, nmonth, dpos, lats[lati], lons[loni],
                                                   idirections[i], ispeeds[i], igale[i], icalm[i]);
        }

    /* fill in missing data */
    int searchdata[4][3] = {{1, 2, 3}, {0, 3, 2}, {3, 0, 1}, {2, 1, 0}};
    for(int i = 0; i<4; i++)
        if(!havedata[i]) {
            for(int j = 0; j < 3; j++) {
                int k = searchdata[i][j];
                if(havedata[k]) {
                    memcpy(idirections[i], idirections[k], sizeof *idirections);
                    memcpy(ispeeds[i], ispeeds[k], sizeof *ispeeds);
                    igale[i] = igale[k];
                    icalm[i] = icalm[k];
                    goto outer_continue;
                }
            }
            return false;
        outer_continue:;
        }

    int dir_cnt = m_WindData[month]->dir_cnt;
    for(int i=0; i<dir_cnt; i++) {
        directions[i] = interpquad(idirections[0][i], idirections[1][i],
                                   idirections[2][i], idirections[3][i], latd, lond);
        speeds[i] = interpquad(ispeeds[0][i], ispeeds[1][i],
                               ispeeds[2][i], ispeeds[3][i], latd, lond);
    }

    gale = interpquad(igale[0], igale[1], igale[2], igale[3], latd, lond);
    calm = interpquad(icalm[0], icalm[1], icalm[2], icalm[3], latd, lond);

    return true;
}

void ClimatologyOverlayFactory::DrawLine( double x1, double y1, double x2, double y2,
                                          const wxColour &color, double width )
{
    m_dc->SetPen( wxPen(color, width ) );
    m_dc->SetBrush( *wxTRANSPARENT_BRUSH);
    m_dc->DrawLine(x1, y1, x2, y2);
}

void ClimatologyOverlayFactory::DrawCircle( double x, double y, double r,
                                            const wxColour &color, double width )
{
    m_dc->SetPen( wxPen(color, width ) );
    m_dc->SetBrush( *wxTRANSPARENT_BRUSH);
    m_dc->DrawCircle(x, y, r);
}

struct ColorMap {
    double val;
    wxString text;
    wxUint8 transp;
};

static ColorMap WindMap[] =
{{0,  "#ffffff", 0}, {2, "#00ffff", 0},  {4,  "#00e4e4", 0}, {5,  "#00d9e4", 0},
 {6,  "#00d9d4", 0}, {7, "#00d9b2", 0},  {8,  "#00d96e", 0}, {9,  "#00d92a", 0},
 {10, "#00d900", 0}, {11, "#2ad900", 0}, {12, "#6ed900", 0}, {13, "#b2d900", 0},
 {14, "#d4d400", 0}, {15, "#d9a600", 0}, {16, "#d90000", 0}, {17, "#d90040", 0},
 {18, "#d90060", 0}, {19, "#ae0080", 0}, {20, "#8300a0", 0}, {21, "#5700c0", 0},
 {25, "#0000d0", 0}, {30, "#0000f0", 0}, {35, "#00a0ff", 0}, {40, "#a0a0ff", 0},
 {50, "#ffffff", 0}};

static ColorMap CurrentMap[] =
{{0,   "#0000d9", 255}, {.1,  "#002ad9", 196},  {.2, "#006ed9", 128},  {.3, "#00b2d9", 64},
 {.4,  "#04d4d4", 0}, {.5,  "#06d9a6", 0},  {.7, "#a0d906", 0},  {.9, "#b0d900", 0},
 {1.2, "#c0d900", 0}, {1.5, "#d0ae00", 0}, {1.8, "#e08300", 0}, {2.1, "#e05700", 0},
 {2.4, "#f00000", 0}, {2.7, "#f00004", 0}, {3.0, "#f0001c", 0}, {3.6, "#f00048", 0},
 {4.2, "#f00069", 0}, {4.8, "#f000a0", 0}, {5.6, "#f000f0", 0}};

static ColorMap PressureMap[] =
{{900,  "#283282", 0}, {980,  "#273c8c", 0}, {990,  "#264696", 0}, {1000,  "#2350a0", 0},
 {1001, "#1f5aaa", 0}, {1002, "#1a64b4", 0}, {1003, "#136ec8", 0}, {1004, "#0c78e1", 0},
 {1005, "#0382e6", 0}, {1006, "#0091e6", 0}, {1007, "#009ee1", 0}, {1008, "#00a6dc", 0},
 {1009, "#00b2d7", 0}, {1010, "#00bed2", 0}, {1011, "#28c8c8", 0}, {1012, "#78d2aa", 0},
 {1013, "#8cdc78", 0}, {1014, "#a0eb5f", 0}, {1015, "#c8f550", 0}, {1016, "#f3fb02", 0},
 {1017, "#ffed00", 0}, {1018, "#ffdd00", 0}, {1019, "#ffc900", 0}, {1020, "#ffab00", 0},
 {1021, "#ff8100", 0}, {1022, "#f1780c", 0}, {1024, "#e26a23", 0}, {1028, "#d5453c", 0},
 {1040, "#b53c59", 0}};

static ColorMap SeaTempMap[] =
{{0, "#0000d9", 0},  {3, "#002ad9", 0},  {6, "#006ed9", 0},  {9, "#00b2d9", 0},
 {12, "#00d4d4", 0}, {15, "#00d9a6", 0}, {18, "#00d900", 0}, {20, "#95d900", 0},
 {22, "#d9d900", 0}, {23, "#d9ae00", 0}, {24, "#d98300", 0}, {25, "#d95700", 0},
 {26, "#d90000", 0}, {27, "#ae0000", 0}, {28, "#8c0000", 0}, {29, "#870000", 0},
 {30, "#690000", 0}, {32, "#550000", 0}, {35, "#410000", 0}};

static ColorMap AirTempMap[] =
{{-50, "#0000d9", 0},  {-40, "#002ad9", 0},  {-30, "#006ed9", 0},  {-20, "#00b2d9", 0},
 {-10, "#00d4d4", 0}, {0, "#00d9a6", 0}, {5, "#00d900", 0}, {8, "#95d900", 0},
 {12, "#d9d900", 0}, {15, "#d9ae00", 0}, {18, "#d98300", 0}, {21, "#d95700", 0},
 {24, "#d90000", 0}, {27, "#ae0000", 0}, {30, "#8c0000", 0}, {35, "#870000", 0},
 {40, "#690000", 0}, {45, "#550000", 0}, {50, "#410000", 0}};

static ColorMap CloudMap[] =
{{0, "#f0f0e6", 255},  {10, "#e6e6dc", 224}, {20, "#dcdcd2", 192},
 {30, "#c8c8b4", 160}, {40, "#aaaa8c", 128}, {50, "#969678", 96}, {60, "#787864", 64},
 {70, "#646450", 32},  {80, "#5a5a46", 0},   {90, "#505036", 0}, {100, "#404036", 0}};

static ColorMap PrecipitationMap[] =
{{0, "#000080", 255}, {1, "#0000a0", 208}, {2, "#4040f0", 164},
 {3, "#8080f0", 128}, {4, "#f0f0f0", 64}, {5, "#f0a0f0", 32}, {8, "#f080f0", 0},
 {10, "#f04080", 0}, {13, "#f00040", 0}, {16, "#f00000", 0}, {19, "#800000", 0}};

static ColorMap RelativeHumidityMap[] =
{{0,  "#000000", 0}, {30, "#303030", 0}, {60, "#606060", 0},
 {65, "#a06060", 0}, {70, "#ff6060", 0}, {75, "#ffc080", 0},
 {80, "#a0f0a0", 0}, {85, "#60f0f0", 0}, {95, "#40a0f0", 0},
 {100, "#2080f0", 0}};

static ColorMap LightningMap[] =
{{0, "#490000", 255},  {64, "#890000", 128}, {128, "#a98900", 64},
 {192, "#ffd900", 0}, {255, "#ffff00", 0}};

ColorMap SeaDepthMap[] =
{{0, "#0000d9", 255},  {20, "#002ad9", 0},   {50, "#006ed9", 0},   {100, "#00b2d9", 0},
 {150, "#00d4d4", 0},  {250, "#00d9a6", 0},  {400, "#00d900", 0},  {600, "#95d900", 0},
 {800, "#a9d900", 0},  {1000, "#d9d900", 0}, {1200, "#d9f000", 0}, {1400, "#d9c040", 0},
 {2000, "#a9c040", 0}, {3000, "#a0a040", 0}, {4000, "#808060", 0}, {5000, "#606060", 0},
 {6000, "#404040", 0}, {8000, "#202020", 0}, {10000, "#000000", 0}};

static ColorMap CycloneMap[] =
{{0, "#0000d9", 255}, {10, "#002ad9", 200}, {20, "#006ed9", 128}, {30, "#00b2d9", 96},
 {40, "#00d4d4", 64},  {50, "#00d9a6", 64}, {60, "#00d900", 64}, {70, "#75d900", 64},
 {80, "#a9d900", 32},  {90, "#a9ae00", 32}, {100, "#d98000", 32}, {110, "#d94000", 32},
 {120, "#d90000", 0}, {130, "#ff0000", 0}, {140, "#ff0080", 0}, {150, "#ff00ff", 0},
 {160, "#ff40ff", 0}, {180, "#ff80ff", 0}, {200, "#ffffff", 0}};

static int color255(char a, char b)
{
    char str[3] = {a, b, 0};
    return strtol(str, 0, 16);
}

static wxColour TextColor(wxString text)
{
    return wxColour(color255(text[1], text[2]), color255(text[3], text[4]), color255(text[5], text[6]));
}                        

ColorMap *ColorMaps[] = {WindMap, CurrentMap, PressureMap, SeaTempMap, AirTempMap,
                         CloudMap, PrecipitationMap, RelativeHumidityMap, LightningMap,
                         SeaDepthMap, CycloneMap};

static const int ColorMapLens[] = { (sizeof WindMap) / (sizeof *WindMap),
                             (sizeof CurrentMap) / (sizeof *CurrentMap),
                             (sizeof PressureMap) / (sizeof *PressureMap),
                             (sizeof SeaTempMap) / (sizeof *SeaTempMap),
                             (sizeof AirTempMap) / (sizeof *AirTempMap),
                             (sizeof CloudMap) / (sizeof *CloudMap),
                             (sizeof PrecipitationMap) / (sizeof *PrecipitationMap),
                             (sizeof RelativeHumidityMap) / (sizeof *RelativeHumidityMap),
                             (sizeof LightningMap) / (sizeof *LightningMap),
                             (sizeof SeaDepthMap) / (sizeof *SeaDepthMap),
                             (sizeof CycloneMap) / (sizeof *CycloneMap)};


wxColour ClimatologyOverlayFactory::GetGraphicColor(int setting, double val_in)
{
    if(isnan(val_in))
        return wxColour(0, 0, 0, 0); /* transparent */

    int colormap_index = setting;
    ColorMap *map = ColorMaps[colormap_index];
    int maplen = ColorMapLens[colormap_index];

    double cmax = 1;
    for(int i=1; i<maplen; i++) {
        double nmapvala = map[i-1].val/cmax;
        double nmapvalb = map[i].val/cmax;
        if(nmapvalb > val_in || i==maplen-1) {
            wxColour b, c;
            c = TextColor(map[i].text);
            b = TextColor(map[i-1].text);
            double d = (val_in-nmapvala)/(nmapvalb-nmapvala);
            c.Set((1-d)*b.Red()   + d*c.Red(),
                  (1-d)*b.Green() + d*c.Green(),
                  (1-d)*b.Blue()  + d*c.Blue(),
                  255 - ((1-d)*map[i-1].transp + d*map[i].transp));
            return c;
        }
    }
    return *wxBLACK; /* unreachable */
}

void ClimatologyOverlayFactory::LoadInternal(wxGenericProgressDialog *progressdialog)
{
    wxString fmt = "%02d";

    /* load wind */
    for(int month = 0; month < 12; month++) {
        wxString filename = wxString::Format("wind"+fmt, month+1);
		 if(progressdialog && !progressdialog->Update(month, filename)) {
			wxLogWarning("Climatology: Progress dialog aborted update step");
			continue;
		}
        ReadWindData(month, filename);
    }

	/* averaging wind */
	if(progressdialog && !progressdialog->Update(12, _("averaging wind"))) {
		wxLogWarning("Climatology: Progress dialog aborted update step");
		// continue;  <-- but this is not in a loop, so just fall through
	}
	AverageWindData();

	/* load current */
	for(int month = 0; month < 12; month++) {
		wxString filename = wxString::Format("current"+fmt, month+1);

		if(progressdialog && !progressdialog->Update(month+13, filename)) {
			wxLogWarning("Climatology: Progress dialog aborted update step");
			continue;   // <-- DO NOT RETURN
		}

		ReadCurrentData(month, filename);
	}

	/* averaging current */
	if(progressdialog && !progressdialog->Update(25, _("averaging current"))) {
		wxLogWarning("Climatology: Progress dialog aborted update step");
		// DO NOT RETURN — just continue
	}

	AverageCurrentData();

	if (!m_CurrentData[0]) {
		wxLogWarning("Climatology: Averaged current data missing or invalid");
	}

	/* load sea level pressure and sea surface temperature */
	if(progressdialog && !progressdialog->Update(26, _("sea level presure"))) {
		wxLogWarning("Climatology: Progress dialog aborted update step");
		// do NOT return
	}
	ReadSeaLevelPressureData("sealevelpressure");

	if(progressdialog && !progressdialog->Update(27, _("sea surface tempertature"))) {
		wxLogWarning("Climatology: Progress dialog aborted update step");
	}
	ReadSeaSurfaceTemperatureData("seasurfacetemperature");

	if(progressdialog && !progressdialog->Update(28, _("air tempertature"))) {
		wxLogWarning("Climatology: Progress dialog aborted update step");
	}
	ReadAirTemperatureData("airtemperature");

	if(progressdialog && !progressdialog->Update(29, _("cloud cover"))) {
		wxLogWarning("Climatology: Progress dialog aborted update step");
	}
	ReadCloudData("cloud");

	if(progressdialog && !progressdialog->Update(30, _("precipitation"))) {
		wxLogWarning("Climatology: Progress dialog aborted update step");
	}
	ReadPrecipitationData("precipitation");

	if(progressdialog && !progressdialog->Update(31, _("relative humidity"))) {
		wxLogWarning("Climatology: Progress dialog aborted update step");
	}
	ReadRelativeHumidityData("relativehumidity");

    /* load lightning */
	if(progressdialog && !progressdialog->Update(32, _("lightning"))) {
		wxLogWarning("Climatology: Progress dialog aborted update step");
		// do NOT return
	}
	ReadLightningData("lightning");

    /* load sea depth */
	if(progressdialog && !progressdialog->Update(33, _("sea depth"))) {
		wxLogWarning("Climatology: Progress dialog aborted update step");
	}
	ReadSeaDepthData("seadepth");

	// CYCLONE LOADING DISABLED AT STARTUP
	// Cyclones will be loaded later in a background thread.

	wxLogMessage("Climatology: Skipping cyclone loading at startup");
	m_cycloneReady = false;

	// Skip cyclone loading and El Niño years for now.
	// They will be loaded asynchronously.
	return;
}

void ClimatologyOverlayFactory::Load()
{
    m_sFailedMessage.clear();
    m_FailedFiles.clear();

    wxGenericProgressDialog* progressdialog =
        new wxGenericProgressDialog(_("Climatology"), wxString(), 38, &m_dlg,
            wxPD_CAN_ABORT | wxPD_ELAPSED_TIME);

    LoadInternal(progressdialog);
    progressdialog->Destroy();

    // Default: loading failed
    m_bCompletedLoading = false;

    // Change this - Only consider success if no file failures
    // Filter out cyclone failures — they are optional
		bool onlyOptionalFailures = true;
		for (auto& f : m_FailedFiles) {
			if (!f.StartsWith("cyclone") && !f.StartsWith("elnino")) {
				onlyOptionalFailures = false;
				break;
			}
		}

		if (onlyOptionalFailures) {

        bool ok = true;

        // Validate wind data require all 12 months
        for (int m = 0; m < 12; ++m) {
            if (!m_WindData[m]) {
                wxLogWarning("Climatology: Missing wind data for month %d", m);
                ok = false;
                break;
            }
        }
		// Validate current data: require all 12 months
		for (int m = 0; m < 12; ++m) {
			if (!m_CurrentData[m]) {
				wxLogWarning("Climatology: Missing current data for month %d", m + 1);
				ok = false;
				break;
			}
		}
		// Validate single-file datasets (require at least one month loaded)
		if (!m_slp[0]) {
			wxLogWarning("Climatology: Missing sea level pressure data");
			ok = false;
		}

		if (!m_sst[0]) {
			wxLogWarning("Climatology: Missing sea surface temperature data");
			ok = false;
		}

		if (!m_at[0]) {
			wxLogWarning("Climatology: Missing air temperature data");
			ok = false;
		}

		if (!m_cld[0]) {
			wxLogWarning("Climatology: Missing cloud cover data");
			ok = false;
		}

		if (!m_precip[0]) {
			wxLogWarning("Climatology: Missing precipitation data");
			ok = false;
		}

		if (!m_rhum[0]) {
			wxLogWarning("Climatology: Missing relative humidity data");
			ok = false;
		}

		if (!m_lightn[0]) {
			wxLogWarning("Climatology: Missing lightning data");
			ok = false;
		}

		// Sea depth is always allocated, so no null check needed

        // Validate cyclone data (optional dataset)
        if (!ValidateCycloneFiles()) {
            wxLogWarning("Climatology: No cyclone data loaded");
            // Do NOT set ok = false; cyclones are optional
        }
        // TODO: validate other datasets here (cyclones, currents, etc.)

        m_bCompletedLoading = ok;
		
		// ⭐ START BACKGROUND CYCLONE LOADER HERE ⭐
		if (ok) {
			// ⭐ Snapshot all UI filter values on the main thread
			UpdateCycloneFilterFromUI();

			wxLogMessage("Climatology: Starting background cyclone loader thread");
			auto* t = new CycloneLoaderThread(this);
			if (t->Run() != wxTHREAD_NO_ERROR) {
				wxLogWarning("Climatology: Failed to start cyclone loader thread");
				delete t;
			}
		}
    }
}


void ClimatologyOverlayFactory::ReadWindData(int month, wxString filename)
{
    ZUFILE *f;
#ifdef __OCPN__ANDROID__
    int div = 1; // less ram usage half resolution ?
#else
    int div = 1;
#endif
    wxString path = ClimatologyDataDirectory();
    if(!(f = TryOpenFile(path + filename))) {
        path = ClimatologyDataDirectory();
        if(!(f = TryOpenFile(path + filename)))
            goto missing;
    }

    m_dlg.m_cbWind->Enable();
    
    wxUint16 header[7];
    int dirs;
    if(zu_read(f, header, sizeof header) != sizeof header)
        goto corrupt;

    if(header[0] != 0xfefe ||
       header[1] > 180*16 || header[2] > 360*16 || header[3] != 8 || header[6] == 0)
        goto corrupt;

    m_WindData[month] = new WindData(header[1]/div, header[2]/div, header[3], header[4], (float)header[5] / header[6]);

    dirs = m_WindData[month]->dir_cnt;

    for(int pass=0; pass<2*dirs+1; pass++)
        for(int lati = 0; lati < header[1]; lati++) {
            for(int loni = 0; loni < header[2]; loni++) {
                WindData::WindPolar &wp = m_WindData[month]->data[lati*m_WindData[month]->longitudes/div + loni/div];
                wxUint8 value;

                if(pass == 0) {
                    if(zu_read(f, &value, 1) != 1)
                        goto corrupt;

                    if(value > 200)
                        wp.gale = 255;
                    else if(value >= 100) {
                        wp.gale = value - 100;
                        wp.calm = 0;
                    } else {
                        wp.gale = 0;
                        wp.calm = value;
                    }
                } else if(wp.gale != 255) {
                    if(pass < dirs + 1) {
                        if(zu_read(f, &value, 1) != 1)
                            goto corrupt;

                        wp.directions[pass - 1] = value;
                    } else {
                        if(wp.directions[pass-dirs-1] == 0) {
                            value = 0;
                        }
                        else if(zu_read(f, &value, 1) != 1)
                            goto corrupt;

                        wp.speeds[pass-dirs-1] = value;
                    }
                }
            }
        }

    zu_close(f);
    return;

corrupt:
    zu_close(f);
    delete m_WindData[month];
    m_WindData[month] = NULL;
    m_sFailedMessage += _("corrupt file: ") + filename + "\n";
missing:
    m_FailedFiles.push_back(filename);
    wxLogMessage(climatology_pi + _("wind data file corrupt: ") + filename);
}

void ClimatologyOverlayFactory::AverageWindData()
{
    int fmonth;
    for(fmonth=0; fmonth<12; fmonth++)
        if(m_WindData[fmonth])
            goto havedata;
    return;

havedata:
    int latitudes = m_WindData[fmonth]->latitudes;
    int longitudes = m_WindData[fmonth]->longitudes;
    int dir_cnt = m_WindData[fmonth]->dir_cnt;
    float dir_res = m_WindData[fmonth]->direction_resolution;
    float spd_mul = m_WindData[fmonth]->speed_multiplier;
    m_WindData[12] = new WindData(latitudes, longitudes, dir_cnt, dir_res, spd_mul);

    float latoff = 90.0/m_WindData[fmonth]->latitudes, lonoff = 180.0/m_WindData[fmonth]->longitudes;

    float *directions = new float[dir_cnt];
    float *speeds = new float[dir_cnt];

    for(int lati = 0; lati < latitudes; lati++)
        for(int loni = 0; loni < longitudes; loni++) {
            double lat = 180.0*((double)lati/latitudes-.5) + latoff;
            double lon = 360.0*loni/longitudes + lonoff;

            double gale = 0, calm = 0;
            for(int i=0; i<dir_cnt; i++)
                directions[i] = speeds[i] = 0;

            int mcount = 0;
            for(int month=0; month<12; month++) {
                if(!m_WindData[month])
                    continue;
                WindData::WindPolar *polar = m_WindData[month]->GetPolar(lat, lon);
                if(!polar) {
                    mcount = 0; /* average invalid if missing any month */
                    break;
                }

                int mdir_cnt = m_WindData[month]->dir_cnt;
                gale += polar->gale;
                calm +=  polar->calm;
                for(int i=0; i<dir_cnt; i++) {
                    directions[i] += polar->directions[i*mdir_cnt/dir_cnt];
                    speeds[i] += polar->speeds[i*mdir_cnt/dir_cnt];
                }
                mcount++;
            }

            WindData::WindPolar &wp = m_WindData[12]->data[lati*longitudes + loni];
            if(mcount == 0) {
                wp.gale = 255;
                goto done;
            }

            wp.gale = gale / mcount;
            wp.calm = calm / mcount;

            for(int i=0; i<dir_cnt; i++) {
                wp.directions[i] = directions[i] / mcount;
                wp.speeds[i] = speeds[i] / mcount;
            }

        done:;
        }

    delete [] directions;
    delete [] speeds;
}

void ClimatologyOverlayFactory::ReadCurrentData(int month, wxString filename)
{
    ZUFILE *f;
    wxString path = ClimatologyDataDirectory();
    if(!(f = TryOpenFile(path + filename))) {
        path = ClimatologyDataDirectory();
        if(!(f = TryOpenFile(path + filename)))
            goto missing;
    }

    m_dlg.m_cbCurrent->Enable();

    wxUint16 header[3];
    if (zu_read(f, header, sizeof header) != sizeof header)
        goto corrupt;

    m_CurrentData[month] = new CurrentData(header[0], header[1], header[2]);
    for(int dim = 0; dim<2; dim++)
        for(int lati = 0; lati < m_CurrentData[month]->latitudes; lati++)
            for(int loni = 0; loni < m_CurrentData[month]->longitudes; loni++) {
                int ind = m_CurrentData[month]->longitudes * lati + loni;
                wxInt8 v;
                if (zu_read(f, &v, 1) != 1)
                    goto corrupt;

                if(v == -128)
                    m_CurrentData[month]->data[dim][ind] = NAN;
                else
                    m_CurrentData[month]->data[dim][ind] = (float)v / m_CurrentData[month]->multiplier;
            }
    zu_close(f);
    return;
corrupt:
    delete m_CurrentData[month];
    m_CurrentData[month] = NULL;
    zu_close(f);
    m_sFailedMessage += _("corrupt file: ") + filename + "\n";
missing:
    m_FailedFiles.push_back(filename);
    wxLogMessage(climatology_pi + _("current data file corrupt: ") + filename);
}

void ClimatologyOverlayFactory::AverageCurrentData()
{
    int fmonth;
    for(fmonth=0; fmonth<12; fmonth++)
        if(m_CurrentData[fmonth])
            goto havedata;
    return;

havedata:
    int latitudes = m_CurrentData[fmonth]->latitudes;
    int longitudes = m_CurrentData[fmonth]->longitudes;
    m_CurrentData[12] = new CurrentData(latitudes, longitudes, 1);

    for(int lati = 0; lati < latitudes; lati++)
        for(int loni = 0; loni < longitudes; loni++) {
            double u = 0, v = 0;
            int mcount = 0;
            for(int month=0; month<12; month++) {
                if(!m_CurrentData[month]
                   || m_CurrentData[month]->latitudes != latitudes
                   || m_CurrentData[month]->longitudes != longitudes)
                    continue;
                
                u += m_CurrentData[month]->Value(U, lati, loni);
                v += m_CurrentData[month]->Value(V, lati, loni);
                mcount++;
            }

            static bool nwarned = true;
            if(nwarned && mcount < 12) {
                wxString fmt = " %d ";
                wxLogMessage(climatology_pi + wxString::Format(_("Average Current includes only")
                                                               + fmt + _("months"), mcount));

                nwarned = false;
            }
            if (mcount == 0)
                mcount = 1;

            m_CurrentData[12]->data[0][lati*longitudes + loni] = u / mcount;
            m_CurrentData[12]->data[1][lati*longitudes + loni] = v / mcount;
        }
}


ZUFILE *ClimatologyOverlayFactory::OpenClimatologyDataFile(wxString filename)
{
    wxString path = ClimatologyDataDirectory();

    // Ensure trailing slash
    if (!path.EndsWith("/") && !path.EndsWith("\\"))
        path += wxFileName::GetPathSeparator();

    wxString full = path + filename;

    ZUFILE *f = TryOpenFile(full);
    if(!f)
        m_FailedFiles.push_back(filename);

    return f;
}



void ClimatologyOverlayFactory::ReadSeaLevelPressureData(wxString filename)
{
    ZUFILE *f = OpenClimatologyDataFile(filename);
    if(!f)
        return;

    wxInt16 slp[12][90][180];
    if(zu_read(f, slp, sizeof slp) != sizeof slp) {
        m_FailedFiles.push_back(filename);
        m_sFailedMessage += _("corrupt file: ") + filename + "\n";
        wxLogMessage(climatology_pi + _("slp file truncated"));
    } else {
        for(int j=0; j<90; j++)
            for(int k=0; k<180; k++) {
                long total = 0, totalcount = 0;
                for(int i=0; i<12; i++) {
                    m_slp[i][j][k] = slp[i][j][k];
                    if(slp[i][j][k] != 32767) {
                        total += slp[i][j][k];
                        totalcount++;
                    }
                    if(totalcount)
                        m_slp[12][j][k] = total / totalcount;
                    else
                        m_slp[12][j][k] = 32767;
                }
            }
        m_dlg.m_cbPressure->Enable();
    }
    zu_close(f);
}

void ClimatologyOverlayFactory::ReadSeaSurfaceTemperatureData(wxString filename)
{
    ZUFILE *f = OpenClimatologyDataFile(filename);
    if(!f)
        return;

    wxInt8 sst[12][180][360];
    if(zu_read(f, sst, sizeof sst) != sizeof sst) {
        m_FailedFiles.push_back(filename);
        m_sFailedMessage += _("corrupt file: ") + filename + "\n";
        wxLogMessage(climatology_pi + _("sst file truncated"));
    } else {
        for(int j=0; j<180; j++)
            for(int k=0; k<360; k++) {
                long total = 0, totalcount = 0;
                for(int i=0; i<12; i++) {
                    if(sst[i][j][k] == -128)
                        m_sst[i][j][k] = 32767;
                    else {
                        m_sst[i][j][k] = sst[i][j][k]*200;
                        total += m_sst[i][j][k];
                        totalcount++;
                    }
                    if(totalcount)
                        m_sst[12][j][k] = total / totalcount;
                    else
                        m_sst[12][j][k] = 32767;
                }
            }
        m_dlg.m_cbSeaTemperature->Enable();
    }
    zu_close(f);
}

void ClimatologyOverlayFactory::ReadAirTemperatureData(wxString filename)
{
    ZUFILE *f = OpenClimatologyDataFile(filename);
    if(!f)
        return;

    wxInt8 at[12][90][180];
    if(zu_read(f, at, sizeof at) != sizeof at) {
        m_FailedFiles.push_back(filename);
        m_sFailedMessage += _("corrupt file: ") + filename + "\n";
        wxLogMessage(climatology_pi + _("at file truncated"));
    } else {
        for(int j=0; j<90; j++)
            for(int k=0; k<180; k++) {
                long total = 0, totalcount = 0;
                for(int i=0; i<12; i++) {
                    if(at[i][j][k] == -128)
                        m_at[i][j][k] = 32767;
                    else {
                        m_at[i][j][k] = at[i][j][k];
                        total += m_at[i][j][k];
                        totalcount++;
                    }
                    if(totalcount)
                        m_at[12][j][k] = total / totalcount;
                    else
                        m_at[12][j][k] = 32767;
                }
            }
        m_dlg.m_cbAirTemperature->Enable();
    }
    zu_close(f);
}

void ClimatologyOverlayFactory::ReadCloudData(wxString filename)
{
    ZUFILE *f = OpenClimatologyDataFile(filename);
    if(!f)
        return;

    wxUint8 cld[12][90][180];
    if(zu_read(f, cld, sizeof cld) != sizeof cld) {
        m_FailedFiles.push_back(filename);
        m_sFailedMessage += _("corrupt file: ") + filename + "\n";
        wxLogMessage(climatology_pi + _("cld file truncated"));
    } else {
        for(int j=0; j<90; j++)
            for(int k=0; k<180; k++) {
                long total = 0, totalcount = 0;
                for(int i=0; i<12; i++) {
                    if(cld[i][j][k] == 255)
                        m_cld[i][j][k] = 32767;
                    else {
                        m_cld[i][j][k] = cld[i][j][k]*40;
                        total += m_cld[i][j][k];
                        totalcount++;
                    }
                    if(totalcount)
                        m_cld[12][j][k] = total / totalcount;
                    else
                        m_cld[12][j][k] = 32767;
                }
            }
        m_dlg.m_cbCloudCover->Enable();
    }
    zu_close(f);
}

void ClimatologyOverlayFactory::ReadPrecipitationData(wxString filename)
{
    ZUFILE *f = OpenClimatologyDataFile(filename);
    if(!f)
        return;

    wxUint8 precip[12][72][144];
    if(zu_read(f, precip, sizeof precip) != sizeof precip) {
        m_FailedFiles.push_back(filename);
        m_sFailedMessage += _("corrupt file: ") + filename + "\n";
        wxLogMessage(climatology_pi + _("precip file truncated"));
    } else {
        for(int j=0; j<72; j++)
            for(int k=0; k<144; k++) {
                long total = 0, totalcount = 0;
                for(int i=0; i<12; i++) {
                    if(precip[i][j][k] == 255)
                        m_precip[i][j][k] = 32767;
                    else {
                        m_precip[i][j][k] = precip[i][j][k]*100;
                        total += m_precip[i][j][k];
                        totalcount++;
                    }
                    if(totalcount)
                        m_precip[12][j][k] = total / totalcount;
                    else
                        m_precip[12][j][k] = 32767;
                }
            }
        m_dlg.m_cbPrecipitation->Enable();
    }
    zu_close(f);
}

void ClimatologyOverlayFactory::ReadRelativeHumidityData(wxString filename)
{
    ZUFILE *f = OpenClimatologyDataFile(filename);
    if(!f)
        return;

    wxUint8 rhum[12][180][360];
    if(zu_read(f, rhum, sizeof rhum) != sizeof rhum) {
        m_FailedFiles.push_back(filename);
        m_sFailedMessage += _("corrupt file: ") + filename + "\n";
        wxLogMessage(climatology_pi + _("relative humidity file truncated"));
    } else {
        for(int j=0; j<180; j++)
            for(int k=0; k<360; k++) {
                long total = 0, totalcount = 0;
                for(int i=0; i<12; i++) {
                    if(rhum[i][j][k] == 255)
                        m_rhum[i][j][k] = 32767;
                    else {
                        m_rhum[i][j][k] = rhum[i][j][k];
                        total += m_rhum[i][j][k];
                        totalcount++;
                    }
                    if(totalcount)
                        m_rhum[12][j][k] = total / totalcount;
                    else
                        m_rhum[12][j][k] = 32767;
                }
            }
        m_dlg.m_cbRelativeHumidity->Enable();
    }
    zu_close(f);
}

void ClimatologyOverlayFactory::ReadLightningData(wxString filename)
{
    ZUFILE *f = OpenClimatologyDataFile(filename);
    if(!f)
        return;

    wxUint8 lightn[12][180][360];
    if(zu_read(f, lightn, sizeof lightn) != sizeof lightn) {
        m_FailedFiles.push_back(filename);
        m_sFailedMessage += _("corrupt file: ") + filename + "\n";
        wxLogMessage(climatology_pi + _("lightning file truncated"));
    } else {
        for(int j=0; j<180; j++)
            for(int k=0; k<360; k++) {
                long total = 0, totalcount = 0;
                for(int i=0; i<12; i++) {
                    m_lightn[i][j][k] = lightn[i][j][k];
                    total += m_lightn[i][j][k];
                    totalcount++;
                }
                m_lightn[12][j][k] = total / totalcount;
            }
        m_dlg.m_cbLightning->Enable();
    }
    zu_close(f);
}

void ClimatologyOverlayFactory::ReadSeaDepthData(wxString filename)
{
    ZUFILE *f = OpenClimatologyDataFile(filename);
    if(!f)
        return;

    wxInt8 seadepth[180][360];
    if(zu_read(f, seadepth, sizeof seadepth) != sizeof seadepth) {
        m_FailedFiles.push_back(filename);
        m_sFailedMessage += _("corrupt file: ") + filename + "\n";
        wxLogMessage(climatology_pi + _("seadepth file truncated"));
    } else {
        for(int j=0; j<180; j++)
            for(int k=0; k<360; k++)
                if(seadepth[j][k] == -128)
                    m_seadepth[j][k] = 32767;
                else
                    m_seadepth[j][k] = seadepth[j][k];

        m_dlg.m_cbSeaDepth->Enable();
    }
    zu_close(f);
}

bool ClimatologyOverlayFactory::ReadCycloneData(wxString filename,
                                                std::list<Cyclone*> &cyclones,
                                                bool south)
{
    ZUFILE *f;
    wxString path = ClimatologyDataDirectory();

    // 1. Prefer decompressed file
    wxString decompressed = path + filename;
    if (wxFileExists(decompressed)) {
        wxLogMessage("Climatology: using existing decompressed cyclone file %s", decompressed);
        f = zu_open(decompressed.mb_str(), "rb");
        if (!f)
            goto missing;
        goto parse;
    }

    // 2. Otherwise fall back to TryOpenFile (handles .gz)
    if (!(f = TryOpenFile(path + filename)))
        goto missing;

parse:
    wxUint16 lyear, llastmonth;
    Cyclone *cyclone;

    while (zu_read(f, &lyear, sizeof lyear) == sizeof lyear) {
#ifdef __MSVC__
        if (lyear < 1972)
            lyear = 1972;
#endif
        cyclone = new Cyclone;
        llastmonth = 0;

        wxUint8 wk;
        wxUint16 press;
        CycloneState::State lastcyclonestate = CycloneState::UNKNOWN;
        CycloneDateTime lastdatetime(1, 1, 1900, 0);
        wxInt16 lastlat = -10000, lastlon = -10000;

        for (;;) {
            signed char state;
            if (zu_read(f, &state, sizeof state) != sizeof state)
                goto corrupted;

            if (state == -128)
                break;

            CycloneState::State cyclonestate;
            switch (state) {
            case '*': cyclonestate = CycloneState::TROPICAL; break;
            case 'S': cyclonestate = CycloneState::SUBTROPICAL; break;
            case 'E': cyclonestate = CycloneState::EXTRATROPICAL; break;
            case 'W': cyclonestate = CycloneState::WAVE; break;
            case 'L': cyclonestate = CycloneState::REMANENT; break;
            case 'D': case 'X': cyclonestate = CycloneState::UNKNOWN; break;
            default: goto corrupted;
            }

            char lday, lmonth;
            if (zu_read(f, &lday, sizeof lday) != sizeof lday ||
                zu_read(f, &lmonth, sizeof lmonth) != sizeof lmonth)
                goto corrupted;

            if (lmonth < llastmonth)
                lyear++;
            llastmonth = lmonth;

            wxDateTime::Month month = (wxDateTime::Month)(lmonth - 1);
            int day = lday / 4, hour = (lday % 4) * 6;
            if (lmonth < 1 || lmonth > 12 ||
                day < 1 || day > wxDateTime::GetNumberOfDays(month, lyear) ||
                hour < 0 || hour >= 24)
                goto corrupted;

            wxInt16 lat, lon;
            if (zu_read(f, &lat, sizeof lat) != sizeof lat ||
                zu_read(f, &lon, sizeof lon) != sizeof lon)
                goto corrupted;

            if (std::abs((double)lat / 10) >= 90 ||
                (double)lon / 10 > 15 ||
                (double)lon / 10 < -360)
                goto corrupted;

            if (lastlat != -10000) {
                cyclone->states.push_back(
                    new CycloneState(lastcyclonestate, lastdatetime,
                                     (south ? -1 : 1) * (double)lastlat / 10,
                                     (double)lastlon / 10,
                                     (south ? -1 : 1) * (double)lat / 10,
                                     (double)lon / 10,
                                     wk, press));
            }

            lastcyclonestate = cyclonestate;
            lastdatetime = CycloneDateTime(day, month, lyear, hour);
            lastlat = lat;
            lastlon = lon;

            if (zu_read(f, &wk, sizeof wk) != sizeof wk ||
                zu_read(f, &press, sizeof press) != sizeof press)
                goto corrupted;
        }

        cyclones.push_back(cyclone);
    }

    zu_close(f);
    return true;

corrupted:
    delete cyclone;
    m_sFailedMessage += _("corrupt file: ") + filename + "\n";
    wxLogMessage(climatology_pi + _("cyclone data corrupt: ") + filename
                 + wxString::Format(" at %ld", zu_tell(f)));
    zu_close(f);

missing:
    m_FailedFiles.push_back(filename);
    return false;
}

void ClimatologyOverlayFactory::BuildCycloneCache()
{
    std::list<Cyclone*> *cyclones[6] = {&m_epa, &m_wpa, &m_spa, &m_atl, &m_nio, &m_she};

    // make sure we have all the cyclone theatres
    for (int i = 0; i < 6; i++)
        if (cyclones[i]->empty())
            return;

    // use cached filter params instead of UI
    int statemask     = m_cycloneParams.statemask;
    int minwindspeed  = m_cycloneParams.minwindspeed;
    int maxpressure   = m_cycloneParams.maxpressure;
    wxDateTime start  = m_cycloneParams.startDate;
    wxDateTime end    = m_cycloneParams.endDate;

    bool allowElNino      = m_cycloneParams.allowElNino;
    bool allowLaNina      = m_cycloneParams.allowLaNina;
    bool allowNeutral     = m_cycloneParams.allowNeutral;
    bool allowNotAvailable= m_cycloneParams.allowNotAvailable;

    for (int i = 0; i < CYCLONE_CACHE_SEMAPHORE_COUNT; i++)
        m_cyclone_cache_semaphore.Wait();

    m_cyclone_cache.clear();
    wxStopWatch sw;

    for (int i = 0; i < 6; i++) {
        for (std::list<Cyclone*>::iterator it = cyclones[i]->begin();
             it != cyclones[i]->end(); ++it) {

            Cyclone *s = *it;

            for (std::list<CycloneState*>::iterator it2 = s->states.begin();
                 it2 != s->states.end(); ++it2) {

                CycloneState* st = *it2;

                if (st->windknots < minwindspeed)
                    continue;

                if (st->pressure > maxpressure)
                    continue;

#ifndef __OCPN__ANDROID__
                wxDateTime dt = st->datetime.DateTime();
                if ((dt < start) || (dt > end))
                    continue;
#endif
                // el nino test
                int year = st->datetime.year;
                std::map<int, ElNinoYear>::iterator ipt = m_ElNinoYears.find(year);
                if (ipt == m_ElNinoYears.end()) {
                    if (!allowNotAvailable && !m_ElNinoYears.size())
                        continue;
                } else {
                    ElNinoYear elninoyear = ipt->second;
                    int month = st->datetime.month;
                    double value = elninoyear.months[month];

                    if (isnan(value)) {
                        if (!allowNotAvailable)
                            continue;
                    } else {
                        if (value >= .5) {
                            if (!allowElNino)
                                continue;
                        } else if (value <= -.5) {
                            if (!allowLaNina)
                                continue;
                        } else if (!allowNeutral)
                            continue;
                    }
                }

                if (!((1 << st->state) & statemask))
                    continue;

                int lon[2], lat[2];
                for (int j = 0; j < 2; j++) {
                    lon[j] = st->lon[0] < 15 ? st->lon[j] : st->lon[j] - 360;
                    lat[j] = st->lat[j];
                }

                for (int loni = wxMin(lon[0], lon[1]); loni <= wxMax(lon[0], lon[1]); loni++)
                    for (int lati = wxMin(lat[0], lat[1]); lati <= wxMax(lat[0], lat[1]); lati++) {
                        int hash = (loni * 180 + lati)*12 + st->datetime.month;
                        m_cyclone_cache[hash].push_back(st);
                    }
            }
        }
    }

    wxLogMessage(climatology_pi + _("cyclone cache time ") +
                 wxString::Format("%ld ms", sw.Time()));

    for (int i = 0; i < CYCLONE_CACHE_SEMAPHORE_COUNT; i++)
        m_cyclone_cache_semaphore.Post();

    if (m_cyclone_cache.empty()) {
        wxLogWarning("Climatology: Cyclone cache is empty after filtering");
    }
}


static double strtod_nan(const char *str)
{
    if(!str)
        return NAN;

    double ret;
    if(wxString::FromUTF8(str).ToDouble(&ret))
        return ret;

    return NAN;
}

bool ClimatologyOverlayFactory::ReadElNinoYears(wxString filename)
{
    char line[128];
    int header = 1;
    FILE *f;
    wxString path = ClimatologyDataDirectory();
    if(!(f = fopen((path + filename).mb_str(), "r"))) {
        path = ClimatologyDataDirectory();
        if(!(f = fopen((path + filename).mb_str(), "r")))
            goto missing;
    }

    while(fgets(line, sizeof line, f)) {
        if(header)
            header--;
        else {
            //char *saveptr;
            int year = strtol(strtok(line, " "), 0, 10);
            ElNinoYear elninoyear;
            for(int i=0; i<12; i++)
                elninoyear.months[i] = strtod_nan(strtok(0, " \n"));

            m_ElNinoYears[year] = elninoyear;
        }
    }
    fclose(f);
    return true;
missing:
    wxLogMessage(climatology_pi + _("failed to open file: ") + filename);
    m_FailedFiles.push_back(filename);
    return false;
}

bool ClimatologyOverlayFactory::HasDataFor(int setting, int month)
{
    switch (setting) {
    case ClimatologyOverlaySettings::WIND:
        return m_WindData[month] != NULL;

    case ClimatologyOverlaySettings::CURRENT:
        return m_CurrentData[month] != NULL;

    case ClimatologyOverlaySettings::SLP:
        return m_slp[month] != NULL;

    case ClimatologyOverlaySettings::SST:
        return m_sst[month] != NULL;

    case ClimatologyOverlaySettings::AT:
        return m_at[month] != NULL;

    case ClimatologyOverlaySettings::CLOUD:
        return m_cld[month] != NULL;

    case ClimatologyOverlaySettings::PRECIPITATION:
        return m_precip[month] != NULL;

    case ClimatologyOverlaySettings::RELATIVE_HUMIDITY:
        return m_rhum[month] != NULL;

    case ClimatologyOverlaySettings::LIGHTNING:
        return m_lightn[month] != NULL;

    case ClimatologyOverlaySettings::SEADEPTH:
        // m_seadepth is a fixed array, always allocated.
        // If you later add a "loaded" flag, check it here.
        return true;

// Not in the enum. Handled separately is vector.
//   case ClimatologyOverlaySettings::CYCLONES:
//        return !m_epa.empty() || !m_wpa.empty() || !m_spa.empty() ||
//               !m_atl.empty() || !m_nio.empty() || !m_she.empty();

    default:
        return false;
    }
}

void ClimatologyOverlayFactory::RenderGLOverlay(wxGLContext *pcontext,
                                                PlugIn_ViewPort *vp)
{
    if (!m_bCompletedLoading)
        return;

    glEnable(GL_BLEND);

    piDC pidc;
    pidc.SetVP(vp);

    RenderOverlay(pidc, *vp);
}





void ClimatologyOverlayFactory::DrawGLTexture(GLuint tex1, GLuint tex2,
                                              double dpos,
                                              PlugIn_ViewPort &vp,
                                              double transparency)
{
    // Screen quad
    float x = 0;
    float y = 0;
    float w = vp.pix_width;
    float h = vp.pix_height;

    float coords[8] = {
        x,     y,
        x+w,   y,
        x,     y+h,
        x+w,   y+h
    };

    float uv[8] = {
        0, 0,
        1, 0,
        0, 1,
        1, 1
    };

    // Use the shader program
    glUseProgram(pi_texture_2DA_shader_program);

    // Attributes
    GLint mPosAttrib = glGetAttribLocation(pi_texture_2DA_shader_program, "aPos");
    GLint mUvAttrib  = glGetAttribLocation(pi_texture_2DA_shader_program, "aUV");

    // Texture samplers
    GLint texUni1 = glGetUniformLocation(pi_texture_2DA_shader_program, "uTex1");
    GLint texUni2 = glGetUniformLocation(pi_texture_2DA_shader_program, "uTex2");
    glUniform1i(texUni1, 0);
    glUniform1i(texUni2, 1);

    // Blend factor
    GLint blendLoc = glGetUniformLocation(pi_texture_2DA_shader_program, "blendFactor");
    glUniform1f(blendLoc, (float)dpos);

    // Transparency
    float colorv[4] = {0, 0, 0, -(float)transparency};
    GLint colloc = glGetUniformLocation(pi_texture_2DA_shader_program, "color");
    glUniform4fv(colloc, 1, colorv);

    // No VBOs
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);

    glVertexAttribPointer(mPosAttrib, 2, GL_FLOAT, GL_FALSE, 0, coords);
    glEnableVertexAttribArray(mPosAttrib);

    glVertexAttribPointer(mUvAttrib, 2, GL_FLOAT, GL_FALSE, 0, uv);
    glEnableVertexAttribArray(mUvAttrib);

    // Identity transform
    mat4x4 I;
    mat4x4_identity(I);

    GLint matloc = glGetUniformLocation(pi_texture_2DA_shader_program, "TransformMatrix");
    if (matloc != -1)
        glUniformMatrix4fv(matloc, 1, GL_FALSE, (const GLfloat*)I);

    // Bind textures
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, tex1);

    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, tex2);

    // Draw
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
}


/* give value for y at a given x location on a segment */
static double interp_value(double v0, double v1, double d)
{
    return (1-d)*v0 + d*v1;
}

// interpolate two angles in range +- PI, with resulting angle in the same range
static double interp_angle(double a0, double a1, double d)
{
    if(isnan(a0)) return a1;
    if(isnan(a1)) return a0;
    if(a0 - a1 > M_PI) a0 -= 2*M_PI;
    else if(a1 - a0 > M_PI) a1 -= 2*M_PI;
    double a = (1-d)*a0 + d*a1;
    if(a < -M_PI) a += 2*M_PI;
    return a;
}

static double ArrayValue(wxInt16 *a, int index)
{
    int v = a[index];
    if(v == 32767)
        return NAN;
    return v;
}

static double InterpArray(double x, double y, wxInt16 *a, int h)
{
    if(y<0) y+=h;
    int x0 = floor(x), x1 = x0+1;
    int y0 = floor(y), y1 = y0+1;
    int y1v = y1;
    if(y1v == h) y1v = 0;

    double v00 = ArrayValue(a, x0*h + y0), v01 = ArrayValue(a, x0*h + y1v);
    double v10 = ArrayValue(a, x1*h + y0), v11 = ArrayValue(a, x1*h + y1v);

    double v0 = interp_value(v00, v01, y-y0);
    double v1 = interp_value(v10, v11, y-y0);
    return      interp_value(v0,   v1, x-x0);
}

double WindData::WindPolar::Value(enum Coord coord, int dir_cnt)
{
    if(gale == 255)
        return NAN;

    if(coord == DIRECTION) // maybe should do most likely here rather than vector average?
        return atan2(Value(U, dir_cnt), Value(V, dir_cnt));

    int totald = 0, totals = 0;
    for(int i=0; i<dir_cnt; i++) {
        totald += directions[i];

        double mul = 0;
        switch(coord) {
        case U: mul = sin(i*2*M_PI/dir_cnt); break;
        case V: mul = cos(i*2*M_PI/dir_cnt); break;
        case MAG: mul = 1; break;
        default: printf("error, invalid coord: %d\n", coord);
        }

        totals += mul*speeds[i]*directions[i];
    }
    assert(totald != 0);
    return (double)totals / totald;
}

double CurrentData::Value(enum Coord coord, int xi, int yi)
{
    if(xi < 0 || xi >= latitudes)
        return NAN;

    double u = data[0][xi*longitudes + yi], v = data[1][xi*longitudes + yi];
    switch(coord) {
    case U: return u;
    case V: return v;
    case MAG: return hypot(u, v);
    case DIRECTION: return !u && !v ? NAN : atan2(u, v);
    default: printf("error, invalid coord: %d\n", coord);
    }
    return NAN;
}

double WindData::InterpWind(enum Coord coord, double x, double y)
{
    double latoff = 90.0/latitudes, lonoff = 180.0/longitudes;

    double xi = latitudes*(.5 + (x - latoff)/180.0);
    double yi = longitudes*positive_degrees(y - lonoff)/360.0;

    int h = longitudes, d = dir_cnt;

    if(yi<0) yi+=h;

    int x0 = floor(xi), x1 = x0+1;
    int y0 = floor(yi), y1 = y0+1;
    int y1v = y1;
    if(x1 == latitudes)
        x1 = x0;
    if(y1v == h) y1v = 0;

    double v00 = data[x0*h + y0].Value(coord, d), v01 = data[x0*h + y1v].Value(coord, d);
    double v10 = data[x1*h + y0].Value(coord, d), v11 = data[x1*h + y1v].Value(coord, d);

    if(coord == DIRECTION) {
        double a0 = interp_angle(v00, v01, yi-y0);
        double a1 = interp_angle(v10, v11, yi-y0);
        return      interp_angle(a0,  a1,  xi-x0 ) * 180/M_PI;
    }

    double v0 = interp_value(v00, v01, yi-y0);
    double v1 = interp_value(v10, v11, yi-y0);
    return      interp_value(v0,  v1,  xi-x0 ) / speed_multiplier;
}

double CurrentData::InterpCurrent(enum Coord coord, double x, double y)
{
    y = positive_degrees(y);
    double xi = (latitudes-1)*(.5 - x/160.0);
    double yi = longitudes*y/360.0;
    int h = longitudes;

    if(xi<0) xi+=latitudes;

    int x0 = floor(xi), x1 = x0+1;
    int y0 = floor(yi), y1 = y0+1;
    int y1v = y1;
    if(y1v == h) y1v = 0;

    double v00 = Value(coord, x0, y0), v01 = Value(coord, x0, y1);
    double v10 = Value(coord, x1, y0), v11 = Value(coord, x1, y1);

    if(coord == DIRECTION) {
        double a0 = interp_angle(v00, v01, yi-y0);
        double a1 = interp_angle(v10, v11, yi-y0);
        return      interp_angle(a0,  a1,  xi-x0 ) * 180/M_PI;
    }

    double v0 = interp_value(v00, v01, yi-y0);
    double v1 = interp_value(v10, v11, yi-y0);
    return      interp_value(v0,  v1,  xi-x0 );
}

static double interp_table_value(double x, double x1, double x2, double y1, double y2)
{
    if(x == x1)
        return y1;
    if(x == x2)
        return y2;
    return (y2 - y1)*(x - x1)/(x2 - x1) + y1;
}

static double InterpTable(double ind, const double table[], int tablesize)
{
    int ind1 = floor(ind), ind2 = ceil(ind);
    if(ind1 < 0)
        return table[0];
   if(ind2 >= tablesize)
        return table[tablesize - 1];

    return interp_table_value(ind, ind1, ind2, table[ind1], table[ind2]);
}
 
double ClimatologyOverlayFactory::getValueMonth(enum Coord coord, int setting,
                                                double lat, double lon, int month)
{
    if(!m_bCompletedLoading)
        return NAN;

    if(coord != MAG &&
       setting != ClimatologyOverlaySettings::WIND &&
       setting != ClimatologyOverlaySettings::CURRENT)
        return NAN;

    if(isnan(lat) || isnan(lon))
        return NAN;

    switch(setting) {
    case ClimatologyOverlaySettings::WIND:
        if(m_WindData[month])
            return m_WindData[month]->InterpWind(coord, lat, lon);
        break;
    case ClimatologyOverlaySettings::CURRENT:
        if(m_CurrentData[month])
            return m_CurrentData[month]->InterpCurrent(coord, lat, lon);
        break;
    case ClimatologyOverlaySettings::SLP:
        return InterpArray((-lat+90)/2-.5, positive_degrees(lon-1.5)/2,
                           m_slp[month][0], 180) * .01f + 1000.0;
    case ClimatologyOverlaySettings::SST:
        return InterpArray((-lat+90)-.5, positive_degrees(lon-.5),
                           m_sst[month][0], 360) * .001f + 15.0;
    case ClimatologyOverlaySettings::AT:
        return InterpArray((-lat+90)/2-.5, positive_degrees(lon-.5)/2,
                           m_at[month][0], 180) / 3.0;
    case ClimatologyOverlaySettings::CLOUD:
        return InterpArray((-lat+90)/2-.5, positive_degrees(lon-.5)/2,
                           m_cld[month][0], 180) * .001f * 12.5;
    case ClimatologyOverlaySettings::PRECIPITATION:
        return InterpArray((-lat+90)/2.5, positive_degrees(lon-2)/2.5,
                           m_precip[month][0], 144) * .002f;
    case ClimatologyOverlaySettings::RELATIVE_HUMIDITY:
        return InterpArray((-lat+90), positive_degrees(lon-.5),
                           m_rhum[month][0], 360)/2.0;
    case ClimatologyOverlaySettings::LIGHTNING:
        return InterpArray((-lat+90), positive_degrees(lon-.5),
                           m_lightn[month][0], 360);
    case ClimatologyOverlaySettings::SEADEPTH:
    {
        double ind = InterpArray((-lat+90), positive_degrees(lon-.5),
                           m_seadepth[0], 360);
        const double table[] = {0, 10, 20, 30, 50, 75, 100, 125, 150,
                                200, 250, 300, 400, 500, 600, 700, 800,
                                900, 1000, 1100, 1200, 1300, 1400, 1500,
                                1750, 2000, 2500, 3000, 3500, 4000, 4500,
                                5000, 5500, 6000, 6500, 7000, 7500, 8000,
                                9000, 10000};
        return InterpTable(ind, table, (sizeof table) / (sizeof *table));
    }
    }
    return NAN;
}

double ClimatologyOverlayFactory::getValue(enum Coord coord, int setting,
                                           double lat, double lon, wxDateTime *date)
{
    int month, nmonth;
    double dpos;
    GetDateInterpolation(date, month, nmonth, dpos);

    double v1 = getValueMonth(coord, setting, lat, lon, month);
    double v2 = getValueMonth(coord, setting, lat, lon, nmonth);

    // ------------------------------------------------------------
    // Missing data?  Return NAN immediately.
    // ------------------------------------------------------------
    if (isnan(v1) || isnan(v2))
        return NAN;

    // ------------------------------------------------------------
    // Direction interpolation (circular wrap-around)
    // ------------------------------------------------------------
    if (coord == DIRECTION) {
        if (v1 - v2 > 180) v1 -= 360;
        if (v2 - v1 > 180) v2 -= 360;
        return positive_degrees(dpos * v1 + (1 - dpos) * v2);
    }

    // ------------------------------------------------------------
    // Linear interpolation for scalar values
    // ------------------------------------------------------------
    return dpos * v1 + (1 - dpos) * v2;
}

double ClimatologyOverlayFactory::getCurValue(enum Coord coord, int setting,
                                              double lat, double lon)
{
    if (!m_bCompletedLoading)
        return NAN;

    if (isnan(lat) || isnan(lon))
        return NAN;

    int month = m_CurrentTimeline.GetMonth();

    switch (setting) {

    case ClimatologyOverlaySettings::WIND:
        if (!m_WindData[month])
            return NAN;
        return m_WindData[month]->InterpWind(coord, lat, lon);

    case ClimatologyOverlaySettings::CURRENT:
        if (!m_CurrentData[month])
            return NAN;
        return m_CurrentData[month]->InterpCurrent(coord, lat, lon);

    default:
        return NAN;
    }
}


double ClimatologyOverlayFactory::getCalibratedValueMonth(enum Coord coord, int setting, double lat, double lon, int month)
{
    double v = getValueMonth(coord, setting, lat, lon, month);
    if(coord == DIRECTION)
        return v;

    return m_dlg.m_cfgdlg->m_Settings.CalibrateValue(setting, v);
}

double ClimatologyOverlayFactory::GetMin(int setting)
{
    switch(setting) {
    case ClimatologyOverlaySettings::WIND: return 0;
    case ClimatologyOverlaySettings::CURRENT: return 0;
    case ClimatologyOverlaySettings::SLP: return 920;
    case ClimatologyOverlaySettings::SST: return 0;
    case ClimatologyOverlaySettings::AT:  return -50;
    case ClimatologyOverlaySettings::CLOUD: return 0;
    case ClimatologyOverlaySettings::PRECIPITATION:  return 0;
    case ClimatologyOverlaySettings::RELATIVE_HUMIDITY:  return 0;
    case ClimatologyOverlaySettings::LIGHTNING:  return 0;
    case ClimatologyOverlaySettings::SEADEPTH:  return 0;
    default: return 0;
    }
}

double ClimatologyOverlayFactory::GetMax(int setting)
{
    switch(setting) {
    case ClimatologyOverlaySettings::WIND: return 100;
    case ClimatologyOverlaySettings::CURRENT: return 10;
    case ClimatologyOverlaySettings::SLP: return 1080;
    case ClimatologyOverlaySettings::SST: return 35;
    case ClimatologyOverlaySettings::AT:  return 50;
    case ClimatologyOverlaySettings::CLOUD: return 8;
    case ClimatologyOverlaySettings::PRECIPITATION:  return 1000;
    case ClimatologyOverlaySettings::RELATIVE_HUMIDITY:  return 100;
    case ClimatologyOverlaySettings::LIGHTNING:  return 100; // ???
    case ClimatologyOverlaySettings::SEADEPTH:  return 40;
    default: return NAN;
    }
}

#define EPS 2e-14
#define EPS2 2e-7

static inline int TestIntersectionXY(double x1, double y1, double x2, double y2,
                              double x3, double y3, double x4, double y4)
{
    double ax = x2 - x1, ay = y2 - y1;
    double bx = x3 - x4, by = y3 - y4;
    double cx = x1 - x3, cy = y1 - y3;

    double denom = ay * bx - ax * by;

    if(fabs(denom) < EPS) { /* parallel or really close to parallel */
//        if(fabs((y1*ax - ay*x1)*bx - (y3*bx - by*x3)*ax) > EPS2)
//            return 0; /* different intercepts, no intersection */
      return 1;
//        return 0;
    }

    double recip = 1 / denom;
    double na = (by * cx - bx * cy) * recip;
    if(na < -EPS2 || na > 1 + EPS2)
        return 0;

    double nb = (ax * cy - ay * cx) * recip;
    if(nb < -EPS2 || nb > 1 + EPS2)
        return 0;

    return 1;
}

int ClimatologyOverlayFactory::CycloneTrackCrossings(double lat1, double lon1, double lat2, double lon2,
                                                     const wxDateTime &date, int dayrange)
{
    // if dayrange is zero, we are just probing, so other parameters are likely invalid
    if(!dayrange)
        return 0;

    m_cyclone_cache_semaphore.Wait();
    /* build hash table cache to speed up cyclone crossing calculation */
    if(m_cyclone_cache.empty()) {
        m_cyclone_cache_semaphore.Post();
        return -1;
    }
        
    int lon_min = wxMin(lon1, lon2), lon_max = wxMax(lon1, lon2);

    if(lon_min > 15 || lon_max > 15)
        lon_min -= 360, lon_max -= 360;

    int lat_min = wxMin(lat1, lat2), lat_max = wxMax(lat1, lat2);

    int day = date.GetMonth()*365/12 + date.GetDay()-1;
    int day1 = day - dayrange/2, day2 = day + dayrange/2;
    if(day1 < 0) day1 += 365;
    if(day2 >= 365) day2 -= 365;
    int day_min = wxMin(day1, day2), day_max = wxMax(day1, day2);

    int month_min = day_min * 12 / 365, month_max = day_max * 12 / 365;

    for(int loni = lon_min; loni <= lon_max; loni++)
        for(int lati = lat_min; lati <= lat_max; lati++) {
            int monthi = month_min;
            do {
                if(monthi==12) monthi = 0;

                int hash = (floor((double)loni) * 180
                            + floor((double)lati))*12 + monthi;

                std::list<CycloneState*> &cyclonestates = m_cyclone_cache[hash];
                for(std::list<CycloneState*>::iterator it = cyclonestates.begin();
                    it != cyclonestates.end(); it++) {
                    wxPoint p;
                    CycloneState *ss = *it;
                        
                    int cday = ss->datetime.month*365/12 + ss->datetime.day - 1;
                    int daydiff = cday - day;
                    if(daydiff > 183)
                        daydiff = 365 - daydiff;

                    if(daydiff < dayrange/2) {
                        /* we should split all cyclones at 15 degrees longitude... until then... */
                        while(lon1 - ss->lon[0] > 180) lon1 -= 360, lon2 -= 360;
                        while(lon1 - ss->lon[0] < -180) lon1 += 360, lon2 += 360;
                                
                        if(TestIntersectionXY(lat1, lon1, lat2, lon2,
                                              ss->lat[0], ss->lon[0],
                                              ss->lat[1], ss->lon[1])) {
                            m_cyclone_cache_semaphore.Post();
                            return 1;
                        }
                    }
                }
            } while(++monthi <= month_max);
        }

    m_cyclone_cache_semaphore.Post();
    return 0;
}



void ClimatologyOverlayFactory::RenderOverlayMap(int setting,
                                                 PlugIn_ViewPort &vp)
{
    if (!m_bCompletedLoading)
        return;

    if (!m_Settings.Settings[setting].m_bOverlayMap)
        return;

    int month, nmonth;
    double dpos;

    if (setting == ClimatologyOverlaySettings::SEADEPTH) {
        month = nmonth = 0;
        dpos = 1;
    } else {
        GetDateInterpolation(NULL, month, nmonth, dpos);
    }

    if (!m_Settings.Settings[setting].m_bOverlayInterpolation) {
        nmonth = month;
        dpos = 1;
    }

    ClimatologyOverlay &O1 = m_pOverlay[month][setting];
    ClimatologyOverlay &O2 = m_pOverlay[nmonth][setting];

    // Ensure data exists
    if (!HasDataFor(setting, month) || !HasDataFor(setting, nmonth))
        return;

    // DC fallback (unchanged)
    wxString msg = _("Climatology overlay map unsupported unless OpenGL is enabled");

    wxMemoryDC mdc;
    wxBitmap bm(1000, 1000);
    mdc.SelectObject(bm);
    mdc.Clear();

    wxFont font(10, wxFONTFAMILY_DEFAULT, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL);
    mdc.SetFont(font);
    mdc.SetPen(*wxTRANSPARENT_PEN);
    mdc.SetBrush(wxColour(243, 47, 229));

    int w, h;
    mdc.GetMultiLineTextExtent(msg, &w, &h);
    h += 2;

    int label_offset = 10;
    int wdraw = w + (label_offset * 2);

    mdc.DrawRectangle(0, 0, wdraw, h);
    mdc.DrawLabel(msg, wxRect(label_offset, 0, wdraw, h),
                  wxALIGN_LEFT | wxALIGN_CENTRE_VERTICAL);

    mdc.SelectObject(wxNullBitmap);

    wxBitmap sbm = bm.GetSubBitmap(wxRect(0, 0, wdraw, h));
    int x = vp.pix_width, y = vp.pix_height;

    m_dc->DrawBitmap(sbm,
                     (x - wdraw) / 2,
                     y - (GetChartbarHeight() + h),
                     false);
}


ZUFILE *ClimatologyOverlayFactory::TryOpenFile(wxString filename)
{
    const wxString ext = ".gz";
    ZUFILE *f = zu_open(filename.mb_str(), "rb", ZU_COMPRESS_AUTO);
    if(!f) {
        f = zu_open((filename+ext).mb_str(), "rb", ZU_COMPRESS_AUTO);
        if(f)
            wxLogMessage("climatology found compressed data: " + filename+ext);
    }

    if(!f)
        wxLogMessage("climatology failed to read: " + filename);


    return f;
}

void ClimatologyOverlayFactory::RenderNumber(wxPoint p, double v, const wxColour &color)
{
    if (!m_bCompletedLoading)
        return; 
	wxString text;	
    if(isnan(v))
        text = _("N/A");
    else
        text.Printf("%.0f", round(v));

    m_dc->SetTextForeground(color);

    wxCoord w, h;
    m_dc->GetTextExtent(text, &w, &h);
    m_dc->DrawText(text, p.x-w/2, p.y-h/2);
}

void ClimatologyOverlayFactory::RenderIsoBars(int setting, PlugIn_ViewPort &vp)
{
	 if (!m_bCompletedLoading)
        return;
	recompute:
    if(!m_Settings.Settings[setting].m_bIsoBars)
        return;

    int month = m_bAllTimes ? 12 : m_CurrentTimeline.GetMonth();
    int day = 15; //m_CurrentTimeline.GetDay();
    if(setting == ClimatologyOverlaySettings::SEADEPTH)
        month = 0;

    ClimatologyIsoBarMap *&pIsobars = m_Settings.Settings[setting].m_pIsobars[month];
    double spacing = m_Settings.Settings[setting].m_iIsoBarSpacing, step;
    switch(m_Settings.Settings[setting].m_iIsoBarStep) {
    default: step = 4; break;
    case 1: step = 2; break;
    case 2: step = 1; break;
    case 3: step = .5; break;
    case 4: step = .25; break;
    }

    int units = m_Settings.Settings[setting].m_Units;
    if(pIsobars && !pIsobars->SameSettings(spacing, step, units, month, day)) {
        if(pIsobars->m_bComputing) {
            pIsobars->m_bNeedsRecompute = true;
            return;
        }

        delete pIsobars;
        pIsobars = NULL;
    }

    if( !pIsobars ) {
        pIsobars = new ClimatologyIsoBarMap(m_dlg.m_cfgdlg->SettingName(setting),
                                            spacing, step, *this, setting, units, month, day);
        bool ret = pIsobars->Recompute(&m_dlg);
        if(!ret) {
            if(pIsobars->m_bNeedsRecompute)
                goto recompute;
            pIsobars = NULL;

            m_dlg.m_cfgdlg->DisableIsoBars(setting);
            return;
        }
        goto recompute;
    }

    pIsobars->Plot(m_dc, vp);
}

double ClimatologyOverlayFactory::getCurCalibratedValue(Coord c, int idx, double lat, double lon)
{
    wxDateTime date = m_CurrentTimeline;
    return getValue(c, idx, lat, lon, &date);
}



void ClimatologyOverlayFactory::RenderNumbers(int setting, PlugIn_ViewPort &vp)
{
    if (!m_bCompletedLoading)
        return;
    if(!m_Settings.Settings[setting].m_bNumbers)
        return;

    double space = m_Settings.Settings[setting].m_iNumbersSpacing;
    wxPoint p;
    for(p.y = space/2; p.y <= vp.rv_rect.height-space/4; p.y+=space)
        for(p.x = space/2; p.x <= vp.rv_rect.width-space/4; p.x+=space) {
            double lat, lon;
            GetCanvasLLPix( &vp, p, &lat, &lon);
            RenderNumber(p, getCurCalibratedValue(MAG, setting, lat, lon), *wxBLACK);
        }
}

void ClimatologyOverlayFactory::RenderDirectionArrows(int setting, PlugIn_ViewPort &vp)
{
	if (!m_bCompletedLoading)
        return;
    if(!m_Settings.Settings[setting].m_bDirectionArrows)
        return;

    int month = m_bAllTimes ? 12 : m_CurrentTimeline.GetMonth();

    double step;
    switch(setting) {
    case ClimatologyOverlaySettings::WIND:
        if(!m_WindData[month])
            return;
        step = 360.0 / m_WindData[month]->longitudes;
        break;
    case ClimatologyOverlaySettings::CURRENT:
        if(!m_CurrentData[month])
            return;
        step = 360.0 / m_CurrentData[month]->longitudes;
        break;
    default: return;
    }

    double lengthtype = m_Settings.Settings[setting].m_iDirectionArrowsLengthType;
    int width = m_Settings.Settings[setting].m_iDirectionArrowsWidth;
    wxColour color = m_Settings.Settings[setting].m_cDirectionArrowsColor;
    double size = m_Settings.Settings[setting].m_iDirectionArrowsSize;
    double spacing = m_Settings.Settings[setting].m_iDirectionArrowsSpacing;

    int w = vp.pix_width, h = vp.pix_height;
    while((vp.lat_max - vp.lat_min) / step > h / spacing ||
          (vp.lon_max - vp.lon_min) / step > w / spacing)
        step *= 2;

    for(double lat = round(vp.lat_min/step)*step-1; lat <= vp.lat_max+1; lat+=step)
        for(double lon = round(vp.lon_min/step)*step-1; lon <= vp.lon_max+1; lon+=step) {
            double u = getCurValue(U, setting, lat, lon), v = getCurValue(V, setting, lat, lon);

            // for wind, flip direction to render where the wind is blowing
            if(setting == ClimatologyOverlaySettings::WIND)
               u = -u, v = -v;

            double mag = hypot(u, v);
			
			// Skip if missing or zero-length vector
			if (isnan(u) || isnan(v) || isnan(mag) || mag == 0)
				continue;

            double cstep, minv;
            if(setting == ClimatologyOverlaySettings::CURRENT) {
                cstep = .25;
                minv = .25;
            } else {
                cstep = 5;
                minv = 2;

                if(lengthtype) {
                    u /= 15;
                    v /= 15;
                }
            }

            if(!lengthtype)
                u/=mag, v/=mag;
            else if(mag < minv)
                continue;
            
            double t = vp.rotation;
            double x = -size*(u*cos(t) + v*sin(t)), y = size*(v*cos(t) - u*sin(t));
            wxPoint p;
            GetCanvasPixLL( &vp, &p, lat, lon );
            DrawLine(p.x+x, p.y+y, p.x-x, p.y-y, color, width);

            if(!lengthtype) {
                double ix = x, iy = y, dir = 1;
                while(mag > cstep) {
                    DrawLine(p.x+ix, p.y+iy, p.x+ix+x/3+dir*y/2, p.y+iy+y/3-dir*x/2,
                             color, width);
                    dir = -dir;
                    if(dir > 0) {
                        ix = ix*2/3;
                        iy = iy*2/3;
                    }
                    mag -= cstep;
                }
            }

            DrawLine(p.x-x, p.y-y, p.x-x/3+y*2/3, p.y-y/3-x*2/3,
                     color, width);
            DrawLine(p.x-x, p.y-y, p.x-x/3-y*2/3, p.y-y/3+x*2/3,
                     color, width);
        }
}

void ClimatologyOverlayFactory::RenderWindAtlas(PlugIn_ViewPort &vp)
{
    if (!m_bCompletedLoading)
        return;
    if(!m_dlg.m_cfgdlg->m_cbWindAtlasEnable->GetValue())
        return;

    int month, nmonth;
    double dpos;

    GetDateInterpolation(NULL, month, nmonth, dpos);

    if(!m_WindData[month] || !m_WindData[nmonth])
        return;

    double latstep = 180.0 / (m_WindData[month]->latitudes);
    double lonstep = 360.0 / (m_WindData[month]->longitudes);
    const double r = 12;
    double size = m_dlg.m_cfgdlg->m_sWindAtlasSize->GetValue();
    double spacing = m_dlg.m_cfgdlg->m_sWindAtlasSpacing->GetValue();

    int w = vp.pix_width, h = vp.pix_height;
    while((vp.lat_max - vp.lat_min) / latstep > h / spacing)
        latstep *= 2;
    while((vp.lon_max - vp.lon_min) / lonstep > w / spacing)
        lonstep *= 2;

    int dir_cnt = m_WindData[month]->dir_cnt;
    double latoff = 90.0/m_WindData[month]->latitudes, lonoff = 180.0/m_WindData[month]->longitudes;

    for(double lat = round(vp.lat_min/latstep)*latstep-latoff; lat <= vp.lat_max+1; lat+=latstep)
        for(double lon = round(vp.lon_min/lonstep)*lonstep-lonoff; lon <= vp.lon_max+1; lon+=lonstep) {
            double directions[64], speeds[64], gale, calm;
            if(!InterpolateWindAtlasTime(month, nmonth, dpos, lat, lon, directions, speeds, gale, calm))
                continue;

            wxPoint p;
            GetCanvasPixLL(&vp, &p, lat, lon);

            int opacity = m_dlg.m_cfgdlg->m_sWindAtlasOpacity->GetValue();

            if(gale*2 > calm)
                RenderNumber(p, 100.0*gale, wxColour(255, 0, 0, opacity));
            else if(calm > 0)
                RenderNumber(p, 100.0*calm, wxColour(0, 0, 180, opacity));
            
            wxColour c(0, 0, 0, opacity);
            DrawCircle(p.x, p.y, r, c, 2);

            for(int d = 0; d<dir_cnt; d++) {
                double theta = 2*M_PI*d/dir_cnt + vp.rotation; 
                double x1 = p.x+r*sin(theta), y1 = p.y-r*cos(theta);
                
                if(directions[d] == 0)
                    continue;

                const double maxdirection = .29;
                bool split = directions[d] > maxdirection;
                double u = r + size*(split ? maxdirection : directions[d]);
                double x2 = p.x+u*sin(theta), y2 = p.y-u*cos(theta);

                if(split) {
                    wxPoint q((x1 + x2)/2, (y1 + y2)/2);
                    RenderNumber(q, 100*directions[d], c);

                    DrawLine(x1, y1, (3*x1+x2)/4, (3*y1+y2)/4, c, 2);
                    DrawLine((x1+3*x2)/4, (y1+3*y2)/4, x2, y2, c, 2);
                } else
                    DrawLine(x1, y1, x2, y2, c, 2);

                /* draw wind barbs */
                double cur_speed = speeds[d];
                double dir = 1;
                const double a = 10, b = M_PI*2/3;
                while(cur_speed > 2) {
                    DrawLine(x2, y2, x2-a*sin(theta+dir*b), y2+a*cos(theta+dir*b), c, 2);
                    dir = -dir;
                    if(dir > 0)
                        x2 -= 3*sin(theta), y2 += 3*cos(theta);
                    cur_speed -= 5;
                }
            }
        }
}

void ClimatologyOverlayFactory::RenderCycloneSegment(CycloneState &ss, PlugIn_ViewPort &vp,
                                                     int dayspan)
{
    if (!m_bCompletedLoading)
        return;
    if(ss.drawn_counter == m_cyclone_drawn_counter)
        return;

    ss.drawn_counter = m_cyclone_drawn_counter;
                    
    if(!m_dlg.m_cbAll->GetValue()) {
        int daydiff = abs(ss.datetime.day - m_CurrentTimeline.GetDay()
                          + 30.42*(ss.datetime.month - m_CurrentTimeline.GetMonth()));
        if(daydiff > 183)
            daydiff = 365 - daydiff;
        if(daydiff > dayspan/2)
            return;
    }

    double *lat = ss.lat, *lon = ss.lon;

    {
        wxPoint p[2];
        GetCanvasPixLL( &vp, &p[0], lat[0], lon[0] );
        GetCanvasPixLL( &vp, &p[1], lat[1], lon[1] );

        wxColour c = GetGraphicColor(CYCLONE_SETTING, ss.windknots);
                            
        DrawLine(p[0].x, p[0].y, p[1].x, p[1].y, c, 2);
                            
        /* direction arrow */
        wxPoint a((p[0].x+p[1].x)/2, (p[0].y+p[1].y)/2);
        wxPoint d(p[0].x-p[1].x, p[0].y-p[1].y);
                            
        DrawLine(a.x, a.y, a.x + (d.x+d.y)/5, a.y + (d.y-d.x)/5, c, 2);
        DrawLine(a.x, a.y, a.x + (d.x-d.y)/5, a.y + (d.x+d.y)/5, c, 2);
    }
}

void ClimatologyOverlayFactory::RenderCyclones(PlugIn_ViewPort &vp)
{
	// Skip cyclone rendering if no cyclone data is loaded
    if (m_cyclone_cache.empty())
        return;
	
    if (!m_bCompletedLoading)
        return;

    int dayspan = m_dlg.m_cfgdlg->m_sCycloneDaySpan->GetValue();

    int month_start, month_end;
    if(m_dlg.m_cbAll->GetValue()) {
        month_start = 0;
        month_end = 11;
    } else {
        wxDateTime dt2(m_CurrentTimeline.GetDay(), m_CurrentTimeline.GetMonth(), 1999);
        wxTimeSpan ts_dayspan = wxTimeSpan::Days(dayspan/2);
        month_start = (dt2 - ts_dayspan).GetMonth();
        month_end = (dt2 + ts_dayspan).GetMonth();
    }

    m_cyclone_drawn_counter++;

    wxDateTime start = wxDateTime::Now();
    for(int lati = floor(vp.lat_min); lati <= ceil(vp.lat_max); lati++)
        for(int loni = floor(vp.lon_min); loni <= ceil(vp.lon_max); loni++) {
            int monthi = month_start;
            for(;;) {
                int lonin = loni < 15 ? loni : loni - 360;
                int hash = (lonin * 180 + lati)*12 + monthi;

                std::list<CycloneState*> &states = m_cyclone_cache[hash];
                for(std::list<CycloneState*>::iterator it = states.begin();
                    it != states.end(); it++)
                    RenderCycloneSegment(**it, vp, dayspan);

                if(monthi == month_end)
                    break;
                if(++monthi == 12) monthi = 0;
            }
        }

    wxDateTime end = wxDateTime::Now();

    /* rendering is taking too long, and display lists not used */
    if(m_dc->GetDC() && (end - start).GetMilliseconds() >= 1200) {
        m_dlg.m_cbCyclones->SetValue(false);
        wxMessageDialog mdlg(&m_dlg, _("Computer too slow to render cyclones, disabling theater"),
                             _("Climatology"), wxOK | wxICON_WARNING);
        mdlg.ShowModal();
    }
}



// New RenderOverlay 
bool ClimatologyOverlayFactory::RenderOverlay(piDC &dc, PlugIn_ViewPort &vp)
{
    if (!m_bCompletedLoading)
        return true;   // nothing to draw, but not an error

    m_dc = &dc;

    // If we are in OpenGL mode (dc.GetDC() == NULL), blending is handled by the shader.
    // If we are in DC mode, we fall back to bitmap drawing.
    if (!dc.GetDC()) {
        glEnable(GL_BLEND);
    }

    wxFont font(12, wxFONTFAMILY_DEFAULT, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL);
    m_dc->SetFont(font);

    // Pass 1: overlays
    for (int overlay = 1; overlay >= 0; overlay--) {
        for (int i = 0; i < ClimatologyOverlaySettings::SETTINGS_COUNT; i++) {

            if (!m_dlg.SettingEnabled(i))
                continue;

            if (!m_Settings.Settings[i].m_bEnabled)
                continue;

            if (overlay)
                RenderOverlayMap(i, vp);
            else {
                RenderIsoBars(i, vp);
                RenderNumbers(i, vp);
                RenderDirectionArrows(i, vp);
            }
        }
    }

    if (m_dlg.m_cbWind->GetValue())
        RenderWindAtlas(vp);

    if (m_dlg.m_cbCyclones->GetValue())
        RenderCyclones(vp);

    return true;
}


void ClimatologyOverlayFactory::LoadCycloneDataBackground(
    std::function<void(const wxString&)> sendProgress)
{
    bool allcyclone = true;

    sendProgress("Loading cyclone basin: East Pacific…");
    if(!ReadCycloneData("cyclone-epa", m_epa))
        allcyclone = false;

    sendProgress("Loading cyclone basin: West Pacific…");
    if(!ReadCycloneData("cyclone-wpa", m_wpa))
        allcyclone = false;

    sendProgress("Loading cyclone basin: South Pacific…");
    if(!ReadCycloneData("cyclone-spa", m_spa, true))
        allcyclone = false;

    sendProgress("Loading cyclone basin: Atlantic…");
    if(!ReadCycloneData("cyclone-atl", m_atl))
        allcyclone = false;

    sendProgress("Loading cyclone basin: North Indian…");
    if(!ReadCycloneData("cyclone-nio", m_nio))
        allcyclone = false;

    sendProgress("Loading cyclone basin: South Indian…");
    if(!ReadCycloneData("cyclone-she", m_she, true))
        allcyclone = false;

    sendProgress("Loading El Niño / La Niña years…");
    if(!ReadElNinoYears("elnino_years.txt")) {
        m_dlg.m_cfgdlg->m_cbElNino->Disable();
        m_dlg.m_cfgdlg->m_cbLaNina->Disable();
        m_dlg.m_cfgdlg->m_cbNeutral->Disable();
    }

    sendProgress("Building cyclone cache…");
    BuildCycloneCache();

    // Final message before READY event
    sendProgress("Cyclone data ready.");
}

wxThread::ExitCode CycloneLoaderThread::Entry()
{
    wxLogMessage("Climatology: Background cyclone loading started");

    auto sendProgress = [&](const wxString& msg) {
        wxCommandEvent evt(wxEVT_COMMAND_MENU_SELECTED, ID_CYCLONE_PROGRESS);
        evt.SetString(msg);
        wxPostEvent(m_parent->m_pClimatologyDialog, evt);
    };

    m_parent->LoadCycloneDataBackground(sendProgress);

    m_parent->m_cycloneReady = true;

    sendProgress("Cyclone data ready.");

    wxCommandEvent evt(wxEVT_COMMAND_MENU_SELECTED, ID_CYCLONE_READY);
    wxPostEvent(m_parent->m_pClimatologyDialog, evt);

    wxLogMessage("Climatology: Background cyclone loading completed");
    return 0;
}
