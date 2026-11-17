// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <algorithm>
#include <cmath>
#include <map>
#include <utility>
#include <vector>

namespace climatology {
// Value-owned, immutable after publication. No UI controls or CycloneState
// pointers are accessed by readers, including while a replacement is built.
class CycloneTrackIndex {
public:
  struct Segment {
    double a, b, c, d;
    int day;
  };
  static double Wrap(double longitude) {
    longitude = std::fmod(longitude, 360.0);
    return longitude < 0 ? longitude + 360.0 : longitude;
  }
  static double Align(double longitude, double reference) {
    const double delta = std::remainder(longitude - reference, 360.0);
    return reference + delta;
  }
  static bool Valid(double a, double b, double c, double d) {
    return std::isfinite(a) && std::isfinite(b) && std::isfinite(c) &&
           std::isfinite(d) && std::abs(a) <= 90 && std::abs(c) <= 90;
  }
  void Add(double a, double b, double c, double d, int day) {
    if (!Valid(a, b, c, d) || day < 0 || day >= 365) return;
    b = Wrap(b);
    d = Align(Wrap(d), b);
    const Segment segment{a, b, c, d, day};
    for (int row = int(std::floor(std::min(a, c)));
         row <= int(std::floor(std::max(a, c))); ++row)
      for (int col = int(std::floor(std::min(b, d)));
           col <= int(std::floor(std::max(b, d))); ++col)
        cells_[Key(row, col)].push_back(segment);
  }
  int Crossings(double a, double b, double c, double d, int day,
                int days) const {
    if (!Valid(a, b, c, d) || day < 0 || day >= 365 || days < 1)
      return -1;
    b = Wrap(b);
    d = Align(Wrap(d), b);
    for (int row = int(std::floor(std::min(a, c)));
         row <= int(std::floor(std::max(a, c))); ++row)
      for (int col = int(std::floor(std::min(b, d)));
           col <= int(std::floor(std::max(b, d))); ++col) {
        const auto found = cells_.find(Key(row, col));
        if (found == cells_.end()) continue;
        for (auto s : found->second) {
          const int delta = std::abs(day - s.day);
          if (days < 365 && std::min(delta, 365 - delta) > days / 2) continue;
          const double shift = Align(s.b, b) - s.b;
          s.b += shift;
          s.d += shift;
          if (Intersects({a, b, c, d, day}, s)) return 1;
        }
      }
    return 0;
  }
  std::size_t CellCount() const { return cells_.size(); }

private:
  static std::pair<int, int> Key(int row, int col) {
    return {row, (col % 360 + 360) % 360};
  }
  static double Cross(double a, double b, double c, double d, double e,
                      double f) {
    return (c - a) * (f - b) - (d - b) * (e - a);
  }
  static bool Intersects(const Segment& x, const Segment& y) {
    const double eps = 1e-10;
    if (std::max(std::min(x.a, x.c), std::min(y.a, y.c)) >
            std::min(std::max(x.a, x.c), std::max(y.a, y.c)) + eps ||
        std::max(std::min(x.b, x.d), std::min(y.b, y.d)) >
            std::min(std::max(x.b, x.d), std::max(y.b, y.d)) + eps)
      return false;
    const double a = Cross(x.a, x.b, x.c, x.d, y.a, y.b);
    const double b = Cross(x.a, x.b, x.c, x.d, y.c, y.d);
    const double c = Cross(y.a, y.b, y.c, y.d, x.a, x.b);
    const double d = Cross(y.a, y.b, y.c, y.d, x.c, x.d);
    return ((a <= eps && b >= -eps) || (b <= eps && a >= -eps)) &&
           ((c <= eps && d >= -eps) || (d <= eps && c >= -eps));
  }
  std::map<std::pair<int, int>, std::vector<Segment>> cells_;
};
}  // namespace climatology
