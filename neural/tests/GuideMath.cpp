// SPDX-License-Identifier: GPL-3.0-only
#include "GuideMath.h"
#include <cassert>
#include <cmath>
#include <limits>
using namespace ttwvrnr;
int main() {
  // Row-vector D3D perspective, near=1, far=100; camera translated two units
  // along +X. A point on the current centre ray at z=10 was at NDC x=0.2.
  const float projection[16] = {
      1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 100.f / 99.f, 1, 0, 0, -100.f / 99.f, 0};
  const float before[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
  const float current[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, -2, 0, 0, 1};
  float transform[16];
  assert(ClipToPrevious(current, projection, before, projection, transform));
  const double clip[4] = {0, 0, (10. * 100. / 99. - 100. / 99.) / 10., 1};
  double mapped[4]{};
  for (unsigned c = 0; c < 4; ++c)
    for (unsigned k = 0; k < 4; ++k)
      mapped[c] += clip[k] * transform[k * 4 + c];
  assert(std::abs(mapped[0] / mapped[3] - .2) < 1e-5);
  assert(std::abs(mapped[1] / mapped[3]) < 1e-6);
  assert(ClipToPrevious(before, projection, before, projection, transform));
  for (unsigned r = 0; r < 4; ++r)
    for (unsigned c = 0; c < 4; ++c)
      assert(std::abs(transform[r * 4 + c] - float(r == c)) < 1e-5);
  float zero[16]{};
  assert(!ClipToPrevious(zero, projection, before, projection, transform));
  zero[0] = std::numeric_limits<float>::quiet_NaN();
  assert(!ClipToPrevious(zero, projection, before, projection, transform));
  assert(std::abs(Halton(1, 2) - .5f) < 1e-6);
  assert(std::abs(Halton(2, 2) - .25f) < 1e-6);
  for (unsigned i = 1; i <= 8; ++i)
    for (unsigned base : {2u, 3u})
      assert(std::abs(Halton(i, base) - .5f) <= .5f);
}
