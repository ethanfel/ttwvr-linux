// SPDX-License-Identifier: GPL-3.0-only
#include "SceneJitter.h"
#include "NativeGuides.h"
#include "SuperResolution.h"
#include "Support.h"
#include <array>
#include <chrono>
#include <cstring>
#include <map>
#include <vector>
#include <wrl/client.h>
namespace ttwvrnr {
namespace {
using Microsoft::WRL::ComPtr;
bool sampleTiming = false;
int64_t cpuNanoseconds = 0;
unsigned sampledDraws = 0;
struct CpuTimer {
  std::chrono::steady_clock::time_point start;
  CpuTimer() {
    if (sampleTiming)
      start = std::chrono::steady_clock::now();
  }
  ~CpuTimer() {
    if (sampleTiming)
      cpuNanoseconds += std::chrono::duration_cast<std::chrono::nanoseconds>(
                            std::chrono::steady_clock::now() - start)
                            .count();
  }
};
struct Binding {
  ComPtr<IDirect3DVertexShader9> shader;
  int reg = -1;
};
std::map<IDirect3DVertexShader9 *, Binding> bindings;
// Parse only bounded D3D9 CTAB comments with a named row-major 4x4 MVP.
int FindBinding(IDirect3DVertexShader9 *shader) {
  if (!shader)
    return -1;
  if (auto it = bindings.find(shader); it != bindings.end())
    return it->second.reg;
  Binding binding;
  binding.shader = shader;
  UINT size = 0;
  if (FAILED(shader->GetFunction(nullptr, &size)) || size > 1048576 || size < 8)
    return -1;
  std::vector<unsigned char> bytes(size);
  if (FAILED(shader->GetFunction(bytes.data(), &size)))
    return -1;
  auto word = [&](size_t p) {
    uint32_t v = 0;
    if (p + 4 <= size)
      std::memcpy(&v, bytes.data() + p, 4);
    return v;
  };
  for (size_t cursor = 4; cursor + 8 <= size;) {
    const auto token = word(cursor);
    if ((token & 0xffff) != 0xfffe)
      break;
    const size_t length = ((token >> 16) & 0x7fff) * 4;
    if (length < 4 || cursor + 4 + length > size)
      break;
    if (word(cursor + 4) == 0x42415443 && length >= 32) {
      const size_t base = cursor + 8, end = cursor + 4 + length;
      auto u32 = [&](size_t p) { return p + 4 <= end ? word(p) : 0u; };
      const uint32_t count = u32(base + 12), info = u32(base + 16);
      if (count < 256 && info + uint64_t(count) * 20 <= end - base) {
        for (unsigned i = 0; i < count; ++i) {
          const size_t ci = base + info + i * 20;
          const size_t nameOffset = u32(ci), typeOffset = u32(ci + 12);
          if (nameOffset >= end - base || typeOffset > end - base - 16)
            continue;
          const size_t name = base + nameOffset, type = base + typeOffset;
          const char *n = reinterpret_cast<const char *>(bytes.data() + name);
          const auto *zero = std::memchr(n, 0, end - name);
          if (!zero)
            continue;
          const bool matrix = !std::strcmp(n, "ModelViewProj") ||
                              !std::strcmp(n, "SkinModelViewProj");
          const bool rows = !std::strcmp(n, "MVP");
          if (!matrix && !rows)
            continue;
          const uint32_t registerInfo = u32(ci + 4), counts = u32(ci + 8),
                         typeClass = u32(type), shape = u32(type + 4);
          // TTWVR's replacement lighting shaders declare MVP as float4[4],
          // while engine shaders declare row_major float4x4. Both were checked
          // in captured bytecode: c0..3 dot the vertex into clip x/y/z/w.
          const bool rowArray = (typeClass & 0xffff) == 1 &&
                                shape == 0x00040001 &&
                                (u32(type + 8) & 0xffff) == 4;
          const bool rowMatrix =
              (typeClass & 0xffff) == 2 && shape == 0x00040004;
          if ((registerInfo & 0xffff) != 2 || (counts & 0xffff) != 4 ||
              !((matrix && rowMatrix) || (rows && rowArray)))
            continue;
          const unsigned reg = registerInfo >> 16;
          if (reg + 4 <= 256)
            binding.reg = int(reg);
        }
      }
    }
    cursor += 4 + length;
  }
  const int result = binding.reg;
  bindings.emplace(shader, std::move(binding));
  return result;
}
} // namespace
void SceneJitterFrame(uint64_t nextFrame) {
  if (sampleTiming)
    Log("scene_jitter_cpu frame=%llu draws=%u cpu_ms=%.3f",
        (unsigned long long)(nextFrame - 1), sampledDraws,
        double(cpuNanoseconds) / 1000000.0);
  static const bool profile =
      GetPrivateProfileIntW(
          L"diagnostics", L"gpu_timing", 0,
          (Directory() + L"\\TTWVRNeuralRendering.ini").c_str()) != 0;
  sampleTiming = profile && nextFrame % 120 == 0;
  cpuNanoseconds = 0;
  sampledDraws = 0;
}
SceneJitter::SceneJitter(IDirect3DDevice9 *d, bool bypass) {
  CpuTimer timer;
  float x = 0, y = 0;
  if (bypass || !NativeJitter(x, y))
    return;
  ComPtr<IDirect3DSurface9> rt, ds;
  D3DSURFACE_DESC r{}, z{};
  if (FAILED(d->GetRenderTarget(0, &rt)) || !rt || FAILED(rt->GetDesc(&r)) ||
      r.Width != (SuperResolutionLow() ? InputWidth : OutputWidth) ||
      r.Height != (SuperResolutionLow() ? InputHeight : OutputHeight) ||
      r.Format != D3DFMT_A16B16G16R16F ||
      FAILED(d->GetDepthStencilSurface(&ds)) || !ds ||
      FAILED(ds->GetDesc(&z)) || z.Width != r.Width || z.Height != r.Height ||
      z.Format != D3DFMT_D24S8)
    return;
  ComPtr<IDirect3DVertexShader9> shader;
  if (FAILED(d->GetVertexShader(&shader)))
    return;
  const int reg = FindBinding(shader.Get());
  if (reg < 0)
    return;
  first = unsigned(reg);
  if (FAILED(d->GetVertexShaderConstantF(first, saved, 4)))
    return;
  float jittered[16];
  std::memcpy(jittered, saved, sizeof(saved));
  for (unsigned i = 0; i < 4; ++i) {
    jittered[i] -= 2 * x / float(r.Width) * saved[12 + i];
    jittered[4 + i] += 2 * y / float(r.Height) * saved[12 + i];
  }
  if (SUCCEEDED(d->SetVertexShaderConstantF(first, jittered, 4))) {
    device = d;
    if (sampleTiming)
      ++sampledDraws;
  }
}
SceneJitter::~SceneJitter() {
  CpuTimer timer;
  if (device)
    device->SetVertexShaderConstantF(first, saved, 4);
}
} // namespace ttwvrnr
