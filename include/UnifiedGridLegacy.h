#ifndef _UNIFIED_GRID_LEGACY_H_
#define _UNIFIED_GRID_LEGACY_H_

#include <vector>
#include "GridInfo.h"

// ============================================================================
// UnifiedGridLegacy
// ============================================================================

class UnifiedGridLegacy
{
public:
    int rows, cols;
    double lat0, lon0;
    double latStep, lonStep;
    bool loaded;
    bool isVector;

    std::vector<double> scalar;
    std::vector<double> u;
    std::vector<double> v;

    UnifiedGridLegacy();

    void InitScalar(int r, int c,
                    double la0, double lo0,
                    double laStep, double loStep);

    void InitVector(int r, int c,
                    double la0, double lo0,
                    double laStep, double loStep);

    bool isValid() const;

    double valueAt(double lat, double lon) const;
    double uAt(double lat, double lon) const;
    double vAt(double lat, double lon) const;
};




#endif