// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <cstdint>
#include <windows.h>
struct IDirect3DDevice9;
struct IDirect3DSurface9;
namespace ttwvrnr {
bool InstallNativeGuides(HMODULE verifiedModule);
bool NativeGuidesEnabled();
struct TemporalGuides {
  float clipToPrevious[16]{};
  float jitterX = 0, jitterY = 0;
  bool reset = true;
  IDirect3DSurface9 *motion =
      nullptr; // borrowed only until the current tone pass ends
};
bool GetTemporalGuides(uint64_t frame, unsigned eye, TemporalGuides &);
void NativeGuidesFrame(uint64_t frame);
bool NativeJitter(float &x, float &y);
void NativeGuidesObserveScene(IDirect3DDevice9 *, uint64_t frame);
void NativeGuidesBeforeTone(IDirect3DDevice9 *, uint64_t frame, unsigned eye,
                            bool capture);
} // namespace ttwvrnr
