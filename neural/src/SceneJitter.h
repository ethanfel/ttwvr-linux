// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <cstdint>
#include <d3d9.h>
namespace ttwvrnr {
void SceneJitterFrame(uint64_t nextFrame);
// Only the verified scene MVP registers change, for this draw alone.
class SceneJitter {
  IDirect3DDevice9 *device = nullptr;
  unsigned first = 0;
  float saved[16]{};

public:
  SceneJitter(IDirect3DDevice9 *, bool bypass);
  ~SceneJitter();
  SceneJitter(const SceneJitter &) = delete;
  SceneJitter &operator=(const SceneJitter &) = delete;
};
} // namespace ttwvrnr
