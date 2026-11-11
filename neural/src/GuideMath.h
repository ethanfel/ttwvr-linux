// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <algorithm>
#include <cmath>
namespace ttwvrnr {
inline void Multiply44(const double *a, const double *b, double *out) {
  double product[16]{};
  for (unsigned r = 0; r < 4; ++r)
    for (unsigned c = 0; c < 4; ++c)
      for (unsigned k = 0; k < 4; ++k)
        product[r * 4 + c] += a[r * 4 + k] * b[k * 4 + c];
  std::copy(product, product + 16, out);
}
inline bool Inverse44(const double *input, double *output) {
  double a[4][8]{};
  for (unsigned r = 0; r < 4; ++r)
    for (unsigned c = 0; c < 4; ++c) {
      if (!std::isfinite(input[r * 4 + c]))
        return false;
      a[r][c] = input[r * 4 + c];
      a[r][c + 4] = r == c;
    }
  for (unsigned col = 0; col < 4; ++col) {
    unsigned pivot = col;
    for (unsigned r = col + 1; r < 4; ++r)
      if (std::abs(a[r][col]) > std::abs(a[pivot][col]))
        pivot = r;
    if (std::abs(a[pivot][col]) < 1e-12)
      return false;
    for (unsigned c = 0; c < 8; ++c)
      std::swap(a[pivot][c], a[col][c]);
    const double scale = a[col][col];
    for (double &v : a[col])
      v /= scale;
    for (unsigned r = 0; r < 4; ++r)
      if (r != col) {
        const double factor = a[r][col];
        for (unsigned c = 0; c < 8; ++c)
          a[r][c] -= factor * a[col][c];
      }
  }
  for (unsigned r = 0; r < 4; ++r)
    for (unsigned c = 0; c < 4; ++c)
      output[r * 4 + c] = a[r][c + 4];
  return true;
}
inline bool ClipToPrevious(const float *view, const float *projection,
                           const float *previousView,
                           const float *previousProjection, float *result) {
  double v[16], p[16], pv[16], pp[16], current[16], previous[16], inverse[16];
  for (unsigned i = 0; i < 16; ++i) {
    v[i] = view[i];
    p[i] = projection[i];
    pv[i] = previousView[i];
    pp[i] = previousProjection[i];
  }
  Multiply44(v, p, current);
  Multiply44(pv, pp, previous);
  if (!Inverse44(current, inverse))
    return false;
  Multiply44(inverse, previous, current);
  for (unsigned i = 0; i < 16; ++i) {
    if (!std::isfinite(current[i]))
      return false;
    result[i] = float(current[i]);
  }
  return true;
}
inline float Halton(unsigned index, unsigned base) {
  float value = 0, scale = 1;
  while (index) {
    scale /= float(base);
    value += scale * float(index % base);
    index /= base;
  }
  return value;
}
} // namespace ttwvrnr
