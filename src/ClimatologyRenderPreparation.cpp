#include "ClimatologyRenderPreparation.h"

#include "ClimatologyMath.h"
#include "ClimatologyQueryEngine.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace climatology {

namespace {

std::int64_t CivilUnixSeconds(const CivilTime &time)
{
    int year = time.year;
    const unsigned month = static_cast<unsigned>(time.month + 1);
    const unsigned day = static_cast<unsigned>(time.day);
    year -= month <= 2;
    const int era = (year >= 0 ? year : year - 399) / 400;
    const unsigned year_of_era = static_cast<unsigned>(year - era * 400);
    const unsigned shifted_month = month > 2 ? month - 3 : month + 9;
    const unsigned day_of_year =
        (153 * shifted_month + 2) / 5 + day - 1;
    const unsigned day_of_era = year_of_era * 365 + year_of_era / 4 -
        year_of_era / 100 + day_of_year;
    const std::int64_t days = era * 146097LL + day_of_era - 719468LL;
    return days * 86400LL + static_cast<std::int64_t>(time.hour) * 3600LL;
}

bool IncludeCycloneState(CycloneState state, const CycloneFilterState &filters)
{
    switch(state) {
    case CycloneState::Tropical: return filters.tropical;
    case CycloneState::Subtropical: return filters.subtropical;
    case CycloneState::Extratropical: return filters.extratropical;
    case CycloneState::Remnant: return filters.remnant;
    default: return false;
    }
}

}  // namespace

TextureRasterResult BuildTextureRaster(
    std::shared_ptr<const ClimatologyDatasetSnapshot> snapshot,
    const TextureRasterRequest &request, RasterColorFunction color,
    const std::atomic<bool> *cancel)
{
    TextureRasterResult result;
    if(!snapshot || !color || request.month < 0 ||
       request.month >= static_cast<int>(kStoredMonths) ||
       request.width <= 0 || request.height <= 0 ||
       !std::isfinite(request.samples_per_degree) ||
       request.samples_per_degree <= 0.0)
        return result;

    const std::size_t pixels = static_cast<std::size_t>(request.width) *
        static_cast<std::size_t>(request.height);
    if(pixels > std::numeric_limits<std::size_t>::max() / 4)
        return result;

    ClimatologyQueryEngine query(snapshot);
    result.pixels.resize(pixels * 4);
    for(int x = 0; x < request.width; ++x) {
        if(cancel && cancel->load(std::memory_order_acquire)) {
            result.pixels.clear();
            result.cancelled = true;
            return result;
        }
        for(int y = 0; y < request.height; ++y) {
            double latitude = kPi * (2.0 * y / request.height - 1.0);
            latitude = 2.0 * 180.0 / kPi * std::atan(std::exp(latitude)) -
                90.0;
            const double longitude = x / request.samples_per_degree;
            const double value = query.ValueMonth(
                request.field, QueryComponent::Magnitude,
                latitude + request.latitude_offset,
                longitude + request.longitude_offset, request.month);
            const RgbaPixel rgba = color(request.color_field, value);
            const std::size_t offset = 4 *
                (static_cast<std::size_t>(y) * request.width + x);
            result.pixels[offset] = rgba.red;
            result.pixels[offset + 1] = rgba.green;
            result.pixels[offset + 2] = rgba.blue;
            result.pixels[offset + 3] = rgba.alpha;
        }
    }
    result.completed = true;
    return result;
}

CycloneIndexResult BuildCycloneIndex(
    std::shared_ptr<const ClimatologyDatasetSnapshot> snapshot,
    const CycloneFilterState &filters, const std::atomic<bool> *cancel)
{
    CycloneIndexResult result;
    if(!snapshot || !snapshot->availability.cyclones)
        return result;

    for(std::size_t basin = 0; basin < snapshot->cyclones.basins.size();
        ++basin) {
        const std::vector<CycloneTrack> &tracks =
            snapshot->cyclones.basins[basin];
        for(std::vector<CycloneTrack>::const_iterator track = tracks.begin();
            track != tracks.end(); ++track) {
            if(cancel && cancel->load(std::memory_order_acquire)) {
                result.index.clear();
                result.cancelled = true;
                return result;
            }
            for(std::vector<CycloneSegment>::const_iterator state =
                    track->segments.begin(); state != track->segments.end();
                ++state) {
                const CycloneSegment *segment = &*state;
                if(segment->wind_knots < filters.minimum_wind_knots ||
                   segment->pressure_hpa > filters.maximum_pressure_hpa)
                    continue;
                const std::int64_t unix_time = CivilUnixSeconds(segment->time);
                if(unix_time < filters.start_utc || unix_time > filters.end_utc)
                    continue;

                std::map<int, std::array<double, kClimatologyMonths> >
                    ::const_iterator enso =
                    snapshot->enso_by_year.find(segment->time.year);
                if(enso == snapshot->enso_by_year.end()) {
                    if(!filters.include_enso_unavailable &&
                       !snapshot->enso_by_year.empty())
                        continue;
                } else {
                    const double value = enso->second[segment->time.month];
                    if(!std::isfinite(value)) {
                        if(!filters.include_enso_unavailable) continue;
                    } else if(value >= .5) {
                        if(!filters.include_el_nino) continue;
                    } else if(value <= -.5) {
                        if(!filters.include_la_nina) continue;
                    } else if(!filters.include_neutral) {
                        continue;
                    }
                }
                if(!IncludeCycloneState(segment->state, filters))
                    continue;

                const int longitude0 = segment->longitude0 < 15
                    ? segment->longitude0 : segment->longitude0 - 360;
                const int longitude1 = segment->longitude0 < 15
                    ? segment->longitude1 : segment->longitude1 - 360;
                const int latitude0 = segment->latitude0;
                const int latitude1 = segment->latitude1;
                for(int longitude = std::min(longitude0, longitude1);
                    longitude <= std::max(longitude0, longitude1); ++longitude)
                    for(int latitude = std::min(latitude0, latitude1);
                        latitude <= std::max(latitude0, latitude1); ++latitude) {
                        const int hash = (longitude * 180 + latitude) * 12 +
                            segment->time.month;
                        result.index[hash].push_back(segment);
                    }
            }
        }
    }
    result.completed = true;
    return result;
}

}  // namespace climatology
