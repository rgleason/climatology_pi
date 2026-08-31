#include "ClimatologyProviderAPI.h"

#include <cassert>
#include <limits>
#include <type_traits>

using namespace climatology_api;

static_assert(std::is_same<QueryV1Function,
                           std::int32_t (*)(const QueryV1*, ResultV1*)>::value,
              "The typed query callback ABI changed");
static_assert(WIND_SECTOR_COUNT == 8,
              "V1 wind distributions use the legacy eight sectors");

int main()
{
    QueryV1 query = MakeQueryV1();
    assert(query.struct_size == sizeof(QueryV1));
    assert(query.api_version == QUERY_API_V1);
    assert(query.scenario == SCENARIO_ALL_YEARS);

    ResultV1 result = MakeResultV1();
    assert(result.struct_size == sizeof(ResultV1));
    assert(result.api_version == QUERY_API_V1);
    assert(result.status == STATUS_UNKNOWN);

    query.variable = VARIABLE_WIND_10M;
    query.latitude_degrees_north = 0.0;
    query.longitude_degrees_east = 0.0;
    assert(ValidateQueryV1(&query) == STATUS_OK);

    query.scenario = SCENARIO_EL_NINO;
    assert(ValidateQueryV1(&query) == STATUS_UNSUPPORTED_SCENARIO);

    query.scenario = SCENARIO_ALL_YEARS;
    query.latitude_degrees_north =
        std::numeric_limits<double>::quiet_NaN();
    assert(ValidateQueryV1(&query) == STATUS_INVALID_REQUEST);
    return 0;
}
