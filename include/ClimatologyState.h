#ifndef CLIMATOLOGY_STATE_H
#define CLIMATOLOGY_STATE_H

#include <array>
#include <cstddef>
#include <cstdint>

namespace climatology {

static const std::size_t kDisplayFieldCount = 10;

struct FieldRenderState {
    bool visible = false;
    bool enabled = false;
    int units = 0;
    bool overlay_map = false;
    int overlay_transparency = 0;
    bool overlay_interpolation = true;
    bool isobars = false;
    int isobar_spacing = 0;
    int isobar_step = 0;
    bool numbers = false;
    double numbers_spacing = 0.0;
    bool direction_arrows = false;
    int arrow_length_type = 0;
    int arrow_width = 0;
    int arrow_red = 0;
    int arrow_green = 0;
    int arrow_blue = 0;
    int arrow_alpha = 255;
    int arrow_size = 0;
    int arrow_spacing = 0;
};

struct WindAtlasRenderState {
    bool enabled = false;
    double size = 0.0;
    double spacing = 0.0;
    int opacity = 255;
};

struct CycloneFilterState {
    bool tropical = true;
    bool subtropical = true;
    bool extratropical = true;
    bool remnant = true;
    int minimum_wind_knots = 0;
    int maximum_pressure_hpa = 2000;
    std::int64_t start_utc = 0;
    std::int64_t end_utc = 0;
    bool include_enso_unavailable = true;
    bool include_el_nino = true;
    bool include_la_nina = true;
    bool include_neutral = true;
    int day_span = 365;
};

struct ClimatologyRenderState {
    std::array<FieldRenderState, kDisplayFieldCount> fields;
    WindAtlasRenderState wind_atlas;
    CycloneFilterState cyclone_filter;
    bool all_times = false;
    bool show_wind_atlas = false;
    bool show_cyclones = false;
};

struct ValueCalibration {
    double factor = 1.0;
    double offset = 0.0;

    double Apply(double value) const { return factor * (value + offset); }
};

// Pure equivalent of the historical display-unit conversion.  Keeping this
// outside the dialogs lets query and render preparation use a captured value
// without consulting mutable wx controls from a worker thread.
inline ValueCalibration CalibrationForField(int field, int units)
{
    ValueCalibration calibration;
    switch(field) {
    case 0: // wind
    case 1: // current
        switch(units) {
        case 1: calibration.factor = 1.852 / 3.6; break;
        case 2: calibration.factor = 1.15; break;
        case 3: calibration.factor = 1.85; break;
        default: break;
        }
        break;
    case 2: // pressure
        if(units == 1) calibration.factor = 1.0 / 1.33;
        break;
    case 3: // sea temperature
    case 4: // air temperature
        if(units == 1) {
            calibration.factor = 9.0 / 5.0;
            calibration.offset = 32.0 * 5.0 / 9.0;
        }
        break;
    case 6: // precipitation, native metres/year
        if(units == 0 || units == 1) calibration.factor /= 365.24;
        else if(units == 2 || units == 4) calibration.factor /= 12.0;
        if(units == 0 || units == 2) calibration.factor *= 1000.0;
        else if(units == 1 || units == 4 || units == 7)
            calibration.factor *= 39.37;
        else if(units == 5 || units == 8)
            calibration.factor *= 3.28;
        break;
    case 9: // sea depth
        if(units == 1) calibration.factor = 3.28;
        break;
    default: // percentages/lightning and unknown values are identity
        break;
    }
    return calibration;
}

}  // namespace climatology

#endif
