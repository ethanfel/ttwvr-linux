// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <cstdint>
namespace ttwvrnr {
// Fixed-width cross-bitness protocol. Images stay on the GPU. Event handoffs
// publish this single-writer control block; only one request may be
// outstanding.
struct HostProtocol {
  uint32_t magic = 0x54544e52, version = 4, width = 0, height = 0;
  uint64_t colorHandle = 0, depthHandle = 0, bloomHandle = 0, motionHandle = 0,
           frame = 0;
  uint32_t eye = 0, command = 0, result = 0, parentPid = 0;
  float hdrParam = 1, intensity = .5f;
  uint32_t identity = 0, reserved = 0;
  float clipToPrevious[16]{};
  float jitterX = 0, jitterY = 0;
  uint32_t neural = 1, dlaa = 0, temporal = 0, reset = 1;
  uint32_t resolution = 100, preset = 2, strength = 50, tone = 100,
           detail = 100;
  uint32_t profile = 0, revision = 0, padding = 0;
  uint32_t sr = 0, outputWidth = 0, outputHeight = 0, padding2 = 0;
  uint64_t outputHandle = 0;
};
static_assert(sizeof(HostProtocol) == 232);
} // namespace ttwvrnr
