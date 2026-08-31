#include "ClimatologyDataset.h"

#include <cmath>
#include <limits>

namespace climatology {
namespace {

double PositiveDegrees(double value)
{
    value = std::fmod(value, 360.0);
    return value < 0.0 ? value + 360.0 : value;
}

int ScalarIndex(DatasetField field)
{
    const int value = static_cast<int>(field);
    const int first = static_cast<int>(DatasetField::SeaLevelPressure);
    const int last = static_cast<int>(DatasetField::SeaDepth);
    return value >= first && value <= last ? value - first : -1;
}

}  // namespace

bool GridGeometry::IsValid() const
{
    return rows > 0 && columns > 0 && std::isfinite(latitude_origin) &&
           std::isfinite(longitude_origin) && latitude_step > 0.0 &&
           longitude_step > 0.0 && minimum_latitude <= maximum_latitude;
}

std::size_t GridGeometry::CellCount() const
{
    if(!IsValid() || rows > std::numeric_limits<std::size_t>::max() / columns)
        return 0;
    return rows * columns;
}

double GridGeometry::Row(double latitude) const
{
    if(!std::isfinite(latitude) || latitude < minimum_latitude ||
       latitude > maximum_latitude || !IsValid())
        return std::numeric_limits<double>::quiet_NaN();
    return latitude_decreases
        ? (latitude_origin - latitude) / latitude_step
        : (latitude - latitude_origin) / latitude_step;
}

double GridGeometry::Column(double longitude) const
{
    if(!std::isfinite(longitude) || !IsValid())
        return std::numeric_limits<double>::quiet_NaN();
    double delta = longitude - longitude_origin;
    if(wraps_longitude)
        delta = PositiveDegrees(delta);
    return delta / longitude_step;
}

bool MonthlyScalarField::IsConsistent() const
{
    const std::size_t cells = geometry.CellCount();
    if(!cells)
        return false;
    for(std::size_t month = 0; month < kStoredMonths; ++month)
        if(available[month] && values[month].size() != cells)
            return false;
    return true;
}

bool MonthlyVectorField::IsConsistent() const
{
    const std::size_t cells = geometry.CellCount();
    if(!cells)
        return false;
    for(std::size_t month = 0; month < kStoredMonths; ++month)
        if(available[month] &&
           (eastward[month].size() != cells ||
            northward[month].size() != cells))
            return false;
    return true;
}

bool MonthlyWindDistributionField::IsConsistent() const
{
    const std::size_t cells = geometry.CellCount();
    if(!cells || frequency_divisor <= 0.0 || speed_divisor <= 0.0)
        return false;
    for(std::size_t month = 0; month < kStoredMonths; ++month)
        if(available[month] && values[month].size() != cells)
            return false;
    return true;
}

bool CycloneCatalog::Empty() const
{
    for(std::size_t basin = 0; basin < basins.size(); ++basin)
        if(!basins[basin].empty())
            return false;
    return true;
}

const MonthlyScalarField* ClimatologyDatasetSnapshot::Scalar(
    DatasetField field) const
{
    const int index = ScalarIndex(field);
    return index < 0 ? 0 : &scalar[static_cast<std::size_t>(index)];
}

MonthlyScalarField* ClimatologyDatasetSnapshot::MutableScalar(
    DatasetField field)
{
    const int index = ScalarIndex(field);
    return index < 0 ? 0 : &scalar[static_cast<std::size_t>(index)];
}

bool ClimatologyDatasetSnapshot::IsConsistent(std::string* reason) const
{
    if(availability.fields[static_cast<std::size_t>(DatasetField::Wind)] &&
       !wind.IsConsistent()) {
        if(reason) *reason = "wind field is inconsistent";
        return false;
    }
    if(availability.fields[static_cast<std::size_t>(DatasetField::Current)] &&
       !current.IsConsistent()) {
        if(reason) *reason = "current field is inconsistent";
        return false;
    }
    for(int field = static_cast<int>(DatasetField::SeaLevelPressure);
        field <= static_cast<int>(DatasetField::SeaDepth); ++field) {
        const DatasetField typed = static_cast<DatasetField>(field);
        if(availability.fields[static_cast<std::size_t>(typed)] &&
           !Scalar(typed)->IsConsistent()) {
            if(reason) *reason = "scalar field is inconsistent";
            return false;
        }
    }
    if(availability.cyclones && cyclones.Empty()) {
        if(reason) *reason = "cyclone catalog is marked available but empty";
        return false;
    }
    return true;
}

}  // namespace climatology
