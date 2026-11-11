// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <cstdint>
#include <d3d9.h>
namespace ttwvrnr {
constexpr unsigned OutputWidth = 2944, OutputHeight = 2208;
constexpr unsigned InputWidth = 1963, InputHeight = 1472;
using SRTarget = HRESULT(STDMETHODCALLTYPE *)(IDirect3DDevice9 *, DWORD,
                                              IDirect3DSurface9 *);
HRESULT SuperResolutionSetTarget(IDirect3DDevice9 *, DWORD, IDirect3DSurface9 *,
                                 SRTarget);
bool InstallSuperResolution(IDirect3DDevice9 *);
void SuperResolutionFrame(bool ready);
void SuperResolutionObserveTone(IDirect3DDevice9 *);
bool SuperResolutionLow();
IDirect3DSurface9 *SuperResolutionDepth();
bool SuperResolutionBindOutput(IDirect3DDevice9 *, IDirect3DTexture9 *);
void SuperResolutionRestoreTone(IDirect3DDevice9 *);
} // namespace ttwvrnr
