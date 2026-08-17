#ifndef CLIMATOLOGY_API_H
#define CLIMATOLOGY_API_H

class wxDateTime;

// ABI consumed by Weather Routing through the CLIMATOLOGY plugin message.
// These signatures intentionally remain unchanged; dataset metadata is added
// to the JSON message rather than to any function call.
using ClimatologyDataFunction = bool (*)(int, const wxDateTime&, double, double,
                                         double&, double&);
using ClimatologyWindAtlasFunction = bool (*)(const wxDateTime&, double, double,
                                              int&, double*, double*, double&,
                                              double&);
using ClimatologyCycloneCrossingsFunction = int (*)(double, double, double,
                                                    double, const wxDateTime&,
                                                    int);

#endif
