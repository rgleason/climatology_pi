#include "ClimatologyMath.h"
#include "ClimatologyState.h"

#include <cassert>
#include <cmath>
#include <vector>

static bool close(double first, double second, double tolerance = 1e-10) {
    return std::fabs(first - second) <= tolerance;
}

int main() {
    assert(close(climatology::Wrap360(-0.25), 359.75));
    assert(close(climatology::Wrap360(720.5), 0.5));

    const double angle = climatology::InterpolateAngleRadians(
        359.0 * M_PI / 180.0, 1.0 * M_PI / 180.0, 0.5);
    assert(close(climatology::Wrap360(angle * 180.0 / M_PI), 0.0));

    const std::vector<double> grid = {0.0, 10.0, 20.0, 30.0};
    const auto getter = [&grid](std::size_t row, std::size_t column) {
        return grid[row * 2 + column];
    };
    assert(close(climatology::Bilinear(2, 2, 0.5, 0.5, getter), 15.0));
    assert(close(climatology::Bilinear(2, 2, -2.0, 0.0, getter), 0.0));
    assert(close(climatology::Bilinear(2, 2, 3.0, 1.0, getter), 30.0));

    const std::vector<double> dateline = {10.0, 20.0, 30.0, 40.0};
    const auto wrap_getter = [&dateline](std::size_t, std::size_t column) {
        return dateline[column];
    };
    assert(close(climatology::Bilinear(1, 4, 0.0, 3.5, wrap_getter), 25.0));

    const std::vector<double> missing = {NAN, 10.0, 20.0, 30.0};
    const auto missing_getter = [&missing](std::size_t row, std::size_t column) {
        return missing[row * 2 + column];
    };
    assert(close(climatology::Bilinear(2, 2, 0.5, 0.5, missing_getter), 20.0));
    assert(std::isnan(climatology::InterpolateMissing(NAN, NAN, 0.5)));
    assert(close(climatology::InterpolateMissing(NAN, 7.0, 0.5), 7.0));

    assert(climatology::CircularDayDistance(2, 363) == 4);
    assert(climatology::CircularDayDistance(363, 2) == 4);
    assert(climatology::CircularDayDistance(100, 110) == 10);

    assert(close(climatology::CalibrationForField(0, 0).Apply(10.0), 10.0));
    assert(close(climatology::CalibrationForField(0, 1).Apply(10.0),
                 10.0 * 1.852 / 3.6));
    assert(close(climatology::CalibrationForField(2, 1).Apply(1013.0),
                 1013.0 / 1.33));
    assert(close(climatology::CalibrationForField(3, 1).Apply(20.0), 68.0));
    assert(close(climatology::CalibrationForField(6, 0).Apply(1.0),
                 1000.0 / 365.24));
    assert(close(climatology::CalibrationForField(6, 5).Apply(1.0), 3.28));
    assert(close(climatology::CalibrationForField(9, 1).Apply(100.0), 328.0));

    const auto february_mid = climatology::MonthInterpolation(1, 15, 0.0, 29);
    assert(february_mid.month == 1);
    assert(february_mid.neighbour == 0);
    assert(close(february_mid.current_weight, 1.0));
    const auto year_wrap = climatology::MonthInterpolation(11, 31, 0.5, 31);
    assert(year_wrap.neighbour == 0);
    assert(close(year_wrap.current_weight, 0.5));
    return 0;
}
