#ifndef CLIMATOLOGY_MATH_H
#define CLIMATOLOGY_MATH_H

#include <cmath>
#include <cstddef>

namespace climatology {

// C++11 has no standard pi constant, and M_PI is an optional extension which
// MSVC only exposes when configured before the first <cmath> include.
constexpr double kPi = 3.14159265358979323846;

double Wrap360(double degrees);
double InterpolateMissing(double first, double second, double fraction);
double InterpolateAngleRadians(double first, double second, double fraction);
int CircularDayDistance(int first, int second, int days = 365);

struct MonthInterpolationResult {
    int month;
    int neighbour;
    double current_weight;
};

MonthInterpolationResult MonthInterpolation(int month, int day,
                                            double day_fraction,
                                            int days_in_month);

template <typename Getter>
double Bilinear(std::size_t rows, std::size_t columns, double row, double column,
                Getter getter, bool wrap_columns = true) {
    if (!rows || !columns || !std::isfinite(row) || !std::isfinite(column))
        return NAN;

    if (row < 0.0)
        row = 0.0;
    if (row > static_cast<double>(rows - 1))
        row = static_cast<double>(rows - 1);

    if (wrap_columns) {
        column = std::fmod(column, static_cast<double>(columns));
        if (column < 0.0)
            column += static_cast<double>(columns);
    } else {
        if (column < 0.0)
            column = 0.0;
        if (column > static_cast<double>(columns - 1))
            column = static_cast<double>(columns - 1);
    }

    const std::size_t row0 = static_cast<std::size_t>(std::floor(row));
    const std::size_t row1 = row0 + 1 < rows ? row0 + 1 : row0;
    const std::size_t column0 = static_cast<std::size_t>(std::floor(column));
    const std::size_t column1 = column0 + 1 < columns ? column0 + 1
                                                      : (wrap_columns ? 0 : column0);
    const double dr = row - static_cast<double>(row0);
    const double dc = column - static_cast<double>(column0);
    const double values[4] = {getter(row0, column0), getter(row0, column1),
                              getter(row1, column0), getter(row1, column1)};
    const double weights[4] = {(1.0 - dr) * (1.0 - dc), (1.0 - dr) * dc,
                               dr * (1.0 - dc), dr * dc};
    double weighted = 0.0;
    double weight = 0.0;
    for (int index = 0; index < 4; ++index) {
        if (std::isfinite(values[index]) && weights[index] > 0.0) {
            weighted += values[index] * weights[index];
            weight += weights[index];
        }
    }
    // At an exact grid point every non-selected weight is zero. The selected
    // point must still be returned even if row/column pairs collapse.
    if (weight == 0.0 && std::isfinite(values[0]))
        return values[0];
    return weight == 0.0 ? NAN : weighted / weight;
}

}  // namespace climatology

#endif
