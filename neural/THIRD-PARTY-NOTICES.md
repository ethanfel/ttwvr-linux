# Notices

Capture layer and GPU backend adapted from https://github.com/ethanfel/TF2VRNeuralRendering,
GPL-3.0-only, copyright 2026 Ethanfel and contributors. Reference revision: 7e8c5d40a0eb5f6ec1ed485a36ff71d86bfd0eaa.

MinHook retains its upstream BSD license in third_party/minhook/LICENSE.txt.
NVIDIA SDKs and runtime DLLs are external dependencies under NVIDIA's terms;
none are redistributed here. No frame generation is included in this port.

DXVK interop declarations retain the zlib license in licenses/DXVK.txt.

The New Vegas renderer/camera layout was cross-checked against the MIT-licensed
[Kim2091/Remix-Wrappers FNV adapter](https://github.com/Kim2091/Remix-Wrappers/tree/main/FalloutNV-Remix-Wrapper).
The layout was then verified against this game's live shader constants and
image reprojection. No wrapper injector or game shader bytecode is distributed.
