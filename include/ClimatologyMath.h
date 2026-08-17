#ifndef CLIMATOLOGY_MATH_H
#define CLIMATOLOGY_MATH_H

#include <cmath>
#include <cstddef>

namespace climatology {

double Wrap360(double degrees);
double InterpolateMissing(double first, double second, double fraction);
double InterpolateAngleRadians(double first, double second, double fraction);

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
            column += columns;
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
    const double dr = row - row0;
    const double dc = column - column0;
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
