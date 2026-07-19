#pragma once

#include <vector>
#include <map>
#include <list>
#include <memory>

#include <wx/dc.h>
#include <wx/colour.h>
#include <wx/generic/progdlgg.h>   // wxGenericProgressDialog

#include "ClimatologyEnums.h"          // OverlayType, NUM_OVERLAYS
#include "ClimatologyCoord.h"          // Coord enum
#include "ClimatologyRenderParams.h"   // Render-time parameters
#include "WindData.h"                  // NOAA u/v grid
#include "CurrentData.h"               // NOAA u/v grid
#include "OverlayMapping.h"            // ColorMap, ClimatologyDataDirectory
#include "ClimatologyDataModel.h"      // ClimatologyDataModel
#include "ClimatologyMonthData.h"      // ClimatologyMonthData
#include "StandardDisplayParams.h"     // StandardDisplayParams
#include "CycloneFilterParams.h"       // CycloneFilterParams

class ClimatologyDataModel;
class PlugIn_ViewPort;

class ClimatologyIsoBarMap;
struct Cyclone;
struct CyclonePoint;
struct CycloneTrack;
struct ElNinoYear;


typedef unsigned int GLuint;

// ---------------------------------------------------------------------------
// ClimatologyOverlay
// ---------------------------------------------------------------------------
struct ClimatologyOverlay
{
    OverlayType type = OVERLAY_INVALID;   // added
    double latStep = 0.0;                 // added
    double lonStep = 0.0;                 // added	
	
    ClimatologyOverlay()
        : m_iTexture(0),
          m_data(nullptr),
          m_width(0),
          m_height(0),
          m_latoff(0),
          m_lonoff(0),
          m_valid(false),
          lon_min(0.0), lon_max(0.0),
          lat_min(0.0), lat_max(0.0),
          lon_step(0.0), lat_step(0.0),
          lon_count(0), lat_count(0),
          range_min(0.0), range_max(1.0),
          m_mean(0.0), m_stddev(0.0), m_range(1.0),
          weight(1.0),
          month_index(0)
    {}

    ~ClimatologyOverlay();

    GLuint         m_iTexture;
    unsigned char* m_data;
    int            m_width, m_height;
    double         m_latoff, m_lonoff;

    bool   m_valid;

    double lon_min, lon_max;
    double lat_min, lat_max;
    double lon_step, lat_step;
    int    lon_count, lat_count;

    double range_min, range_max;
    double m_mean, m_stddev, m_range;

    double weight;
    int    month_index;

    double value(int ix, int iy) const;
    void   setValue(int ix, int iy, double v);
};

// ---------------------------------------------------------------------------
// ClimatologyColorGrid
// ---------------------------------------------------------------------------
struct ClimatologyColorGrid
{
    int lon_count = 0;
    int lat_count = 0;
    std::vector<wxColour> data;

    void allocate(int nx, int ny)
    {
        lon_count = nx;
        lat_count = ny;
        data.assign(nx * ny, wxColour(0, 0, 0));
    }

    wxColour get(int ix, int iy) const
    {
        return data[iy * lon_count + ix];
    }

    void set(int ix, int iy, const wxColour& c)
    {
        data[iy * lon_count + ix] = c;
    }
};

// ---------------------------------------------------------------------------
// ClimatologyOverlayFactory
// ---------------------------------------------------------------------------
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

    double getCurValue(enum Coord coord,
                       int setting,
                       double lat,
                       double lon,
                       const ClimatologyRenderParams& p) const;

    bool Render(const ClimatologyRenderParams& p);

    // Overlay access
	ClimatologyOverlay* GetOverlay(OverlayType type,
                               const ClimatologyRenderParams& p);

	const ClimatologyOverlay* GetOverlay(OverlayType type,
                                     const ClimatologyRenderParams& p) const;

	    // Unified overlay selection / metadata / composite
    std::vector<const ClimatologyOverlay*>
    GatherOverlaysForMode(DisplayMode mode,
                          int overlayType,
                          int month_index);

    std::vector<const ClimatologyOverlay*>
    GatherOverlaysForMode(DisplayMode mode,
                          OverlayType type,
                          int month_index);

    void GatherOverlayMetadata(const std::vector<const ClimatologyOverlay*>& overlays,
                               DisplayMode mode,
                               std::vector<double>& weights,
                               std::vector<double>& vmins,
                               std::vector<double>& vmaxs,
                               double& global_vmin,
                               double& global_vmax);

    void GenerateUnifiedColorGrid(ClimatologyColorGrid& outGrid,
                                  const std::vector<const ClimatologyOverlay*>& overlays,
                                  DisplayMode mode,
                                  const std::vector<double>& weights,
                                  const std::vector<double>& vmins,
                                  const std::vector<double>& vmaxs,
                                  double global_vmin,
                                  double global_vmax);

    // High‑level unified operators
    void GenerateUnifiedColorGridFromSingleOverlayScaled(
            const ClimatologyOverlay& O,
            double vmin, double vmax,
            ClimatologyColorGrid& outGrid,
            double global_vmin,
            double global_vmax);

    void GenerateUnifiedColorGridMultiOverlayAdditive(
            const std::vector<const ClimatologyOverlay*>& overlays,
            ClimatologyColorGrid& outGrid,
            double global_vmin,
            double global_vmax);

    void GenerateUnifiedColorGridMultiOverlayMax(
            const std::vector<const ClimatologyOverlay*>& overlays,
            ClimatologyColorGrid& outGrid,
            double global_vmin,
            double global_vmax);

    void GenerateUnifiedColorGridMultiOverlayMin(
            const std::vector<const ClimatologyOverlay*>& overlays,
            ClimatologyColorGrid& outGrid,
            double global_vmin,
            double global_vmax);

    void GenerateUnifiedColorGridMultiOverlayMean(
            const std::vector<const ClimatologyOverlay*>& overlays,
            ClimatologyColorGrid& outGrid,
            double global_vmin,
            double global_vmax);

    void GenerateUnifiedColorGridMultiOverlayMedian(
            const std::vector<const ClimatologyOverlay*>& overlays,
            ClimatologyColorGrid& outGrid,
            double global_vmin,
            double global_vmax);

    // ENSO
    bool LoadENSODataFromCSV(const wxString& filename);

private:
    // Core state
    wxWindow* m_parent_window = nullptr;
    wxString  m_dataDir;
    PlugIn_ViewPort m_vp;

    std::unique_ptr<ClimatologyDataModel> m_dataModel;

    StandardDisplayParams m_params;
    StandardDisplayParams m_displayParams;
    CycloneFilterParams   m_cycloneParams;

    bool m_bCompletedLoading = false;
    bool m_shaders_loaded    = false;
    bool m_hasCycloneData    = false;
    bool m_hasENSOData       = false;

    // Unified NOAA grid storage
    ClimatologyOverlay m_pOverlay[13][NUM_OVERLAYS];

    // Raw NOAA fields
    float* m_slp[13]     = {};
    float* m_sst[13]     = {};
    float* m_at[13]      = {};
    float* m_cld[13]     = {};
    float* m_precip[13]  = {};
    float* m_rhum[13]    = {};
    float* m_lightn[13]  = {};
    float* m_seadepth    = nullptr;

    WindData*    m_WindData[13]    = {};
    CurrentData* m_CurrentData[13] = {};

    ClimatologyMonthData m_data[NUM_OVERLAYS][13];

    wxString              m_sFailedMessage;
    std::list<wxString>   m_FailedFiles;

    // IsoBars
    ClimatologyIsoBarMap* m_pIsobars[NUM_OVERLAYS][13] = {};

    // Overlay registry
	std::map<OverlayType, std::vector<ClimatologyOverlay*>> m_overlay_map;

    // Color palette
    std::vector<wxColour> m_colorPalette;

    // Cyclone / ENSO data
    std::vector<Cyclone>              m_cyclones;
    std::vector<CycloneTrack>         m_cyclone_cache;
    std::vector<ElNinoYear>           m_enso;
    std::map<int, std::vector<double>> m_ENSOIndex;
    std::map<int, std::vector<Cyclone>> m_Cyclones;
    std::map<int, CycloneTrack>        m_CycloneTracks;

    // Storm symbols
    wxBitmap m_bmpStormCat1;
    wxBitmap m_bmpStormCat2;
    wxBitmap m_bmpStormCat3;
    wxBitmap m_bmpStormCat4;
    wxBitmap m_bmpStormCat5;
    wxBitmap m_bmpStormTD;

    // Loading / data helpers
    bool LoadInternal(wxGenericProgressDialog* progress);
    void ApplyCycloneFilter();
	
	void LoadVectorField(int setting,
                     int month,
                     const float* u,
                     const float* v,
                     int latCount,
                     int lonCount,
                     double lat0,
                     double lon0,
                     double latStep,
                     double lonStep);

    void LoadScalarField(int setting,
                         int month,
                         float* src,
                         int latCount,
                         int lonCount,
                         double lat0,
                         double lon0,
                         double latStep,
                         double lonStep);
	bool AverageWindData();
	bool AverageCurrentData();

    bool LoadScalarNOAA();
    bool LoadWindNOAA();
    bool LoadCurrentNOAA();
    bool ReadNOAAFile(const wxString& name,
                      int month,
                      float* dst,
                      int latCount,
                      int lonCount);

    bool HasDataFor(int setting, int month);
	
	double getCurValue(Coord coord,
                   int setting,
                   double lat,
                   double lon,
                   const ClimatologyRenderParams& p);


    double getValueMonth(enum Coord coord,
                         int setting,
                         double lat,
                         double lon,
                         int month);

    bool BuildOverlayData(ClimatologyOverlay& O,
                          int setting,
                          int month);

    bool CreateGLTexture(ClimatologyOverlay& O);

    void DrawGLTexture(GLuint tex1, GLuint tex2,
                       double dpos,
                       PlugIn_ViewPort& vp,
                       double transparency);
					   
    // Rendering
    void RenderOverlayMap(const ClimatologyRenderParams& p);
    void RenderUnifiedGrid(const ClimatologyRenderParams& p);

    void RenderGridGL(const ClimatologyColorGrid& grid,
                      const ClimatologyRenderParams& p);

    void RenderGridDC(const ClimatologyColorGrid& grid,
                      wxDC& dc,
                      const ClimatologyRenderParams& p);

    void RenderDirectionArrows(const ClimatologyRenderParams& p);
    void RenderDirectionArrowsGL(const ClimatologyRenderParams& p);
    void RenderDirectionArrowsDC(const ClimatologyRenderParams& p);

void DrawArrowGL(double x1, double y1,
                 double x2, double y2,
                 const wxColour& color,
                 int width);

void DrawBarbsGL(double px, double py,
                double x, double y,
                double mag,
                double cstep,
                const wxColour& c,
                int width);

void DrawArrowDC(wxDC* dc,
                 double x1, double y1,
                 double x2, double y2,
                 const wxColour& color,
                 int width);

void DrawBarbsDC(wxDC* dc,
                 double px, double py,
                 double x, double y,
                 double mag,
                 double cstep,
                 const wxColour& color,
                 int width);


    void RenderNumbers(const ClimatologyRenderParams& p);
    void RenderNumbersDC(const ClimatologyRenderParams& p);
    void RenderNumbersGL(const ClimatologyRenderParams& p);

    void RenderIsoBars(const ClimatologyRenderParams& p);

    ClimatologyIsoBarMap* GetOrCreateIsoBarMap(int overlayType,
                                               int month,
                                               double spacing,
                                               double step,
                                               int units,
                                               const ClimatologyRenderParams& p);

    void DestroyIsoBarMap(int overlayType, int month);

    void RenderWindAtlas(const ClimatologyRenderParams& p);
    void RenderCyclones(const ClimatologyRenderParams& p);

    // Cyclone helpers
    wxColour BasinColor(int basinId);

    wxColour CycloneColor(const Cyclone& C,
                          const CycloneParams& A) const;

    wxColour CyclonePointColor(const CyclonePoint& pt,
                               const CycloneParams& A) const;

    wxColour CycloneSegmentColor(const CyclonePoint& a,
                                 const CyclonePoint& b,
                                 const CycloneParams& A) const;

    wxColour CycloneGradientColor(const Cyclone& C,
                                  double t,
                                  const CycloneParams& A) const;

    float CycloneTrackWidth(const CyclonePoint& pt,
                            const CycloneParams& A) const;

    void CycloneLegend(wxDC* dc,
                       const CycloneParams& A,
                       const wxPoint& origin);

    bool CycloneTimelineInterpolation(const CyclonePoint& pt,
                                      const CycloneFilterParams& F,
                                      int currentMonth) const;

    bool CycloneVisible(const CycloneTrack& track,
                        const ClimatologyRenderParams& p) const;

    wxColour CycloneColor(const CycloneTrack& track,
                          const ClimatologyRenderParams& p) const;

    // ENSO / storm accessors
    double GetENSOIndex(int year, int month);

    bool GetCycloneData(int year,
                        int month,
                        std::vector<CyclonePoint>& out) const;

    bool GetCycloneTrack(int id,
                         CycloneTrack& out) const;

    bool GetCyclonePoint(int id,
                         CyclonePoint& out) const;

    bool GetStormCursorData(double lat,
                            double lon,
                            CyclonePoint& out) const;

    bool GetStormHistory(int id,
                         std::vector<CyclonePoint>& out) const;

    bool GetStormHistoryPoint(int id,
                              CyclonePoint& out) const;

    int      GetStormCategory(double wind_kt);
    wxColour GetStormColor(int category);
    wxBitmap GetStormSymbol(int category);
    wxString GetStormLabel(int category);
    wxString GetStormTooltip(const CyclonePoint& p);
    wxString GetStormIntensityText(double wind_kt);

    // Overlay value helpers
	double GetOverlayValue(OverlayType type,
                       const ClimatologyRenderParams& p,
                       double lat,
                       double lon);

    double GetOverlayValueBilinear(const ClimatologyOverlay& O,
                                   double lat,
                                   double lon);

    double GetOverlayGradient(const ClimatologyOverlay& O,
                              double lat,
                              double lon);

    bool GetOverlayMinMax(const ClimatologyOverlay& O,
                          double& vmin,
                          double& vmax);

    bool NormalizeOverlay(ClimatologyOverlay& O);
    void NormalizeAllOverlays();

    bool ComputeOverlayStatistics(const ClimatologyOverlay& O,
                                  double& mean,
                                  double& stddev);

    void ComputeAllOverlayStatistics();

    bool ComputeOverlayRange(const ClimatologyOverlay& O,
                             double& range);

    void ComputeAllOverlayRanges();

    wxColour GetOverlayColor(double v);
    wxColour GetOverlayColorScaled(double value,
                                   double vmin,
                                   double vmax);

    void ApplyColorMap(const ClimatologyOverlay& O,
                       ClimatologyColorGrid& grid);

    void ApplyColorMapScaled(const ClimatologyOverlay& O,
                             ClimatologyColorGrid& grid,
                             double vmin,
                             double vmax);

    void GenerateColorGrid(const ClimatologyOverlay& O,
                           ClimatologyColorGrid& grid);

    void GenerateColorGridScaled(const ClimatologyOverlay& O,
                                 ClimatologyColorGrid& grid,
                                 double vmin,
                                 double vmax);

    // Unified grid variants (full set)
	
    void GenerateUnifiedColorGrid(const std::vector<const ClimatologyOverlay*>& overlays,
                                  ClimatologyColorGrid& outGrid);

    void GenerateUnifiedColorGridScaled(
            const std::vector<const ClimatologyOverlay*>& overlays,
            ClimatologyColorGrid& outGrid,
            double vmin,
            double vmax);

    void GenerateUnifiedColorGridWeighted(
            const std::vector<const ClimatologyOverlay*>& overlays,
            const std::vector<double>& weights,
            ClimatologyColorGrid& outGrid);

    void GenerateUnifiedColorGridWeightedScaled(
            const std::vector<const ClimatologyOverlay*>& overlays,
            const std::vector<double>& weights,
            ClimatologyColorGrid& outGrid,
            double vmin,
            double vmax);

    void GenerateUnifiedColorGridMultiRange(
            const std::vector<const ClimatologyOverlay*>& overlays,
            const std::vector<double>& vmins,
            const std::vector<double>& vmaxs,
            ClimatologyColorGrid& outGrid);

    void GenerateUnifiedColorGridMultiRangeScaled(
            const std::vector<const ClimatologyOverlay*>& overlays,
            const std::vector<double>& vmins,
            const std::vector<double>& vmaxs,
            ClimatologyColorGrid& outGrid,
            double global_vmin,
            double global_vmax);

    void GenerateUnifiedColorGridMultiRangeWeighted(
            const std::vector<const ClimatologyOverlay*>& overlays,
            const std::vector<double>& weights,
            const std::vector<double>& vmins,
            const std::vector<double>& vmaxs,
            ClimatologyColorGrid& outGrid);

    void GenerateUnifiedColorGridMultiRangeWeightedScaled(
            const std::vector<const ClimatologyOverlay*>& overlays,
            const std::vector<double>& weights,
            const std::vector<double>& vmins,
            const std::vector<double>& vmaxs,
            ClimatologyColorGrid& outGrid,
            double global_vmin,
            double global_vmax);

    void GenerateUnifiedColorGridMultiRangeWeightedGlobal(
            const std::vector<const ClimatologyOverlay*>& overlays,
            const std::vector<double>& weights,
            const std::vector<double>& vmins,
            const std::vector<double>& vmaxs,
            ClimatologyColorGrid& outGrid);

    void GenerateUnifiedColorGridMultiRangeWeightedGlobalScaled(
            const std::vector<const ClimatologyOverlay*>& overlays,
            const std::vector<double>& weights,
            const std::vector<double>& vmins,
            const std::vector<double>& vmaxs,
            ClimatologyColorGrid& outGrid,
            double global_vmin,
            double global_vmax);

    void ApplyGlobalScalingToUnifiedGrid(ClimatologyColorGrid& grid,
                                         double global_vmin,
                                         double global_vmax);
};
