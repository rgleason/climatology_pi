#include "ClimatologyMath.h"

namespace climatology {

double Wrap360(double degrees) {
    if (!std::isfinite(degrees))
        return NAN;
    degrees = std::fmod(degrees, 360.0);
    return degrees < 0.0 ? degrees + 360.0 : degrees;
}

double InterpolateMissing(double first, double second, double fraction) {
    if (!std::isfinite(first))
        return second;
    if (!std::isfinite(second))
        return first;
    return (1.0 - fraction) * first + fraction * second;
}

double InterpolateAngleRadians(double first, double second, double fraction) {
    if (!std::isfinite(first))
        return second;
    if (!std::isfinite(second))
        return first;
    double delta = std::fmod(second - first, 2.0 * M_PI);
    if (delta > M_PI)
        delta -= 2.0 * M_PI;
    else if (delta < -M_PI)
        delta += 2.0 * M_PI;
    double value = first + fraction * delta;
    if (value > M_PI)
        value -= 2.0 * M_PI;
    else if (value < -M_PI)
        value += 2.0 * M_PI;
    return value;
}

}  // namespace climatology
