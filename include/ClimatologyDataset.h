#ifndef CLIMATOLOGY_DATASET_H
#define CLIMATOLOGY_DATASET_H

#include <array>
#include <cstddef>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace climatology {

static const std::size_t kClimatologyMonths = 12;
static const std::size_t kAllTimesMonth = 12;
static const std::size_t kStoredMonths = 13;
static const std::size_t kWindSectors = 8;

enum class DatasetField : std::uint32_t {
    Wind = 0,
    Current = 1,
    SeaLevelPressure = 2,
    SeaSurfaceTemperature = 3,
    AirTemperature = 4,
    CloudCover = 5,
    Precipitation = 6,
    RelativeHumidity = 7,
    Lightning = 8,
    SeaDepth = 9,
    Count = 10
};

struct GridGeometry {
    std::size_t rows = 0;
    std::size_t columns = 0;
    double latitude_origin = 0.0;
    double longitude_origin = 0.0;
    double latitude_step = 0.0;
    double longitude_step = 0.0;
    bool latitude_decreases = true;
    bool wraps_longitude = true;
    double minimum_latitude = -90.0;
    double maximum_latitude = 90.0;

    bool IsValid() const;
    std::size_t CellCount() const;
    double Row(double latitude) const;
    double Column(double longitude) const;
};

struct MonthlyScalarField {
    GridGeometry geometry;
    std::array<std::vector<float>, kStoredMonths> values;
    std::array<bool, kStoredMonths> available = {{false}};

    bool IsConsistent() const;
};

struct MonthlyVectorField {
    GridGeometry geometry;
    std::array<std::vector<float>, kStoredMonths> eastward;
    std::array<std::vector<float>, kStoredMonths> northward;
    std::array<bool, kStoredMonths> available = {{false}};

    bool IsConsistent() const;
};

struct WindDistributionCell {
    bool valid = false;
    std::uint8_t calm_percent = 0;
    std::uint8_t gale_percent = 0;
    std::array<std::uint8_t, kWindSectors> frequency = {{0}};
    std::array<std::uint8_t, kWindSectors> encoded_speed = {{0}};
};

struct MonthlyWindDistributionField {
    GridGeometry geometry;
    double frequency_divisor = 1.0;
    double speed_divisor = 1.0;
    std::array<std::vector<WindDistributionCell>, kStoredMonths> values;
    std::array<bool, kStoredMonths> available = {{false}};

    bool IsConsistent() const;
};

enum class CycloneState : std::uint8_t {
    Tropical,
    Subtropical,
    Extratropical,
    Wave,
    Remnant,
    Unknown
};

struct CivilTime {
    int year = 0;
    int month = 0;
    int day = 0;
    int hour = 0;
};

struct CycloneSegment {
    CycloneState state = CycloneState::Unknown;
    CivilTime time;
    double latitude0 = 0.0;
    double longitude0 = 0.0;
    double latitude1 = 0.0;
    double longitude1 = 0.0;
    double wind_knots = 0.0;
    double pressure_hpa = 0.0;
};

struct CycloneTrack {
    std::vector<CycloneSegment> segments;
};

struct CycloneCatalog {
    std::array<std::vector<CycloneTrack>, 6> basins;
    // Spatial key -> packed basin/track/segment references.
    std::map<int, std::vector<std::uint64_t> > crossing_index;

    bool Empty() const;
};

struct DatasetMetadata {
    std::string version = "legacy/unversioned";
    std::string period_start;
    std::string period_end;
    std::string period_kind;
    bool manifest_validated = false;
};

struct DatasetAvailability {
    std::array<bool, static_cast<std::size_t>(DatasetField::Count)> fields =
        {{false}};
    bool cyclones = false;
    bool enso = false;
};

class ClimatologyDatasetSnapshot {
public:
    MonthlyWindDistributionField wind;
    MonthlyVectorField current;
    std::array<MonthlyScalarField, 8> scalar;
    CycloneCatalog cyclones;
    std::map<int, std::array<double, kClimatologyMonths> > enso_by_year;
    DatasetMetadata metadata;
    DatasetAvailability availability;

    const MonthlyScalarField* Scalar(DatasetField field) const;
    MonthlyScalarField* MutableScalar(DatasetField field);
    bool IsConsistent(std::string* reason = 0) const;
};

}  // namespace climatology

#endif
