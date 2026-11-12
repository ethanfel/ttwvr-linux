// SPDX-License-Identifier: GPL-3.0-only
// Adapted from Ethanfel TF2VRNeuralRendering; see THIRD-PARTY-NOTICES.md.
#pragma once
#include <cstdint>
#include <d3d11.h>
#include <memory>
#include <string>
namespace ttwvrnr {
struct NeuralOptions {
  unsigned resolutionPercent = 100;
  float intensity = 0.5f;
  float tone = 1.f;
  float detail = 1.f;
  unsigned preset = 2;     // NVIDIA NR preset B; A..G are 1..7.
  bool profileGpu = false; // Optional NGX GPU timestamp samples.
  bool neural = true;
  bool dlaa = false;
  unsigned sr = 0; // 0 off, 1 Quality, 2 Balanced, 3 Performance.
  // SR uses a lower-resolution input and a distinct native output.
  unsigned outputWidth = 0, outputHeight = 0;
  bool temporalGuides = false; // Explicit verified motion/jitter contract.
  bool passthrough =
      false; // Exercises the complete GPU conversion route without NGX.
};
class NeuralBackend {
  struct Impl;
  std::unique_ptr<Impl> impl_;

public:
  NeuralBackend();
  ~NeuralBackend();
  bool Init(ID3D11Device *, unsigned width, unsigned height,
            const std::wstring &runtimeDirectory, const NeuralOptions & = {});
  // GPU-only. The owner supplies HDR, non-inverted 0..1 depth,
  // current-to-previous pixel motion, and a scalar exposure multiplier. These
  // are a port contract, not Titanfall shader bindings. Until motion is
  // verified, always reset. Replaces the caller-owned HDR image; alpha is
  // preserved. NR retains signed HDR; DLAA preparation clamps negative RGB. One
  // feature/history per eye; the owner supplies eye identity and reset.
  bool Render(ID3D11Texture2D *hdr, ID3D11Texture2D *depth,
              ID3D11Texture2D *motion, ID3D11Texture2D *exposure, unsigned eye,
              uint64_t frame, bool reset, float jitterX = 0, float jitterY = 0,
              ID3D11Texture2D *output = nullptr);
  // Serialized host only: call after its existing GPU completion wait.
  void LogCompletedGpuTiming();
  bool Shutdown();
};
} // namespace ttwvrnr
