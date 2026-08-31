#include "ClimatologyQueryEngine.h"

#include "ClimatologyMath.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace climatology {
namespace {

double NaN()
{
    return std::numeric_limits<double>::quiet_NaN();
}

double Sample(const GridGeometry& geometry, const std::vector<float>& values,
              double latitude, double longitude)
{
    const double row = geometry.Row(latitude);
    const double column = geometry.Column(longitude);
    if(!std::isfinite(row) || !std::isfinite(column))
        return NaN();
    return Bilinear(geometry.rows, geometry.columns, row, column,
        [&values, &geometry](std::size_t r, std::size_t c) {
            const float value = values[r * geometry.columns + c];
            return std::isfinite(value) ? static_cast<double>(value) : NaN();
        });
}

double DirectionDegrees(double eastward, double northward)
{
    if(!std::isfinite(eastward) || !std::isfinite(northward) ||
       (eastward == 0.0 && northward == 0.0))
        return NaN();
    double degrees = std::atan2(eastward, northward) * 180.0 / M_PI;
    return degrees < 0.0 ? degrees + 360.0 : degrees;
}

double InterpolateDirection(double first, double second, double first_weight)
{
    if(!std::isfinite(first)) return second;
    if(!std::isfinite(second)) return first;
    if(first - second > 180.0) first -= 360.0;
    if(second - first > 180.0) second -= 360.0;
    double value = first_weight * first + (1.0 - first_weight) * second;
    value = std::fmod(value, 360.0);
    return value < 0.0 ? value + 360.0 : value;
}

double WindCellValue(const MonthlyWindDistributionField& field,
                     const WindDistributionCell& cell,
                     QueryComponent component)
{
    if(!cell.valid)
        return NaN();
    double total_frequency = 0.0;
    double weighted = 0.0;
    for(std::size_t sector = 0; sector < kWindSectors; ++sector) {
        const double frequency = cell.frequency[sector] / field.frequency_divisor;
        const double speed = cell.encoded_speed[sector] / field.speed_divisor;
        total_frequency += frequency;
        double multiplier = 1.0;
        if(component == QueryComponent::Eastward)
            multiplier = std::sin(sector * 2.0 * M_PI / kWindSectors);
        else if(component == QueryComponent::Northward)
            multiplier = std::cos(sector * 2.0 * M_PI / kWindSectors);
        weighted += multiplier * speed * frequency;
    }
    return total_frequency > 0.0 ? weighted / total_frequency : NaN();
}

double SampleWindComponent(const MonthlyWindDistributionField& field,
                           int month, QueryComponent component,
                           double latitude, double longitude)
{
    if(month < 0 || month >= static_cast<int>(kStoredMonths) ||
       !field.available[month])
        return NaN();
    const double row = field.geometry.Row(latitude);
    const double column = field.geometry.Column(longitude);
    if(!std::isfinite(row) || !std::isfinite(column))
        return NaN();
    const std::vector<WindDistributionCell>& values = field.values[month];
    return Bilinear(field.geometry.rows, field.geometry.columns, row, column,
        [&field, &values, component](std::size_t r, std::size_t c) {
            return WindCellValue(field,
                                 values[r * field.geometry.columns + c],
                                 component);
        });
}

bool SegmentIntersection(double x1, double y1, double x2, double y2,
                         double x3, double y3, double x4, double y4)
{
    const double ax = x2 - x1;
    const double ay = y2 - y1;
    const double bx = x3 - x4;
    const double by = y3 - y4;
    const double cx = x1 - x3;
    const double cy = y1 - y3;
    const double denominator = ay * bx - ax * by;
    if(std::fabs(denominator) < 2e-14)
        return true;  // Preserve the historical conservative result.
    const double inverse = 1.0 / denominator;
    const double a = (by * cx - bx * cy) * inverse;
    const double b = (ax * cy - ay * cx) * inverse;
    return a >= -2e-7 && a <= 1.0 + 2e-7 &&
           b >= -2e-7 && b <= 1.0 + 2e-7;
}

}  // namespace

ClimatologyQueryEngine::ClimatologyQueryEngine(
    std::shared_ptr<const ClimatologyDatasetSnapshot> snapshot)
    : m_snapshot(snapshot)
{
}

double ClimatologyQueryEngine::ValueMonth(
    DatasetField field, QueryComponent component, double latitude,
    double longitude, int month) const
{
    if(!m_snapshot || month < 0 || month >= static_cast<int>(kStoredMonths))
        return NaN();

    if(field == DatasetField::Wind) {
        if(component == QueryComponent::Magnitude) {
            const double eastward = SampleWindComponent(
                m_snapshot->wind, month, QueryComponent::Eastward,
                latitude, longitude);
            const double northward = SampleWindComponent(
                m_snapshot->wind, month, QueryComponent::Northward,
                latitude, longitude);
            return std::isfinite(eastward) && std::isfinite(northward)
                ? std::hypot(eastward, northward) : NaN();
        }
        if(component == QueryComponent::Direction) {
            const double eastward = SampleWindComponent(
                m_snapshot->wind, month, QueryComponent::Eastward,
                latitude, longitude);
            const double northward = SampleWindComponent(
                m_snapshot->wind, month, QueryComponent::Northward,
                latitude, longitude);
            return DirectionDegrees(eastward, northward);
        }
        return SampleWindComponent(m_snapshot->wind, month, component,
                                   latitude, longitude);
    }

    if(field == DatasetField::Current) {
        if(!m_snapshot->current.available[month])
            return NaN();
        const double eastward = Sample(m_snapshot->current.geometry,
            m_snapshot->current.eastward[month], latitude, longitude);
        const double northward = Sample(m_snapshot->current.geometry,
            m_snapshot->current.northward[month], latitude, longitude);
        if(component == QueryComponent::Eastward) return eastward;
        if(component == QueryComponent::Northward) return northward;
        if(component == QueryComponent::Magnitude)
            return std::isfinite(eastward) && std::isfinite(northward)
                ? std::hypot(eastward, northward) : NaN();
        return DirectionDegrees(eastward, northward);
    }

    if(component != QueryComponent::Magnitude)
        return NaN();
    const MonthlyScalarField* scalar = m_snapshot->Scalar(field);
    if(!scalar || !scalar->available[month])
        return NaN();
    return Sample(scalar->geometry, scalar->values[month], latitude, longitude);
}

double ClimatologyQueryEngine::Value(
    DatasetField field, QueryComponent component, double latitude,
    double longitude, const MonthPosition& position) const
{
    const double first = ValueMonth(field, component, latitude, longitude,
                                    position.month);
    const double second = ValueMonth(field, component, latitude, longitude,
                                     position.next_month);
    if(component == QueryComponent::Direction)
        return InterpolateDirection(first, second, position.current_weight);
    return InterpolateMissing(second, first, position.current_weight);
}

bool ClimatologyQueryEngine::WindDistribution(
    double latitude, double longitude, const MonthPosition& position,
    WindDistributionResult& result) const
{
    if(!m_snapshot)
        return false;
    const MonthlyWindDistributionField& field = m_snapshot->wind;
    if(position.month < 0 || position.next_month < 0 ||
       position.month >= static_cast<int>(kStoredMonths) ||
       position.next_month >= static_cast<int>(kStoredMonths) ||
       !field.available[position.month] ||
       !field.available[position.next_month])
        return false;

    const double row = field.geometry.Row(latitude);
    const double column = field.geometry.Column(longitude);
    if(!std::isfinite(row) || !std::isfinite(column))
        return false;

    auto sample = [&field, row, column](int month, std::size_t sector,
                                        bool speed) {
        return Bilinear(field.geometry.rows, field.geometry.columns,
                        row, column,
            [&field, month, sector, speed](std::size_t r, std::size_t c) {
                const WindDistributionCell& cell =
                    field.values[month][r * field.geometry.columns + c];
                if(!cell.valid) return NaN();
                return speed
                    ? cell.encoded_speed[sector] / field.speed_divisor
                    : cell.frequency[sector] / field.frequency_divisor;
            });
    };
    for(std::size_t sector = 0; sector < kWindSectors; ++sector) {
        const double f0 = sample(position.month, sector, false);
        const double f1 = sample(position.next_month, sector, false);
        result.frequency[sector] =
            InterpolateMissing(f1, f0, position.current_weight);
        const double s0 = sample(position.month, sector, true);
        const double s1 = sample(position.next_month, sector, true);
        result.speed_knots[sector] =
            InterpolateMissing(s1, s0, position.current_weight);
    }

    auto probability = [&field, row, column](int month, bool gale) {
        return Bilinear(field.geometry.rows, field.geometry.columns,
                        row, column,
            [&field, month, gale](std::size_t r, std::size_t c) {
                const WindDistributionCell& cell =
                    field.values[month][r * field.geometry.columns + c];
                if(!cell.valid) return NaN();
                return (gale ? cell.gale_percent : cell.calm_percent) / 100.0;
            });
    };
    result.calm_probability = InterpolateMissing(
        probability(position.next_month, false),
        probability(position.month, false), position.current_weight);
    result.gale_probability = InterpolateMissing(
        probability(position.next_month, true),
        probability(position.month, true), position.current_weight);
    return std::isfinite(result.calm_probability) &&
           std::isfinite(result.gale_probability);
}

int ClimatologyQueryEngine::CycloneTrackCrossings(
    double latitude0, double longitude0, double latitude1, double longitude1,
    int day_of_year, int day_range) const
{
    if(!m_snapshot)
        return -1;
    if(day_range == 0)
        return 0;
    if(m_snapshot->cyclones.Empty())
        return -1;
    const int half_range = std::min(day_range / 2, 182);
    for(std::size_t basin = 0; basin < m_snapshot->cyclones.basins.size(); ++basin)
        for(const CycloneTrack& track : m_snapshot->cyclones.basins[basin])
            for(const CycloneSegment& segment : track.segments) {
                const int segment_day =
                    segment.time.month * 365 / 12 + segment.time.day - 1;
                if(CircularDayDistance(segment_day, day_of_year) > half_range)
                    continue;
                double route_lon0 = longitude0;
                double route_lon1 = longitude1;
                while(route_lon0 - segment.longitude0 > 180.0)
                    route_lon0 -= 360.0, route_lon1 -= 360.0;
                while(route_lon0 - segment.longitude0 < -180.0)
                    route_lon0 += 360.0, route_lon1 += 360.0;
                if(SegmentIntersection(latitude0, route_lon0,
                                       latitude1, route_lon1,
                                       segment.latitude0, segment.longitude0,
                                       segment.latitude1, segment.longitude1))
                    return 1;
            }
    return 0;
}

}  // namespace climatology
