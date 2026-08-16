#pragma once

#pragma message("Header: " __FILE__)

#include <wx/wxprec.h>
#ifndef WX_PRECOMP
#include <wx/wx.h>
#endif

//=== System & STL ============================================================
#include <vector>
#include <map>
#include <list>
#include <memory>

//=== wxWidgets Forward Declarations ==========================================
class wxDC;
class wxColour;
class wxGenericProgressDialog;
class wxWindow;
class wxBitmap;

//=== OpenCPN Forward Declarations ============================================
class PlugIn_ViewPort;

//=== Climatology / Plugin Includes ===========================================
#include "ClimatologyEnums.h"          // OverlayType, NUM_OVERLAYS
#include "ClimatologyRenderParams.h"   // Render-time parameters
#include "WindData.h"                  // NOAA u/v grid
#include "CurrentData.h"               // NOAA u/v grid
#include "OverlayMapping.h"            // ColorMap, ClimatologyDataDirectory
#include "ClimatologyDataModel.h"      // ClimatologyDataModel
#include "ClimatologyMonthData.h"      // ClimatologyMonthData
#include "StandardDisplayParams.h"     // StandardDisplayParams
#include "CycloneFilterParams.h"       // CycloneFilterParams
#include "CycloneStructs.h"            // CyclonePoint, CycloneTrack, CycloneData
#include "RenderGrid.h"
#include "UnifiedGrid.h"


//=== External Helpers =========================================================
wxString ClimatologyDataDirectory();

//=== GL Typedef (GL kept OUT of header) ======================================
typedef unsigned int GLuint;

//=== Forward Declarations =====================================================
class ClimatologyIsoBarMap;

//=============================================================================
//  ClimatologyOverlay 
//=============================================================================
struct ClimatologyOverlay
{
    OverlayType type = OVERLAY_INVALID;
    double latStep = 0.0;
    double lonStep = 0.0;

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

//=============================================================================
//   ClimatologyColorGrid 
//=============================================================================
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

//=============================================================================
//   ClimatologyOverlayFactory 
//=============================================================================
class ClimatologyOverlayFactory : public wxEvtHandler
{
public:
    //=== Construction & Lifetime ============================================
    ClimatologyOverlayFactory(wxWindow* parent, const wxString& dataDir);
    ClimatologyOverlayFactory(wxWindow* parent);
    ~ClimatologyOverlayFactory();

    //=== Loading & State =====================================================
    bool Load();
    bool IsCompletedLoading() const { return m_bCompletedLoading; }

    //=== Configuration & Viewport ===========================================
    void SetParams(const StandardDisplayParams& p);
    void SetCycloneFilter(const CycloneFilterParams& params);
	
    void SetViewPort(PlugIn_ViewPort* vp);

	StandardDisplayParams& GetDisplayParams() { return m_displayParams; }
	CycloneParams&        GetCycloneParams()  { return m_cycloneParams; }


    // Build render parameters for a single overlay
	void BuildRenderParams(int overlayIndex,
						   PlugIn_ViewPort* vp,
						   piDC* dc,
						   ClimatologyRenderParams& outParams) const;

    //=== Value Queries =======================================================
    double getCurValue(enum Coord coord,
                       int setting,
                       double lat,
                       double lon,
                       const ClimatologyRenderParams& p) const;

    double getValueMonth(enum Coord coord,
                         int setting,
                         double lat,
                         double lon,
                         int month) const;

    //=== Top-Level Rendering Entry ==========================================
    bool Render(const ClimatologyRenderParams& p);

    //=== Overlay Access ======================================================
    ClimatologyOverlay* GetOverlay(OverlayType type,
                                   const ClimatologyRenderParams& p);

    const ClimatologyOverlay* GetOverlay(OverlayType type,
                                         const ClimatologyRenderParams& p) const;

    //=== Unified Overlay Selection / Metadata ================================
    std::vector<const ClimatologyOverlay*>
    GatherOverlaysForMode(DisplayMode mode,
                          OverlayType type,
                          int month_index,
                          const ClimatologyRenderParams& p) const;

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

    //=== Coordinate Helpers ==================================================
    void LatLonToPixel(const PlugIn_ViewPort& vp,
                       double lat,
                       double lon,
                       wxPoint& r) const;

    void LatLonToPixel(const PlugIn_ViewPort& vp,
                       double lat,
                       double lon,
                       double& x,
                       double& y) const;

    void GetCanvasPixLL(const PlugIn_ViewPort* vp,
                        wxPoint* r,
                        double lat,
                        double lon) const;

    void GetCanvasLLPix(const PlugIn_ViewPort* vp,
                        const wxPoint& p,
                        double* lat,
                        double* lon) const;

    //=== Color Helpers =======================================================
    wxColour GetOverlayColorLegacy(int setting, double value);

    wxColour GetOverlayColorUnified(int setting,
                                    double value,
                                    double vmin,
                                    double vmax,
                                    const ClimatologyRenderParams& params);

    wxColour GetOverlayColor(double v);
    wxColour GetOverlayColorScaled(double value, double vmin, double vmax);

    bool BuildOverlayData(ClimatologyOverlay& O,
                          int setting,
                          int month,
                          const ClimatologyRenderParams& params);

    //=== Cyclone History Access =============================================
    bool GetStormHistory(int stormID, std::vector<CyclonePoint>& out);
    bool GetStormHistoryPoint(int stormID, int index, CyclonePoint& out);

private:
    //=========================================================================
    // Core State 
    //=========================================================================
    wxWindow*       m_parent_window = nullptr;
    wxString        m_dataDir;
    PlugIn_ViewPort m_vp;

    std::unique_ptr<ClimatologyDataModel> m_dataModel;
	// Modern unified database (month-major, multi-dataset)
	UnifiedGrid m_unifiedGrid;


    StandardDisplayParams m_params;
    StandardDisplayParams m_displayParams;

    CycloneParams       m_cycloneParams;
    CycloneFilterParams m_cycloneFilter;

    bool m_bCompletedLoading = false;
    bool m_shaders_loaded    = false;
    bool m_hasCycloneData    = false;
    bool m_hasENSOData       = false;

    // Unified NOAA grid storage
    ClimatologyOverlay m_pOverlay[13][NUM_OVERLAYS];

    // Raw NOAA fields
    std::vector<float> m_slp[12];
    std::vector<float> m_sst[12];
    std::vector<float> m_at[12];
    std::vector<float> m_cld[12];
    std::vector<float> m_precip[12];
    std::vector<float> m_rhum[12];
    std::vector<float> m_lightn[12];
    std::vector<float> m_seadepth;

    WindData*    m_WindData[13]    = {};
    CurrentData* m_CurrentData[13] = {};

    ClimatologyMonthData m_data[NUM_OVERLAYS][13];

    wxString            m_sFailedMessage;
    std::list<wxString> m_FailedFiles;

    // IsoBars
    ClimatologyIsoBarMap* m_pIsobars[NUM_OVERLAYS][13] = {};

    // Cyclone / ENSO data
    CycloneData                 m_cycloneData;
    std::vector<CyclonePoint>   m_rawCyclonePoints;
    std::vector<CycloneTrack>   m_cyclone_cache;

    // Color palette
    std::vector<wxColour> m_colorPalette;

    // Overlay registry
    std::map<OverlayType, std::vector<ClimatologyOverlay*>> m_overlay_map;

    // ENSO lookup table: year → month → ENSO phase
    std::map<int, std::map<int, CycloneENSO>> m_ensoData;

    // Storm symbols
    wxBitmap m_bmpStormCat1;
    wxBitmap m_bmpStormCat2;
    wxBitmap m_bmpStormCat3;
    wxBitmap m_bmpStormCat4;
    wxBitmap m_bmpStormCat5;
    wxBitmap m_bmpStormTD;

    //=========================================================================
    //    Loading / Data Helpers 
    //=========================================================================
    bool LoadInternal(wxGenericProgressDialog* progress);

    void LoadVectorField(int setting,
                         int month,
                         const float* srcU,
                         const float* srcV,
                         int latitudes,
                         int longitudes,
                         double lat0,
                         double lon0,
                         double latStep,
                         double lonStep);

    void LoadScalarField(int setting,
                         int month,
                         const float* src,
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

    bool HasDataFor(int setting, int month) const;

    bool CreateGLTexture(ClimatologyOverlay& O);

    void DrawGLTexture(GLuint tex1, GLuint tex2,
                       double dpos,
                       PlugIn_ViewPort& vp,
                       double transparency);

    //=========================================================================
    //   Rendering 
    //=========================================================================
    void RenderOverlayMap(const ClimatologyRenderParams& p);
    void RenderUnifiedGrid(const ClimatologyRenderParams& p);
	
	

// modern overloads exist but are NOT used yet
	void RenderGridGL(const RenderGrid& grid,
                  const ClimatologyRenderParams& p);

// legacy renderer still expects ClimatologyColorGrid
    void RenderGridGL(const ClimatologyColorGrid& grid,
                      const ClimatologyRenderParams& p);
					  
					  
					  
// modern overloads exist but are NOT used yet
	void RenderGridDC(const RenderGrid& grid,
                  wxDC& dc,
                  const ClimatologyRenderParams& p);

// legacy renderer still expects ClimatologyColorGrid
    void RenderGridDC(const ClimatologyColorGrid& grid,
                      wxDC& dc,
                      const ClimatologyRenderParams& p);

    void RenderDirectionArrows(const ClimatologyRenderParams& p);
    void RenderDirectionArrowsGL(const ClimatologyRenderParams& p);
    void RenderDirectionArrowsDC(const ClimatologyRenderParams& p);

    //=========================================================================
    //   Draw & Render: Arrows, Barbs, Numbers, WindAtlas 
    //=========================================================================
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
    void RenderWindAtlas(const ClimatologyRenderParams& p);

    //=========================================================================
    //   Isobar Rendering  
    //=========================================================================
    void RenderIsoBars(const ClimatologyRenderParams& p);

    ClimatologyIsoBarMap* GetOrCreateIsoBarMap(int overlayType,
                                               int month,
                                               double spacing,
                                               double step,
                                               int units,
                                               const ClimatologyRenderParams& p);

    void DestroyIsoBarMap(int overlayType, int month);

    //=========================================================================
    //   Cyclone Loader Pipeline  
    //=========================================================================
    bool LoadCyclonePoints();  // loads raw points into m_rawCyclonePoints
    bool LoadENSOYears(const wxString& filename);

    bool ReadCycloneData(const wxString& filename,
                         std::vector<Cyclone>& basinTracks,
                         bool southernHemisphere);

    bool BuildCycloneTracks();  // assembles raw points into CycloneTrack objects

    void RenderCyclones(const ClimatologyRenderParams& p);
    void ApplyCycloneFilter(const CycloneFilterParams& F);

    //=========================================================================
    //   Cyclone Accessors  
    //=========================================================================
    bool GetCycloneData(int year,
                        int month,
                        std::vector<CyclonePoint>& out) const;

    bool GetCycloneTrack(int id,
                         CycloneTrack& out) const;

    bool GetCyclonePoint(int id,
                         int index,
                         CyclonePoint& out) const;

    //=========================================================================
    //   Cyclone Rendering Helpers  
    //=========================================================================
    wxColour BasinColor(CycloneBasin basin) const;

    wxColour CycloneColor(const CycloneTrack& track,
                          const ClimatologyRenderParams& p) const;

    wxColour CyclonePointColor(const CyclonePoint& pt,
                               const CycloneParams& A) const;

    wxColour CycloneSegmentColor(const CyclonePoint& a,
                                 const CyclonePoint& b,
                                 const CycloneParams& A) const;

    wxColour CycloneGradientColorTrack(const CycloneTrack& track,
                                       double t,
                                       const CycloneParams& A) const;

    float CycloneTrackWidth(const CyclonePoint& pt,
                            const CycloneParams& A) const;

    bool CycloneVisible(const CycloneTrack& track,
                        const ClimatologyRenderParams& p) const;

    void CycloneLegend(wxDC& dc,
                       const CycloneParams& A,
                       const wxPoint& origin) const;

    bool CycloneTimelineInterpolation(const CyclonePoint& pt,
                                      const CycloneFilterParams& F,
                                      int currentMonth) const;

    //=========================================================================
    //   ENSO Helpers  
    //=========================================================================
    CycloneENSO ENSOFromDate(int year, int month) const;

    double GetENSOIndex(int year, int month);

    int      GetStormCategory(double wind_kt);
    wxColour GetStormColor(int category);
    wxBitmap GetStormSymbol(int category);
    wxString GetStormLabel(int category);
    wxString GetStormIntensityText(double wind_kt);

    bool GetStormCursorData(double lat,
                            double lon,
                            CycloneTrack& outTrack,
                            CyclonePoint& outPoint) const;

    wxString GetStormTooltip(const CycloneTrack& track,
                             const CyclonePoint& pt) const;

    //=========================================================================
    //    Overlay Value Helpers  
    //=========================================================================
    double GetOverlayGradient(const ClimatologyOverlay& O,
                              double lat,
                              double lon);

    bool GetOverlayMinMax(const ClimatologyOverlay& O,
                          double& vmin,
                          double& vmax);

    bool NormalizeOverlay(ClimatologyOverlay& O,
                          double vmin,
                          double vmax);

    void NormalizeAllOverlays();

    int    DayOfYear(int year, int month, int day) const;
    double NormalizeWind(double wind, CycloneBasin basin) const;
    double NormalizePressure(double pressure) const;

    bool ComputeOverlayStatistics(const ClimatologyOverlay& O,
                                  double& mean,
                                  double& stddev);

    void ComputeAllOverlayStatistics();

    bool ComputeOverlayRange(const ClimatologyOverlay& O,
                             double& range);

    void ComputeAllOverlayRanges();

    //=========================================================================
    //   Overlay Color Helpers  
    //=========================================================================
    void ColorMapLegacy(int setting,
                        double v,
                        unsigned char& r,
                        unsigned char& g,
                        unsigned char& b);

    void ApplyColorMap(const ClimatologyOverlay& O,
                       ClimatologyColorGrid& grid);

    void ApplyColorMapScaled(const ClimatologyOverlay& O,
                             ClimatologyColorGrid& grid,
                             double vmin,
                             double vmax);

    void ApplyGlobalScalingToUnifiedGrid(
        const std::vector<double>& blended_cache,
        int NX,
        int NY,
        ClimatologyColorGrid& outGrid,
        double global_vmin,
        double global_vmax);

    //=========================================================================
    //   Unified Grid Variants  
    //=========================================================================
    void GenerateColorGrid(const ClimatologyOverlay& O,
                           ClimatologyColorGrid& grid);

    void GenerateColorGridScaled(const ClimatologyOverlay& O,
                                 ClimatologyColorGrid& grid,
                                 double vmin,
                                 double vmax);

    void GenerateUnifiedColorGrid(
        const std::vector<const ClimatologyOverlay*>& overlays,
        ClimatologyColorGrid& outGrid);

    void GenerateUnifiedColorGridScaled(
        const std::vector<const ClimatologyOverlay*>& overlays,
        const std::vector<double>& vmins,
        const std::vector<double>& vmaxs,
        ClimatologyColorGrid& outGrid,
        double global_vmin,
        double global_vmax);

    void GenerateUnifiedColorGridWeighted(
        const std::vector<const ClimatologyOverlay*>& overlays,
        const std::vector<double>& weights,
        ClimatologyColorGrid& outGrid);

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
};

#pragma message("END HEADER: " __FILE__)
