// SPDX-License-Identifier: GPL-3.0-only
// Experimental adapter to the exact, hash-verified TTWVR motion renderer.
// Does not activate TTWVR's upscaler or alter its on-disk binary.
#include "NativeGuides.h"
#include "GuideMath.h"
#include "SceneCapture.h"
#include "SpatialNR.h"
#include "Support.h"
#include "TTWVRBuild.h"
#include "VRSettings.h"
#include <MinHook.h>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <d3d9.h>
#include <vector>
#include <wrl/client.h>
namespace ttwvrnr {
namespace {
using Microsoft::WRL::ComPtr;
using Prepare = bool(__cdecl *)(IDirect3DDevice9 *, const unsigned *, unsigned,
                                const float *);
using EyeSetup = void(__cdecl *)(unsigned, bool);
EyeSetup eyeSetup = nullptr;
using Ready = bool(__cdecl *)();
using Depth = IDirect3DSurface9 *(__cdecl *)();
using Size = void(__cdecl *)(unsigned, unsigned, unsigned *, unsigned *);
Prepare prepare = nullptr;
Ready ready = nullptr;
Depth depth = nullptr;
Size size = nullptr;
unsigned char *base = nullptr;
const TTWVRBuild *layout = nullptr;
bool enabled = false, captureProbe = false, profile = false;
bool jitterProbe = false, applyJitter = false;
TemporalGuides guides[2];
uint64_t guideFrames[2] = {~0ull, ~0ull}, lastToneTime[2]{};
float jitterX = 0, jitterY = 0;
thread_local bool preparing = false;
ComPtr<IDirect3DSurface9> sceneDepth;
uint64_t attempts = 0, prepared = 0;
unsigned Width = 2944, Height = 2208;
struct Camera {
  uint64_t frame = ~0ull;
  float view[16]{}, projection[16]{}, origin[4]{};
  unsigned verified = 0, rejected = 0;
  float error = 0;
  double cpuMs = 0;
};
Camera cameras[2], previous[2];
void MatrixJson(FILE *out, const float *m) {
  fputc('[', out);
  for (unsigned i = 0; i < 16; ++i) {
    if (i)
      fputc(',', out);
    if (std::isfinite(m[i]))
      fprintf(out, "%.9g", m[i]);
    else
      fputs("null", out);
  }
  fputc(']', out);
}
void __cdecl HookEyeSetup(unsigned eye, bool activeUpscaler) {
  eyeSetup(eye, activeUpscaler);
  if (!NativeGuidesEnabled() || activeUpscaler || eye > 1 || !sceneDepth ||
      *reinterpret_cast<int *>(base + layout->activeEye) >= 0)
    return;
  auto *d = *reinterpret_cast<IDirect3DDevice9 **>(base + layout->device);
  if (d)
    reinterpret_cast<void(__cdecl *)(IDirect3DDevice9 *, unsigned, float,
                                     float)>(base + layout->motionPrepare)(
        d, eye, jitterX, jitterY);
}
bool __cdecl HookReady() { return preparing ? true : ready(); }
IDirect3DSurface9 *__cdecl HookDepth() {
  return preparing && sceneDepth ? sceneDepth.Get() : depth();
}
void __cdecl HookSize(unsigned w, unsigned h, unsigned *outW, unsigned *outH) {
  if (preparing) {
    *outW = w;
    *outH = h;
  } else
    size(w, h, outW, outH);
}
bool __cdecl HookPrepare(IDirect3DDevice9 *d, const unsigned *eyes,
                         unsigned count, const float *jitter) {
  if (!NativeGuidesEnabled())
    return prepare(d, eyes, count, jitter);
  ++attempts;
  ComPtr<IDirect3DSurface9> current;
  d->GetDepthStencilSurface(&current);
  D3DSURFACE_DESC desc{};
  if (current && SUCCEEDED(current->GetDesc(&desc)) && desc.Width == Width &&
      desc.Height == Height && desc.Format == D3DFMT_D24S8)
    sceneDepth = current;
  preparing = !!sceneDepth;
  const bool result = prepare(d, eyes, count, jitter);
  preparing = false;
  prepared += result;
  if (attempts <= 4 || attempts % 600 == 0)
    Log("native_guides_prepare attempts=%llu prepared=%llu result=%d count=%u "
        "depth=%p config=%u",
        (unsigned long long)attempts, (unsigned long long)prepared, result,
        count, sceneDepth.Get(), unsigned(*(base + layout->settings + 0xe8)));
  return result;
}
} // namespace
bool InstallNativeGuides(HMODULE module, const TTWVRBuild &build) {
  layout = &build;
  const auto ini = Directory() + L"\\TTWVRNeuralRendering.ini";
  // Install on the verified TTWVR binary even when initially off. Hook work
  // stays gated by NativeGuidesEnabled; off -> on no longer needs a restart.
  profile =
      GetPrivateProfileIntW(L"diagnostics", L"gpu_timing", 0, ini.c_str()) != 0;
  captureProbe = GetPrivateProfileIntW(L"capture", L"native_motion_probe", 0,
                                       ini.c_str()) != 0;
  jitterProbe =
      GetPrivateProfileIntW(L"dlaa", L"jitter_probe", 0, ini.c_str()) != 0;
  base = reinterpret_cast<unsigned char *>(module);
  struct Hook {
    uintptr_t rva;
    void *detour;
    void **original;
  };
  Hook hooks[] = {
      {layout->guidesPrepare, (void *)HookPrepare, (void **)&prepare},
      {layout->guidesReady, (void *)HookReady, (void **)&ready},
      {layout->guidesDepth, (void *)HookDepth, (void **)&depth},
      {layout->guidesSize, (void *)HookSize, (void **)&size},
      {layout->eyeSetup, (void *)HookEyeSetup, (void **)&eyeSetup}};
  std::vector<void *> added;
  for (auto &h : hooks) {
    auto *address = base + h.rva;
    auto result = MH_CreateHook(address, h.detour, h.original);
    if (result == MH_OK) {
      added.push_back(address);
      result = MH_EnableHook(address);
    }
    if (result != MH_OK) {
      for (auto p : added) {
        MH_DisableHook(p);
        MH_RemoveHook(p);
      }
      Log("native_guides_hooks_failed rva=%lx status=%d", (unsigned long)h.rva,
          result);
      return false;
    }
  }
  enabled = true;
  Log("native_guides_probe_enabled scoped_upscaler_gate=1 native_resolution=1");
  return true;
}
bool NativeGuidesEnabled() {
  return enabled && !CurrentMsaaSamples() && (captureProbe || SceneProcessingEnabled());
}
void NativeGuidesFrame(uint64_t frame) {
  const bool camerasReady = previous[0].verified && !previous[0].rejected &&
                            previous[1].verified && !previous[1].rejected &&
                            previous[0].frame + 1 == frame &&
                            previous[1].frame + 1 == frame;
  applyJitter = jitterProbe || (DlaaJitterReady() && camerasReady);
  jitterX = applyJitter ? Halton(unsigned(frame % 8) + 1, 2) - .5f : 0;
  jitterY = applyJitter ? Halton(unsigned(frame % 8) + 1, 3) - .5f : 0;
}
bool GetTemporalGuides(uint64_t frame, unsigned eye, TemporalGuides &out) {
  if (eye > 1 || guideFrames[eye] != frame || !guides[eye].motion)
    return false;
  out = guides[eye];
  return true;
}
bool NativeJitter(float &x, float &y) {
  if (!NativeGuidesEnabled() || !applyJitter ||
      *reinterpret_cast<int *>(base + layout->activeEye) < 0 ||
      *(base + layout->motionRendering))
    return false;
  x = jitterX;
  y = jitterY;
  return true;
}
void NativeGuidesObserveScene(IDirect3DDevice9 *d, uint64_t frame) {
  if (!NativeGuidesEnabled())
    return;
  const int eye = *reinterpret_cast<int *>(base + layout->activeEye);
  if (eye < 0 || eye > 1)
    return;
  auto &camera = cameras[eye];
  if (camera.frame != frame) {
    camera = Camera{};
    camera.frame = frame;
  }
  const bool sample = profile && frame % 120 == 0;
  const auto start = sample ? std::chrono::steady_clock::now()
                            : std::chrono::steady_clock::time_point{};
  // Candidate layout from the MIT Remix-Wrappers FNV adapter. It is accepted
  // only after matching this draw's actual vertex-shader projection constants.
  uintptr_t renderer = 0;
  SIZE_T got = 0;
  float matrices[48], constants[16], origin[3];
  if (!ReadProcessMemory(GetCurrentProcess(),
                         reinterpret_cast<void *>(0x11c73b4), &renderer,
                         sizeof(renderer), &got) ||
      !renderer ||
      !ReadProcessMemory(GetCurrentProcess(),
                         reinterpret_cast<void *>(renderer + 0x940), matrices,
                         sizeof(matrices), &got) ||
      !ReadProcessMemory(GetCurrentProcess(),
                         reinterpret_cast<void *>(0x11f8e9c), origin,
                         sizeof(origin), &got) ||
      FAILED(d->GetVertexShaderConstantF(0, constants, 4)))
    return;
  for (float x : origin)
    if (!std::isfinite(x))
      return;
  float wv[16]{}, wvp[16]{};
  for (unsigned r = 0; r < 4; ++r)
    for (unsigned c = 0; c < 4; ++c)
      for (unsigned k = 0; k < 4; ++k)
        wv[r * 4 + c] += matrices[r * 4 + k] * matrices[16 + k * 4 + c];
  for (unsigned r = 0; r < 4; ++r)
    for (unsigned c = 0; c < 4; ++c)
      for (unsigned k = 0; k < 4; ++k)
        wvp[r * 4 + c] += wv[r * 4 + k] * matrices[32 + k * 4 + c];
  float scale = 1, error = 0;
  for (unsigned r = 0; r < 4; ++r)
    for (unsigned c = 0; c < 4; ++c) {
      const float expected = wvp[c * 4 + r], actual = constants[r * 4 + c];
      if (!std::isfinite(expected) || !std::isfinite(actual))
        return;
      scale = std::max(scale, std::abs(expected));
      error = std::max(error, std::abs(actual - expected));
    }
  error /= scale;
  if (error > 0.0002f) {
    ++camera.rejected;
    camera.error = error;
    return;
  }
  ++camera.verified;
  camera.error = error;
  std::memcpy(camera.view, matrices + 16, 64);
  std::memcpy(camera.projection, matrices + 32, 64);
  std::memcpy(camera.origin, origin, 12);
  // FNV renders camera-relative world transforms. Restore the candidate
  // absolute origin here; image reprojection, not the WVP check, validates it.
  for (unsigned c = 0; c < 4; ++c)
    for (unsigned k = 0; k < 3; ++k)
      camera.view[12 + c] -= origin[k] * matrices[16 + k * 4 + c];
  if (sample)
    camera.cpuMs += std::chrono::duration<double, std::milli>(
                        std::chrono::steady_clock::now() - start)
                        .count();
}
void NativeGuidesBeforeTone(IDirect3DDevice9 *d, uint64_t frame, unsigned eye,
                            bool capture) {
  if (!NativeGuidesEnabled() || eye > 1)
    return;
  ComPtr<IDirect3DSurface9> current;
  d->GetDepthStencilSurface(&current);
  D3DSURFACE_DESC currentDesc{};
  if (current && SUCCEEDED(current->GetDesc(&currentDesc)) &&
      currentDesc.Width == Width && currentDesc.Height == Height &&
      currentDesc.Format == D3DFMT_D24S8)
    sceneDepth = current;
  const int active = *reinterpret_cast<int *>(base + layout->activeEye);
  const bool sample = profile && frame % 120 == 0;
  const auto start = sample ? std::chrono::steady_clock::now()
                            : std::chrono::steady_clock::time_point{};
  if (active == int(eye))
    reinterpret_cast<void(__cdecl *)(IDirect3DDevice9 *)>(
        base + layout->motionSubmit)(d);
  if (sample)
    Log("guide_cpu_timing frame=%llu eye=%u camera_draws=%u camera_ms=%.3f "
        "motion_submit_ms=%.3f",
        (unsigned long long)frame, eye, cameras[eye].verified,
        cameras[eye].cpuMs,
        std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - start)
            .count());
  auto *surface = *reinterpret_cast<IDirect3DSurface9 **>(
      base + layout->motionSurfaces + eye * 4);
  if (capture && surface) {
    CaptureExtraSurface(d, surface,
                        CaptureDirectory() + L"\\motion-frame-" +
                            std::to_wstring(frame) + L"-eye-" +
                            std::to_wstring(eye) + L".dds");
  }
  auto &camera = cameras[eye];
  guideFrames[eye] = ~0ull;
  if (camera.frame == frame && camera.verified && !camera.rejected &&
      active == int(eye) && surface) {
    auto &g = guides[eye];
    g = TemporalGuides{};
    g.motion = surface;
    g.jitterX = jitterX;
    g.jitterY = jitterY;
    const auto now = GetTickCount64();
    g.reset = previous[eye].frame + 1 != frame || !previous[eye].verified ||
              now - lastToneTime[eye] > 250;
    for (unsigned i = 0; i < 3; ++i)
      if (std::abs(camera.origin[i] - previous[eye].origin[i]) > 64)
        g.reset = true;
    for (unsigned i = 0; i < 12; ++i)
      if (std::abs(camera.view[i] - previous[eye].view[i]) > .3f)
        g.reset = true;
    if (g.reset)
      for (unsigned i = 0; i < 4; ++i)
        g.clipToPrevious[i * 4 + i] = 1;
    if (g.reset ||
        ClipToPrevious(camera.view, camera.projection, previous[eye].view,
                       previous[eye].projection, g.clipToPrevious))
      guideFrames[eye] = frame;
    lastToneTime[eye] = now;
  }
  if (capture) {
    auto path = CaptureDirectory() + L"\\camera-frame-" +
                std::to_wstring(frame) + L"-eye-" + std::to_wstring(eye) +
                L".json";
    if (auto *out = _wfopen(path.c_str(), L"wb")) {
      fprintf(out,
              "{\"frame\":%llu,\"eye\":%u,\"verified_draws\":%u,\"rejected_"
              "draws\":%u,\"relative_error\":%.9g,\"view\":",
              (unsigned long long)frame, eye, camera.verified, camera.rejected,
              camera.error);
      MatrixJson(out, camera.view);
      fprintf(out, ",\"jitter\":[%.9g,%.9g]", jitterX, jitterY);
      fprintf(out, ",\"origin\":[%.9g,%.9g,%.9g]", camera.origin[0],
              camera.origin[1], camera.origin[2]);
      fputs(",\"projection\":", out);
      MatrixJson(out, camera.projection);
      fprintf(out, ",\"previous_frame\":%llu,\"previous_view\":",
              (unsigned long long)previous[eye].frame);
      MatrixJson(out, previous[eye].view);
      fputs(",\"previous_projection\":", out);
      MatrixJson(out, previous[eye].projection);
      fputs("}\n", out);
      fclose(out);
    }
  }
  if (capture || frame % 600 == 0) {
    Log("native_camera frame=%llu eye=%u verified=%u rejected=%u error=%.7g "
        "previous=%llu",
        (unsigned long long)frame, eye, camera.verified, camera.rejected,
        camera.error, (unsigned long long)previous[eye].frame);
    D3DSURFACE_DESC desc{};
    if (surface)
      surface->GetDesc(&desc);
    Log("native_guides_frame frame=%llu eye=%u active=%d prepared=%llu "
        "surface=%p format=%u size=%ux%u capture=%d",
        (unsigned long long)frame, eye, active, (unsigned long long)prepared,
        surface, unsigned(desc.Format), desc.Width, desc.Height, capture);
  }
  if (camera.verified)
    previous[eye] = camera;
}
} // namespace ttwvrnr
