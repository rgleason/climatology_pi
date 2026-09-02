#include "ClimatologyMath.h"

#define _USE_MATH_DEFINES
#include <cmath>
#include <algorithm>
#include <cstdlib>

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

int CircularDayDistance(int first, int second, int days) {
    if (days <= 0)
        return 0;
    int difference = std::abs(first - second) % days;
    return std::min(difference, days - difference);
}

MonthInterpolationResult MonthInterpolation(int month, int day,
                                            double day_fraction,
                                            int days_in_month) {
    if (month < 0 || month > 11 || days_in_month <= 0)
        return {month, month, 1.0};
    const double position = std::max(0.0, std::min(
        1.0, (day - 0.5 + day_fraction) / days_in_month));
    if (position > 0.5)
        return {month, (month + 1) % 12, 1.5 - position};
    return {month, (month + 11) % 12, 0.5 + position};
}

}  // namespace climatology
