// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <array>
#include <cstdint>
#include <windows.h>
namespace ttwvrnr {
// Select a complete, reviewed layout by the on-disk DLL hash. Never mix RVAs
// between versions or accept an unknown build by a short instruction signature.
struct TTWVRBuild {
  const char *version, *sha256;
  uintptr_t deviceGetter, device, forceInt;
  uintptr_t menuBuild, menuEditing;
  std::array<uintptr_t, 3> upscalerRows;
  uintptr_t settings;
  uintptr_t guidesPrepare, guidesReady, guidesDepth, guidesSize, eyeSetup;
  uintptr_t motionPrepare, motionSubmit, activeEye, motionRendering,
      motionSurfaces;
};
const TTWVRBuild *IdentifyTTWVRBuild(HMODULE module);
} // namespace ttwvrnr
