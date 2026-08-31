#include "ClimatologyDataset.h"
#include "ClimatologyQueryEngine.h"
#include "ClimatologyRenderPreparation.h"

#include <atomic>
#include <cassert>
#include <cmath>
#include <memory>

using namespace climatology;

static std::atomic<bool> *cancel_from_color = 0;
static int color_calls = 0;

static RgbaPixel TestColor(int, double value)
{
    ++color_calls;
    if(cancel_from_color && color_calls == 10)
        cancel_from_color->store(true, std::memory_order_release);
    RgbaPixel pixel;
    if(std::isfinite(value)) {
        pixel.red = static_cast<std::uint8_t>(value);
        pixel.alpha = 255;
    }
    return pixel;
}

static GridGeometry Geometry()
{
    GridGeometry geometry;
    geometry.rows = 2;
    geometry.columns = 4;
    geometry.latitude_origin = 45.0;
    geometry.longitude_origin = 0.0;
    geometry.latitude_step = 90.0;
    geometry.longitude_step = 90.0;
    geometry.latitude_decreases = true;
    return geometry;
}

int main()
{
    std::shared_ptr<ClimatologyDatasetSnapshot> mutable_snapshot(
        new ClimatologyDatasetSnapshot);
    MonthlyScalarField* sst =
        mutable_snapshot->MutableScalar(DatasetField::SeaSurfaceTemperature);
    assert(sst);
    sst->geometry = Geometry();
    sst->available[0] = true;
    sst->available[1] = true;
    sst->values[0].assign(8, 10.0f);
    sst->values[1].assign(8, 20.0f);
    mutable_snapshot->availability.fields[
        static_cast<std::size_t>(DatasetField::SeaSurfaceTemperature)] = true;

    CycloneSegment cyclone_segment;
    cyclone_segment.state = CycloneState::Tropical;
    cyclone_segment.time.year = 2020;
    cyclone_segment.time.month = 0;
    cyclone_segment.time.day = 15;
    cyclone_segment.latitude0 = 10.0;
    cyclone_segment.longitude0 = -50.0;
    cyclone_segment.latitude1 = 11.0;
    cyclone_segment.longitude1 = -49.0;
    cyclone_segment.wind_knots = 50.0;
    cyclone_segment.pressure_hpa = 990.0;
    CycloneTrack cyclone_track;
    cyclone_track.segments.push_back(cyclone_segment);
    mutable_snapshot->cyclones.basins[0].push_back(cyclone_track);
    mutable_snapshot->availability.cyclones = true;
    mutable_snapshot->enso_by_year[2020].fill(0.0);

    assert(mutable_snapshot->IsConsistent());
    std::shared_ptr<const ClimatologyDatasetSnapshot> snapshot = mutable_snapshot;
    ClimatologyQueryEngine query(snapshot);

    MonthPosition middle;
    middle.month = 0;
    middle.next_month = 1;
    middle.current_weight = 0.5;
    assert(std::fabs(query.Value(DatasetField::SeaSurfaceTemperature,
                                 QueryComponent::Magnitude, 0.0, 359.0,
                                 middle) - 15.0) < 1e-9);

    // Missing values remain missing unless a valid neighbour can supply the
    // interpolation, matching the modernisation contract.
    mutable_snapshot.reset();
    assert(query.Ready());
    assert(std::isnan(query.ValueMonth(DatasetField::Current,
                                       QueryComponent::Magnitude,
                                       0.0, 0.0, 0)));

    TextureRasterRequest raster_request;
    raster_request.field = DatasetField::SeaSurfaceTemperature;
    raster_request.color_field = 3;
    raster_request.month = 0;
    raster_request.width = 9;
    raster_request.height = 8;
    raster_request.samples_per_degree = 1.0 / 45.0;
    color_calls = 0;
    const TextureRasterResult raster = BuildTextureRaster(
        snapshot, raster_request, TestColor);
    assert(raster.completed);
    assert(!raster.cancelled);
    assert(raster.pixels.size() == 9u * 8u * 4u);
    assert(raster.pixels[0] == 10);
    assert(raster.pixels[3] == 255);

    std::atomic<bool> cancel(false);
    cancel_from_color = &cancel;
    color_calls = 0;
    const TextureRasterResult cancelled = BuildTextureRaster(
        snapshot, raster_request, TestColor, &cancel);
    cancel_from_color = 0;
    assert(!cancelled.completed);
    assert(cancelled.cancelled);
    assert(cancelled.pixels.empty());

    TextureRasterRequest invalid = raster_request;
    invalid.width = 0;
    assert(!BuildTextureRaster(snapshot, invalid, TestColor).completed);

    CycloneFilterState filters;
    filters.minimum_wind_knots = 35;
    filters.maximum_pressure_hpa = 1010;
    filters.start_utc = 1577836800; // 2020-01-01 UTC
    filters.end_utc = 1609459199;   // 2020-12-31 UTC
    const CycloneIndexResult cyclone_index =
        BuildCycloneIndex(snapshot, filters);
    assert(cyclone_index.completed);
    assert(!cyclone_index.cancelled);
    assert(!cyclone_index.index.empty());

    filters.minimum_wind_knots = 60;
    const CycloneIndexResult filtered = BuildCycloneIndex(snapshot, filters);
    assert(filtered.completed);
    assert(filtered.index.empty());

    cancel.store(true, std::memory_order_release);
    const CycloneIndexResult cancelled_cyclones =
        BuildCycloneIndex(snapshot, filters, &cancel);
    assert(cancelled_cyclones.cancelled);
    assert(!cancelled_cyclones.completed);
    return 0;
}
