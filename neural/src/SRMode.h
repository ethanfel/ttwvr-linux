// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <cstdint>
namespace ttwvrnr {
// Shared layer/host contract for the verified TTWVR 150% output layout.
// Input sizes are checked against the selected SDK on every helper start.
constexpr unsigned OutputWidth = 2944, OutputHeight = 2208;
struct SRSize {
  unsigned width, height;
};
constexpr bool ValidSRMode(unsigned mode) { return mode <= 6; }
constexpr bool IsFSR(unsigned mode) { return mode >= 4 && mode <= 6; }
constexpr unsigned SRQuality(unsigned mode) {
  return IsFSR(mode) ? mode - 3 : mode;
}
constexpr SRSize SRInputSize(unsigned mode) {
  return mode == 0   ? SRSize{OutputWidth, OutputHeight}
         : mode == 1 ? SRSize{1963, 1472}
         : mode == 2 ? SRSize{1708, 1281}
         : mode == 3 ? SRSize{1472, 1104}
         : mode == 4 ? SRSize{1962, 1472}
         : mode == 5 ? SRSize{1731, 1298}
         : mode == 6 ? SRSize{1472, 1104}
                     : SRSize{0, 0};
}
constexpr const char *SRName(unsigned mode) {
  return mode == 0   ? "Off"
         : mode == 1 ? "Quality"
         : mode == 2 ? "Balanced"
         : mode == 3 ? "Performance"
         : mode == 4 ? "FSR Quality"
         : mode == 5 ? "FSR Balanced"
         : mode == 6 ? "FSR Performance"
                     : "Invalid";
}
constexpr unsigned SRJitterPhases(unsigned mode) {
  const auto s = SRInputSize(mode);
  const uint64_t pixels = uint64_t(s.width) * s.height;
  if (IsFSR(mode))
    return s.width ? unsigned(8ull * OutputWidth * OutputWidth /
                              (uint64_t(s.width) * s.width))
                   : 0;
  return pixels ? unsigned((8ull * OutputWidth * OutputHeight + pixels / 2) /
                           pixels)
                : 0;
}
} // namespace ttwvrnr
