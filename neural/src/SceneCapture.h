// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <d3d9.h>
#include <cstdint>
namespace ttwvrnr {
void CaptureSceneBeforeTone(IDirect3DDevice9*,uint64_t frame,unsigned pass);
void CaptureSceneAfterTone(IDirect3DDevice9*,uint64_t frame,unsigned pass);
void FinishSceneCapture();
}
