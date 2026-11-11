// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <cstdint>
#include <d3d9.h>
#include <string>
namespace ttwvrnr {
void CaptureExtraSurface(IDirect3DDevice9 *, IDirect3DSurface9 *,
                         const std::wstring &path);
void CaptureSceneBeforeTone(IDirect3DDevice9 *, uint64_t frame, unsigned pass);
void CaptureSceneAfterTone(IDirect3DDevice9 *, uint64_t frame, unsigned pass);
void FinishSceneCapture();
} // namespace ttwvrnr
