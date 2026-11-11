// SPDX-License-Identifier: GPL-3.0-only
// Derived from Ethanfel TF2VRNeuralRendering; see THIRD-PARTY-NOTICES.md.
#pragma once
#include <d3d11.h>
#include <string>
namespace ttwvrnr {
// Read-only immediate-context state at the capture point. These bindings are
// evidence for later investigation, not identified depth/motion/HDR guides.
std::string InspectBindings(ID3D11DeviceContext* context, ID3D11Texture2D* color,
    const char* stage = "before_xrReleaseSwapchainImage");
}
