// SPDX-License-Identifier: GPL-3.0-only
// Adapted from Ethanfel TF2VRNeuralRendering; see THIRD-PARTY-NOTICES.md.
#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>

namespace ttwvrnr {
// Internal matrices multiply column vectors. Transpose only at the NGX ABI.
using Matrix4 = std::array<float, 16>;
inline Matrix4 Identity4() {
  return {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
}
inline Matrix4 Multiply4(const Matrix4 &a, const Matrix4 &b) {
  Matrix4 r{};
  for (unsigned y = 0; y < 4; ++y)
    for (unsigned x = 0; x < 4; ++x)
      for (unsigned k = 0; k < 4; ++k)
        r[y * 4 + x] += a[y * 4 + k] * b[k * 4 + x];
  return r;
}
inline Matrix4 Transpose4(const Matrix4 &a) {
  Matrix4 r{};
  for (unsigned y = 0; y < 4; ++y)
    for (unsigned x = 0; x < 4; ++x)
      r[y * 4 + x] = a[x * 4 + y];
  return r;
}
inline bool Inverse4(const Matrix4 &a, Matrix4 &result) {
  double m[4][8]{};
  for (unsigned y = 0; y < 4; ++y)
    for (unsigned x = 0; x < 4; ++x) {
      m[y][x] = a[y * 4 + x];
      m[y][x + 4] = x == y;
    }
  for (unsigned x = 0; x < 4; ++x) {
    unsigned pivot = x;
    for (unsigned y = x + 1; y < 4; ++y)
      if (std::abs(m[y][x]) > std::abs(m[pivot][x]))
        pivot = y;
    if (!std::isfinite(m[pivot][x]) || std::abs(m[pivot][x]) < 1e-12)
      return false;
    for (unsigned k = 0; k < 8; ++k)
      std::swap(m[x][k], m[pivot][k]);
    const double scale = m[x][x];
    for (auto &v : m[x])
      v /= scale;
    for (unsigned y = 0; y < 4; ++y)
      if (y != x) {
        const double s = m[y][x];
        for (unsigned k = 0; k < 8; ++k)
          m[y][k] -= s * m[x][k];
      }
  }
  for (unsigned y = 0; y < 4; ++y)
    for (unsigned x = 0; x < 4; ++x)
      result[y * 4 + x] = static_cast<float>(m[y][x + 4]);
  return true;
}
struct FrameCamera {
  Matrix4 worldToClip{}, viewToClip{};
  std::array<float, 3> position{}, right{}, up{}, forward{};
  float nearPlane = 0, farPlane = 0, fov = 0, aspect = 0;
  bool valid = false;
  const char *rejection = "uninitialized";
};
// TTWVR supplies verified, unjittered row-vector view/projection matrices.
// Convert only their storage convention; retain its left-handed projection.
inline FrameCamera DecodeTTWVRCamera(const float *view,
                                     const float *projection) {
  FrameCamera r;
  Matrix4 v, p;
  for (unsigned y = 0; y < 4; ++y)
    for (unsigned x = 0; x < 4; ++x) {
      if (!std::isfinite(view[y * 4 + x]) ||
          !std::isfinite(projection[y * 4 + x]))
        return r;
      v[x * 4 + y] = view[y * 4 + x];
      p[x * 4 + y] = projection[y * 4 + x];
    }
  if (std::abs(p[14] - 1) > 1e-5f || std::abs(p[15]) > 1e-5f || p[0] <= 0 ||
      p[5] <= 0 || p[10] <= 1)
    return r;
  r.nearPlane = -p[11] / p[10];
  r.farPlane = -p[11] / (p[10] - 1);
  if (r.nearPlane <= 0 || r.farPlane <= r.nearPlane)
    return r;
  r.fov = std::atan((1 - p[6]) / p[5]) + std::atan((1 + p[6]) / p[5]);
  r.aspect = p[5] / p[0];
  r.viewToClip = p;
  r.worldToClip = Multiply4(p, v);
  Matrix4 inverse;
  if (!Inverse4(v, inverse))
    return r;
  for (unsigned k = 0; k < 3; ++k) {
    r.position[k] = inverse[k * 4 + 3];
    r.right[k] = inverse[k * 4];
    r.up[k] = inverse[k * 4 + 1];
    r.forward[k] = inverse[k * 4 + 2];
  }
  r.valid = Inverse4(r.worldToClip, inverse);
  r.rejection = r.valid ? "accepted" : "singular_matrix";
  return r;
}
inline bool CameraCut(const FrameCamera &a, const FrameCamera &b) {
  if (!a.valid || !b.valid)
    return true;
  float distance = 0, dot = 0;
  for (unsigned k = 0; k < 3; ++k) {
    float d = a.position[k] - b.position[k];
    distance += d * d;
    dot += a.forward[k] * b.forward[k];
  }
  return distance > 64 * 64 || dot < .866f || std::abs(a.fov - b.fov) > .0873f;
}
} // namespace ttwvrnr
