#pragma once

#include <vector>

class UnifiedGrid
{
public:
    UnifiedGrid();

    // ---- Geometry ----
    int    rows;
    int    cols;
    double lat0;
    double lon0;
    double latStep;
    double lonStep;

    // ---- Raw scalar/vector fields ----
    std::vector<float> scalar;
    std::vector<float> u;
    std::vector<float> v;

    // ---- Validity ----
    bool isValid() const { return valid; }

    // ---- Initialization ----
    void InitScalar(int r, int c,
                    double la0, double lo0,
                    double laStep, double loStep);

    void InitVector(int r, int c,
                    double la0, double lo0,
                    double laStep, double loStep);

private:
    bool valid;
};
