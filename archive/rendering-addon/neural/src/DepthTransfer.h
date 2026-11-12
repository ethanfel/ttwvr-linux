// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <d3d9.h>
namespace ttwvrnr {
// D24 depth -> device-local buffer -> shared RGBA8 bytes. No CPU image mapping.
// With wait=true, the same-queue fence completes this copy and all earlier
// D3D9 work. With wait=false, the caller MUST signal a GPU semaphore after
// this submission and order every consumer after it. Command storage is still
// protected by a completion fence before reuse. No CPU image mapping occurs.
bool TransferDepthBits(IDirect3DDevice9 *, IDirect3DSurface9 *depth,
                       IDirect3DSurface9 *packed, bool wait = true);
} // namespace ttwvrnr
