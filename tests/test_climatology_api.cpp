#include "ClimatologyAPI.h"

#include <type_traits>

// Exact declarations from Weather Routing's RouteMap interface.  If either
// project changes qualifiers or arguments, this build-time compatibility test
// fails before a mismatched function pointer can be exchanged at runtime.
using WeatherRoutingData = bool (*)(int, const wxDateTime&, double, double,
                                    double&, double&);
using WeatherRoutingAtlas = bool (*)(const wxDateTime&, double, double, int&,
                                     double*, double*, double&, double&);
using WeatherRoutingCyclones = int (*)(double, double, double, double,
                                       const wxDateTime&, int);

static_assert(std::is_same<ClimatologyDataFunction, WeatherRoutingData>::value,
              "ClimatologyData ABI no longer matches Weather Routing");
static_assert(std::is_same<ClimatologyWindAtlasFunction, WeatherRoutingAtlas>::value,
              "WindAtlas ABI no longer matches Weather Routing");
static_assert(std::is_same<ClimatologyCycloneCrossingsFunction,
                           WeatherRoutingCyclones>::value,
              "Cyclone crossing ABI no longer matches Weather Routing");

int main() { return 0; }
