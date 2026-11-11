// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <d3d9.h>
namespace ttwvrnr {
// D24 depth -> device-local buffer -> shared RGBA8 bytes. No CPU image mapping.
bool TransferDepthBits(IDirect3DDevice9 *, IDirect3DSurface9 *depth,
                       IDirect3DSurface9 *packed);
} // namespace ttwvrnr
