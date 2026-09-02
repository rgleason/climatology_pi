#ifndef CLIMATOLOGY_PROVIDER_API_H
#define CLIMATOLOGY_PROVIDER_API_H

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <type_traits>

// Versioned, transport-independent companion to the legacy Weather Routing
// callbacks in ClimatologyAPI.h.  Only fixed-width arithmetic types cross the
// in-process plugin-message boundary.
namespace climatology_api {

static const std::uint32_t QUERY_API_V1 = 1;
static const std::size_t WIND_SECTOR_COUNT = 8;

// Avoid Xlib's legacy Status macro, which is pulled in by GLX on older Linux
// build images before consumers include this public header.
enum QueryStatus : std::int32_t {
    STATUS_OK = 0,
    STATUS_UNKNOWN = 1,
    STATUS_INVALID_REQUEST = 2,
    STATUS_UNSUPPORTED_VERSION = 3,
    STATUS_UNSUPPORTED_VARIABLE = 4,
    STATUS_UNSUPPORTED_SCENARIO = 5,
    STATUS_NOT_READY = 6
};

enum Scenario : std::uint32_t {
    SCENARIO_ALL_YEARS = 0,
    SCENARIO_ENSO_NEUTRAL = 1,
    SCENARIO_EL_NINO = 2,
    SCENARIO_LA_NINA = 3
};

enum Variable : std::uint32_t {
    VARIABLE_WIND_10M = 0,
    VARIABLE_SURFACE_CURRENT = 1,
    VARIABLE_SEA_LEVEL_PRESSURE = 2,
    VARIABLE_SEA_SURFACE_TEMPERATURE = 3,
    VARIABLE_AIR_TEMPERATURE = 4,
    VARIABLE_CLOUD_COVER = 5,
    VARIABLE_PRECIPITATION = 6,
    VARIABLE_RELATIVE_HUMIDITY = 7,
    VARIABLE_LIGHTNING = 8,
    VARIABLE_SEA_DEPTH = 9
};

enum ValueKind : std::uint32_t {
    VALUE_KIND_NONE = 0,
    VALUE_KIND_SCALAR = 1,
    VALUE_KIND_VECTOR = 2,
    VALUE_KIND_WIND_DISTRIBUTION = 3
};

enum Unit : std::uint32_t {
    UNIT_NONE = 0,
    UNIT_KNOTS = 1,
    UNIT_HECTOPASCALS = 2,
    UNIT_DEGREES_CELSIUS = 3,
    UNIT_PERCENT = 4,
    UNIT_MILLIMETRES_PER_DAY = 5,
    UNIT_LIGHTNING_INDEX = 6,
    UNIT_METRES = 7
};

enum DirectionConvention : std::uint32_t {
    DIRECTION_NONE = 0,
    DIRECTION_METEOROLOGICAL_FROM_TRUE_NORTH = 1,
    DIRECTION_OCEANOGRAPHIC_TO_TRUE_NORTH = 2
};

enum ResultFlag : std::uint32_t {
    RESULT_HAS_SCALAR = 1u << 0,
    RESULT_HAS_VECTOR = 1u << 1,
    RESULT_HAS_WIND_DISTRIBUTION = 1u << 2,
    RESULT_HAS_CALM_PROBABILITY = 1u << 3,
    RESULT_HAS_GALE_PROBABILITY = 1u << 4,
    RESULT_HAS_SAMPLE_COUNT = 1u << 5,
    RESULT_HAS_INDEPENDENT_EVENT_COUNT = 1u << 6,
    RESULT_HAS_STANDARD_ERROR = 1u << 7
};

struct QueryV1 {
    std::uint32_t struct_size;
    std::uint32_t api_version;
    std::uint32_t variable;
    std::uint32_t scenario;
    std::int64_t unix_time_utc;
    double latitude_degrees_north;
    double longitude_degrees_east;
    std::uint64_t reserved[4];
};

struct ResultV1 {
    std::uint32_t struct_size;
    std::uint32_t api_version;
    std::int32_t status;
    std::uint32_t variable;
    std::uint32_t scenario;
    std::uint32_t value_kind;
    std::uint32_t unit;
    std::uint32_t direction_convention;
    std::uint32_t flags;

    double scalar_value;
    double eastward_value;
    double northward_value;
    double speed;
    double direction_degrees_true;

    double sector_direction_degrees_true[WIND_SECTOR_COUNT];
    double sector_frequency[WIND_SECTOR_COUNT];
    double sector_speed_knots[WIND_SECTOR_COUNT];
    double calm_probability;
    double gale_probability;

    std::uint32_t sample_count;
    std::uint32_t independent_event_count;
    double standard_error;
    std::uint64_t reserved[8];
};

using QueryV1Function = std::int32_t (*)(const QueryV1*, ResultV1*);

inline QueryV1 MakeQueryV1()
{
    QueryV1 query = {};
    query.struct_size = sizeof(QueryV1);
    query.api_version = QUERY_API_V1;
    query.scenario = SCENARIO_ALL_YEARS;
    return query;
}

inline ResultV1 MakeResultV1()
{
    ResultV1 result = {};
    result.struct_size = sizeof(ResultV1);
    result.api_version = QUERY_API_V1;
    result.status = STATUS_UNKNOWN;
    return result;
}

inline QueryStatus ValidateQueryV1(const QueryV1 *query)
{
    if(!query || query->struct_size < sizeof(QueryV1))
        return STATUS_INVALID_REQUEST;
    if(query->api_version != QUERY_API_V1)
        return STATUS_UNSUPPORTED_VERSION;
    if(query->scenario != SCENARIO_ALL_YEARS)
        return STATUS_UNSUPPORTED_SCENARIO;
    if(query->variable > VARIABLE_SEA_DEPTH)
        return STATUS_UNSUPPORTED_VARIABLE;
    if(!std::isfinite(query->latitude_degrees_north) ||
       !std::isfinite(query->longitude_degrees_east) ||
       query->latitude_degrees_north < -90.0 ||
       query->latitude_degrees_north > 90.0)
        return STATUS_INVALID_REQUEST;
    return STATUS_OK;
}

static_assert(std::is_standard_layout<QueryV1>::value,
              "Climatology QueryV1 must remain standard-layout");
static_assert(std::is_trivially_copyable<QueryV1>::value,
              "Climatology QueryV1 must remain trivially copyable");
static_assert(std::is_standard_layout<ResultV1>::value,
              "Climatology ResultV1 must remain standard-layout");
static_assert(std::is_trivially_copyable<ResultV1>::value,
              "Climatology ResultV1 must remain trivially copyable");

}  // namespace climatology_api

#endif
