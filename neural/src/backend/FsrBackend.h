// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "FrameCamera.h"
#include <cstdint>
#include <d3d11.h>
#include <memory>
#include <string>
namespace ttwvrnr {
class FsrBackend {
  struct Impl;
  std::unique_ptr<Impl> impl_;

public:
  FsrBackend();
  ~FsrBackend();
  bool Init(ID3D11Device *, unsigned, unsigned, unsigned, unsigned, unsigned,
            const std::wstring &, bool profile);
  bool Render(ID3D11Texture2D *color, ID3D11Texture2D *depth,
              ID3D11Texture2D *motion, ID3D11Texture2D *exposure,
              ID3D11Texture2D *output, unsigned eye, uint64_t frame, bool reset,
              float jitterX, float jitterY, const FrameCamera &, float meters);
  void LogCompletedGpuTiming();
  bool Shutdown();
};
} // namespace ttwvrnr
