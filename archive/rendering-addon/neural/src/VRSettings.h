// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <d3d9.h>
#include <windows.h>
namespace ttwvrnr {
struct TTWVRBuild;
bool InstallVRSettings(HMODULE module, IDirect3DDevice9 *device,
                       const TTWVRBuild &layout);
bool NativeVRSettingsInstalled();
unsigned CurrentMsaaSamples();
void ObserveMSAATarget(IDirect3DSurface9 *surface);
} // namespace ttwvrnr
