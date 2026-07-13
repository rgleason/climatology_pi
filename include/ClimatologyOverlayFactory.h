#pragma once

#include <vector>
#include <map>
#include <functional>

#include <wx/wx.h>
#include <wx/event.h>

#include <wx/string.h>
#include <wx/datetime.h>
#include <wx/log.h>

#include <list>

#include <wx/progdlg.h>

#include "OverlayTypes.h"
#include "ClimatologyRenderParams.h"
#include "StandardDisplayParams.h"

#include "CycloneTypes.h"
#include "CycloneFilterParams.h"



typedef unsigned int GLuint;

class ClimatologyDataModel;
class PlugIn_ViewPort;
class piDC;
class ClimatologyIsoBarMap;
struct Cyclone;
struct CyclonePoint;
struct ElNinoYear;

// Backend interface for cyclone rendering
struct CycloneRenderBackend {
    virtual ~CycloneRenderBackend() {}
    virtual void SetColor(const wxColour& c, double opacity) = 0;
    virtual void DrawLine(double x1, double y1, double x2, double y2) = 0;
};

// Global ClimatologyOverlay struct
struct ClimatologyOverlay
{
    ClimatologyOverlay()
        : m_iTexture(0), m_data(nullptr),
          m_width(0), m_height(0),
          m_latoff(0), m_lonoff(0) {}

    ~ClimatologyOverlay();   // implemented in .cpp

    GLuint          m_iTexture;
    unsigned char*  m_data;
    int             m_width, m_height;
    double          m_latoff, m_lonoff;
};

// External GL shader + matrix utilities
extern GLuint pi_texture_2DA_shader_program;
typedef float mat4x4[16];
extern void mat4x4_identity(mat4x4 M);

// =====================================================
// Class ClimatologyOverlayFactory
// =====================================================
class ClimatologyOverlayFactory : public wxEvtHandler
{
public:
    ClimatologyOverlayFactory(wxWindow* parent, const wxString& dataDir);
    ClimatologyOverlayFactory(wxWindow* parent);
    ~ClimatologyOverlayFactory();

    bool Load();
    bool IsCompletedLoading() const { return m_bCompletedLoading; }

    void SetParams(const StandardDisplayParams& p);
    void SetCycloneFilter(const CycloneFilterParams& params);
    void SetViewPort(PlugIn_ViewPort* vp);

	double getCurValue(Coord coord,
                   int setting,
                   double lat,
                   double lon,
                   const ClimatologyRenderParams& p) const;
		
    // Unified render entry point
    bool Render(const ClimatologyRenderParams& p);

    // Cyclone Colors
    wxColour MapCyclonePointColor(const CyclonePoint& pt,
                                  const ClimatologyRenderParams& p) const;

    // Feature renderers
    void RenderOverlayMap(const ClimatologyRenderParams& p);
    void RenderDirectionArrows(const ClimatologyRenderParams& p);
    void RenderNumbers(const ClimatologyRenderParams& p);
    void RenderIsoBars(const ClimatologyRenderParams& p);
    void RenderWindAtlas(const ClimatologyRenderParams& p);
    void RenderCyclones(const ClimatologyRenderParams& p);

	void RenderCycloneSegment(const Cyclone& cyc,
                          const CyclonePoint& a,
                          const CyclonePoint& b,
                          const ClimatologyRenderParams& p,
                          const PlugIn_ViewPort& vp,
                          wxDC* dc);
						  
  // Background loading
    void LoadCycloneDataBackground(std::function<void(const wxString&)> sendProgress);

    // Cyclone ingestion
    bool ReadCycloneData(const wxString& filename,
                         std::vector<Cyclone>& basinTracks,
                         bool southernHemisphere);

	// ENSO ingestion
    bool LoadENSODataFromCSV(const wxString& filename);

	// Route-crossing analysis
    bool DoesCycloneTrackCrossRoute(const Cyclone& track,
                                    double lat1, double lon1,
                                    double lat2, double lon2,
                                    const ClimatologyRenderParams& p);

    int CountCycloneTrackCrossings(double lat1, double lon1,
                                   double lat2, double lon2,
                                   const ClimatologyRenderParams& p);
	
	// Filtering
	bool PassesCycloneFilters(const Cyclone& cyc,
                          const CyclonePoint& pt,
                          const ClimatologyRenderParams& p) const;
						  						  
private:
    wxWindow* m_parent_window = nullptr;
    PlugIn_ViewPort m_vp;
    wxString m_dataDir;

    std::unique_ptr<ClimatologyDataModel> m_dataModel;

	// Persistent user preferences
    StandardDisplayParams m_params;

	// Cyclone filter + data flags	
    CycloneFilterParams   m_cycloneParams;
    bool m_bCompletedLoading = false;
    bool m_shaders_loaded    = false;

    wxString m_sFailedMessage;
    std::list<wxString> m_FailedFiles;

    // Overlay textures: 13 months × NUM_OVERLAYS
    ClimatologyOverlay m_pOverlay[13][NUM_OVERLAYS];

    // Isobar maps per overlay/month
    ClimatologyIsoBarMap* m_pIsobars[NUM_OVERLAYS][13] = {};


    // Internal helpers
    bool LoadInternal(wxGenericProgressDialog* progress);

    // Direction arrows
    void RenderDirectionArrowsGL(const ClimatologyRenderParams& p);
    void RenderDirectionArrowsDC(const ClimatologyRenderParams& p);
	
	void AccumulateWindBins(const ClimatologyRenderParams& p,
                        double lat, double lon,
                        int dirBins,
                        std::vector<double>& freqBins,
                        std::vector<double>& speedBins,
                        double& calmFrac);

    // Numbers
    void RenderNumbersGL(const ClimatologyRenderParams& p);
    void RenderNumbersDC(const ClimatologyRenderParams& p);

    // Cyclones
    void RenderCyclonesGL(const ClimatologyRenderParams& p);
    void RenderCyclonesDC(const ClimatologyRenderParams& p);

    // Isobar helpers
    ClimatologyIsoBarMap* GetOrCreateIsoBarMap(int overlayType,
                                               int month,
                                               double spacing,
                                               double step,
                                               int units);
    void DestroyIsoBarMap(int overlayType, int month);

    // Unified model population
    void LoadVectorFieldUnified(int setting, int month,
                                const float* srcU, const float* srcV,
                                int latitudes, int longitudes,
                                double lat0, double lon0,
                                double latStep, double lonStep);

    void LoadScalarFieldUnified(int setting, int month,
                                const float* src,
                                int latitudes, int longitudes,
                                double lat0, double lon0,
                                double latStep, double lonStep);

    // Unified model accessors
    float* ReadNOAAFileUnified(const wxString& name,
                               int month,
                               int& rows,
                               int& cols,
                               double& lat0,
                               double& lon0,
                               double& latStep,
                               double& lonStep);

    bool HasDataFor(int setting, int month);
    double getValueMonth(enum Coord coord, int setting,
                         double lat, double lon, int month) const;

    // GL texture overlay pipeline
    bool BuildOverlayData(ClimatologyOverlay& O, int setting, int month);
    bool CreateGLTexture(ClimatologyOverlay& O);
    void DrawGLTexture(GLuint tex1, GLuint tex2,
                       double dpos,
                       PlugIn_ViewPort& vp,
                       double transparency);

    // Wind Atlas helpers
    void RenderWindAtlasGL(const ClimatologyRenderParams& p);
    void RenderWindAtlasDC(const ClimatologyRenderParams& p);

    void DrawWindAtlasLabelGLorDC(const wxPoint& center,
                                  double radius,
                                  int dirIndex,
                                  int dirCount,
                                  double freq,
                                  double rotation,
                                  const ClimatologyRenderParams& p);

    void DrawSegmentGLorDC(double x1, double y1,
                           double x2, double y2,
                           const wxColour& c,
                           const ClimatologyRenderParams& p);

    void DrawNOAABarbsGLorDC(double x2, double y2,
                             double theta,
                             double speed,
                             double scale,
                             const ClimatologyRenderParams& p);

    // Shared helpers
    void DrawArrowGL(double x1, double y1,
                     double x2, double y2,
                     const wxColour& c,
                     int width);

    void DrawArrowDC(wxDC* dc,
                     double x1, double y1,
                     double x2, double y2,
                     const wxColour& c,
                     int width);

    void DrawBarbsGL(double px, double py,
                     double x, double y,
                     double mag,
                     double cstep,
                     const wxColour& c,
                     int width);

    void DrawBarbsDC(wxDC* dc,
                     double px, double py,
                     double x, double y,
                     double mag,
                     double cstep,
                     const wxColour& c,
                     int width);

    bool AverageWindData();
    bool AverageCurrentData();

    // Cyclone helpers
    std::vector<const CyclonePoint*>
		
    GetCyclonePointsInSpan(const ClimatologyRenderParams& p,
                           int centerDay,
                           double lat,
                           double lon);
				  
	wxColour MapCyclonePointToColorENSO(const CyclonePoint& pt) const;

    // Color mapping
    void ColorMap(int setting, double v,
                  unsigned char& r,
                  unsigned char& g,
                  unsigned char& b);

    // Coordinate transforms
    void LatLonToPixel(const PlugIn_ViewPort& vp,
                       double lat,
                       double lon,
                       wxPoint& r) const;

    void LatLonToPixel(const PlugIn_ViewPort& vp,
                       double lat,
                       double lon,
                       double& x,
                       double& y) const;
					   
  	// ============================================================
    // CYCLONE STORAGE — unified model
    // ============================================================
    std::vector<Cyclone> m_epa;   // East Pacific
    std::vector<Cyclone> m_wpa;   // West Pacific
    std::vector<Cyclone> m_spa;   // South Pacific
    std::vector<Cyclone> m_atl;   // Atlantic
    std::vector<Cyclone> m_nio;   // North Indian
    std::vector<Cyclone> m_she;   // South Indian

    // Combined list (optional, if you use it)
    std::vector<Cyclone> m_cyclones;
	
    // ============================================================
    // ENSO DATA — year → month → ENSOPeriod
    // ============================================================
    std::map<int, std::map<int, ENSOPeriod>> m_ensoData;
    std::map<int, ElNinoYear> m_ElNinoYears;
	
    // ============================================================
    // STATE FLAGS
    // ============================================================
    bool m_hasCycloneData = false;
    bool m_hasENSOData    = false;

    // ============================================================
    // HELPERS — declared in .h, implemented in .cpp
    // ============================================================
    CycloneBasin BasinFromFilename(const wxString& filename);
    ENSOPeriod   ENSOFromDate(int year, int month);

    double NormalizeWind(double wind, CycloneBasin basin);
    double NormalizePressure(double pressure);

    int DayOfYear(int year, int month, int day);
    CycloneState StateFromCode(int code);
    CycloneENSO  PointENSOFromPeriod(ENSOPeriod p);
					   
};
