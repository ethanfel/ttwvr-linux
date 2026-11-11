// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <cstdint>
namespace ttwvrnr {
// Fixed-width cross-bitness protocol. Images stay on the GPU. Event handoffs
// publish this single-writer control block; only one request may be
// outstanding.
struct HostProtocol {
  uint32_t magic = 0x54544e52, version = 1, width = 0, height = 0;
  uint64_t colorHandle = 0, depthHandle = 0, bloomHandle = 0, frame = 0;
  uint32_t eye = 0, command = 0, result = 0, parentPid = 0;
  float hdrParam = 1, intensity = .5f;
  uint32_t identity = 0, reserved = 0;
};
static_assert(sizeof(HostProtocol) == 80);
} // namespace ttwvrnr
