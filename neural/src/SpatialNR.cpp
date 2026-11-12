// SPDX-License-Identifier: GPL-3.0-only
#include "SpatialNR.h"
#include "DepthTransfer.h"
#include "HostProtocol.h"
#include "MenuControl.h"
#include "MenuInput.h"
#include "NativeGuides.h"
#include "SceneCapture.h"
#include "Support.h"
#include "VRSettings.h"
#include <chrono>
#include <cmath>
#include <cstring>
#include <memory>
#include <stdexcept>
#include <wrl/client.h>
namespace ttwvrnr {
namespace {
using Microsoft::WRL::ComPtr;
void Check(HRESULT hr) {
  if (FAILED(hr))
    throw std::runtime_error("D3D9 spatial transport");
}
struct Texture {
  ComPtr<IDirect3DTexture9> texture;
  ComPtr<IDirect3DSurface9> surface;
  HANDLE handle = nullptr;
  void Init(IDirect3DDevice9 *d, unsigned w, unsigned h, D3DFORMAT format) {
    Check(d->CreateTexture(w, h, 1, D3DUSAGE_RENDERTARGET, format,
                           D3DPOOL_DEFAULT, &texture, &handle));
    Check(texture->GetSurfaceLevel(0, &surface));
  }
};
ReloadState reload;
struct Client {
  ComPtr<IDirect3DDevice9> device;
  Texture color, depth, bloom, motion;
  HANDLE mapping = nullptr, request = nullptr, reply = nullptr,
         process = nullptr;
  HostProtocol *p = nullptr;
  bool profile = false, stopSent = false;
  std::wstring name;
  uint64_t started = 0, leftAppliedFrame = ~uint64_t(0),
           rightAppliedFrame = ~uint64_t(0);
  ~Client() {
    if (p)
      UnmapViewOfFile(p);
    for (HANDLE h : {mapping, request, reply, process})
      if (h)
        CloseHandle(h);
  }
  void Init(IDirect3DDevice9 *d, const D3DSURFACE_DESC &hdr,
            const D3DSURFACE_DESC &b) {
    device = d;
    color.Init(d, hdr.Width, hdr.Height, D3DFMT_A16B16G16R16F);
    depth.Init(d, hdr.Width, hdr.Height, D3DFMT_A8B8G8R8);
    bloom.Init(d, b.Width, b.Height, D3DFMT_A16B16G16R16F);
    motion.Init(d, hdr.Width, hdr.Height, D3DFMT_A16B16G16R16F);
    name = L"Local\\TTWVRNR-" + std::to_wstring(GetCurrentProcessId()) + L"-" +
           std::to_wstring(GetTickCount64());
    mapping = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE,
                                 0, sizeof(HostProtocol), name.c_str());
    if (!mapping || GetLastError() == ERROR_ALREADY_EXISTS)
      throw std::runtime_error("NR mapping");
    p = static_cast<HostProtocol *>(MapViewOfFile(mapping, FILE_MAP_ALL_ACCESS,
                                                  0, 0, sizeof(HostProtocol)));
    if (!p)
      throw std::runtime_error("NR mapping view");
    new (p) HostProtocol;
    p->width = hdr.Width;
    p->height = hdr.Height;
    p->colorHandle = reinterpret_cast<uintptr_t>(color.handle);
    p->depthHandle = reinterpret_cast<uintptr_t>(depth.handle);
    p->bloomHandle = reinterpret_cast<uintptr_t>(bloom.handle);
    p->parentPid = GetCurrentProcessId();
    p->motionHandle = reinterpret_cast<uintptr_t>(motion.handle);
    request = CreateEventW(nullptr, FALSE, FALSE, (name + L"-request").c_str());
    reply = CreateEventW(nullptr, FALSE, FALSE, (name + L"-reply").c_str());
    if (!request || !reply)
      throw std::runtime_error("NR events");
  }
  void Start() {
    const auto &s = reload.requested;
    ResetEvent(request);
    ResetEvent(reply);
    p->neural = s.neural;
    p->dlaa = s.dlaa;
    p->temporal = s.temporal;
    p->identity = s.identity;
    p->profile = s.profile;
    p->revision = reload.revision;
    p->resolution = s.resolution;
    p->preset = s.preset;
    p->strength = s.strength;
    p->tone = s.tone;
    p->detail = s.detail;
    p->command = p->result = 0;
    p->reset = 1;
    profile = s.profile;
    stopSent = false;
    leftAppliedFrame = rightAppliedFrame = ~uint64_t(0);
    const auto host = Directory() + L"\\neural_host.exe";
    auto command = L"\"" + host + L"\" \"" + name + L"\"";
    STARTUPINFOW si{};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};
    if (!CreateProcessW(host.c_str(), command.data(), nullptr, nullptr, FALSE,
                        CREATE_NO_WINDOW, nullptr, Directory().c_str(), &si,
                        &pi))
      throw std::runtime_error("NR helper launch");
    process = pi.hProcess;
    CloseHandle(pi.hThread);
    started = GetTickCount64();
    reload.Started();
    Log("spatial_host_start revision=%u native=%ux%u nr=%u dlaa=%u model=%u "
        "scale=%u identity=%u",
        p->revision, p->width, p->height, p->neural, p->dlaa, p->preset,
        p->resolution, p->identity);
  }
  // This method runs before either eye. Never enable DLAA halfway through a
  // stereo frame: the same ready snapshot must govern jitter and evaluation.
  void Advance() {
    if (process && WaitForSingleObject(process, 0) == WAIT_OBJECT_0) {
      CloseHandle(process);
      process = nullptr;
      reload.Exited();
      Log("spatial_host_exit revision=%u expected=%u", p->revision,
          !reload.fault);
    }
    if (reload.NeedStop() && !stopSent) {
      // No normal evaluation is in flight here. Following a timeout the host
      // owns only private images; retain them until its process exits.
      p->command = 2;
      if (!SetEvent(request))
        throw std::runtime_error("NR helper stop signal");
      stopSent = true;
      started = GetTickCount64();
      Log("spatial_host_stop revision=%u next_revision=%u", p->revision,
          reload.revision);
    }
    if (reload.phase == ReloadState::Starting) {
      if (WaitForSingleObject(reply, 0) == WAIT_OBJECT_0) {
        if (p->result != 1)
          throw std::runtime_error("NR helper initialization");
        reload.Initialized();
        Log("live_settings_ready revision=%u", p->revision);
      } else if (GetTickCount64() - started > 120000)
        throw std::runtime_error("NR helper startup timeout");
    }
    if (reload.phase == ReloadState::Stopping &&
        GetTickCount64() - started > 130000)
      throw std::runtime_error("NR helper stop timeout; restart withheld");
    if (reload.NeedStart())
      Start();
  }
};
Client *client = nullptr;
bool enabled = false;
ComPtr<IDirect3DSurface9> Surface(IDirect3DDevice9 *d, unsigned slot) {
  ComPtr<IDirect3DBaseTexture9> base;
  Check(d->GetTexture(slot, &base));
  ComPtr<IDirect3DTexture9> t;
  if (!base)
    throw std::runtime_error("missing scene texture");
  Check(base.As(&t));
  ComPtr<IDirect3DSurface9> surface;
  Check(t->GetSurfaceLevel(0, &surface));
  return surface;
}
} // namespace
bool AllApplied(uint64_t frame) {
  return reload.CanApply() && client && client->p && !client->p->identity &&
         client->leftAppliedFrame == frame &&
         client->rightAppliedFrame == frame;
}
bool SpatialApplied(uint64_t frame) {
  return AllApplied(frame) && client->p->neural;
}
bool DlaaApplied(uint64_t frame) {
  return AllApplied(frame) && client->p->dlaa;
}
bool TemporalNRApplied(uint64_t frame) {
  return SpatialApplied(frame) && client->p->temporal;
}
void ProcessingJitter(uint64_t frame, float &x, float &y) {
  if (AllApplied(frame)) {
    x = client->p->jitterX;
    y = client->p->jitterY;
  }
}
bool SceneProcessingEnabled() { return enabled && !reload.fault; }
bool DlaaJitterReady() {
  return enabled && reload.CanApply() && client && client->p->dlaa;
}
bool SpatialEnabled(uint64_t frame) {
  try {
    PollMenuInput();
    static uint64_t polled = 0;
    const auto now = GetTickCount64();
    if (!reload.revision || now - polled >= 250) {
      polled = now;
      auto settings = ReadLiveSettings();
      if (CurrentMsaaSamples()) settings.neural = settings.dlaa = 0;
      bool changed = reload.Request(settings, !reload.revision);
      const bool retry =
          DeleteFileW((Directory() + L"\\neural.request").c_str()) != FALSE;
      if (ReadMenuRequest(reload.revision, settings) || retry) {
        if (CurrentMsaaSamples()) settings.neural = settings.dlaa = 0;
        changed = reload.Request(settings, true);
      }
      if (changed)
        Log("live_settings_request revision=%u nr=%u dlaa=%u model=%u scale=%u",
            reload.revision, settings.neural, settings.dlaa, settings.preset,
            settings.resolution);
    }
    if (client)
      client->Advance();
  } catch (const std::exception &e) {
    reload.Fail();
    Log("spatial_bypass reason=%s original_hdr_retained=1", e.what());
  }
  enabled = reload.requested.Active() && !reload.fault &&
            reload.phase != ReloadState::Stopping;
  PublishMenuState(reload, frame && AllApplied(frame - 1));
  return enabled;
}
bool ApplySpatialNR(IDirect3DDevice9 *d, uint64_t frame, unsigned eye,
                    bool capture) {
  if (CurrentMsaaSamples() || reload.fault || !enabled || eye > 1)
    return false;
  try {
    auto hdr = Surface(d, 1), bloom = Surface(d, 0);
    ComPtr<IDirect3DSurface9> depth;
    Check(d->GetDepthStencilSurface(&depth));
    if (!depth)
      return false;
    D3DSURFACE_DESC hd{}, bd{}, dd{};
    Check(hdr->GetDesc(&hd));
    Check(bloom->GetDesc(&bd));
    Check(depth->GetDesc(&dd));
    if (hd.Format != D3DFMT_A16B16G16R16F ||
        bd.Format != D3DFMT_A16B16G16R16F || dd.Format != D3DFMT_D24S8 ||
        hd.Width != 2944 || hd.Height != 2208 || dd.Width != hd.Width ||
        dd.Height != hd.Height || bd.Width != hd.Width / 4 ||
        bd.Height != hd.Height / 4 || dd.MultiSampleType != D3DMULTISAMPLE_NONE)
      throw std::runtime_error("unverified scene layout");
    DWORD srgb = 0;
    Check(d->GetSamplerState(1, D3DSAMP_SRGBTEXTURE, &srgb));
    if (srgb)
      throw std::runtime_error("unverified HDR sRGB state");
    float c1[4]{};
    Check(d->GetPixelShaderConstantF(1, c1, 1));
    if (!std::isfinite(c1[0]) || c1[0] <= 0)
      throw std::runtime_error("unverified HDR exposure parameter");
    if (!client) {
      auto fresh = std::make_unique<Client>();
      fresh->Init(d, hd, bd);
      client = fresh.release();
      client->Start();
      return false;
    }
    if (!reload.CanApply())
      return false;
    if (eye == 1 && client->leftAppliedFrame != frame)
      return false;
    if (client->device.Get() != d)
      throw std::runtime_error("scene device changed");
    TemporalGuides guides;
    if (client->p->temporal && !GetTemporalGuides(frame, eye, guides))
      return false;
    const auto start = GetTickCount64();
    const bool sample = client->profile && frame % 120 == 0;
    auto stamp = [&] {
      return sample ? std::chrono::steady_clock::now()
                    : std::chrono::steady_clock::time_point{};
    };
    // Subtract integer ticks before converting. The game uses x87 single
    // precision, which would quantize absolute uptime milliseconds.
    auto elapsed = [](auto a, auto b) {
      return std::chrono::duration<double, std::milli>(b - a).count();
    };
    const auto beforeCopies = stamp();
    const auto capturePrefix = CaptureDirectory() + L"\\feature-frame-" +
                               std::to_wstring(frame) + L"-eye-" +
                               std::to_wstring(eye);
    if (capture)
      CaptureExtraSurface(d, hdr.Get(), capturePrefix + L"-input-hdr.dds");
    if (client->p->temporal)
      Check(d->StretchRect(guides.motion, nullptr, client->motion.surface.Get(),
                           nullptr, D3DTEXF_NONE));
    Check(d->StretchRect(hdr.Get(), nullptr, client->color.surface.Get(),
                         nullptr, D3DTEXF_NONE));
    Check(d->StretchRect(bloom.Get(), nullptr, client->bloom.surface.Get(),
                         nullptr, D3DTEXF_NONE));
    const auto beforeDepth = stamp();
    if (!TransferDepthBits(d, depth.Get(), client->depth.surface.Get()))
      throw std::runtime_error("depth bit transport");
    const auto afterDepth = stamp();
    // TransferDepthBits flushes D3D9's CS/submission queues and waits on a
    // same-queue Vulkan fence after all earlier color/motion/bloom copies.
    // A second D3D9 event query would submit and wait again without new work.
    const auto afterWait = afterDepth;
    auto *p = client->p;
    p->frame = frame;
    p->eye = eye;
    p->hdrParam = c1[0];
    p->reset = client->p->temporal ? guides.reset : 1;
    p->jitterX = guides.jitterX;
    p->jitterY = guides.jitterY;
    std::memcpy(p->clipToPrevious, guides.clipToPrevious,
                sizeof(p->clipToPrevious));
    p->command = 1;
    p->result = 0;
    if (!SetEvent(client->request))
      throw std::runtime_error("NR request signal");
    HANDLE waits[] = {client->process, client->reply};
    if (WaitForMultipleObjects(2, waits, FALSE, 5000) != WAIT_OBJECT_0 + 1 ||
        p->result != 1)
      throw std::runtime_error("NR request timeout/failure");
    const auto afterHelper = stamp();
    // This is the first write to the game's HDR. Failure before this point
    // leaves it untouched. The helper owns only private copies, including on
    // timeout.
    Check(d->StretchRect(client->color.surface.Get(), nullptr, hdr.Get(),
                         nullptr, D3DTEXF_NONE));
    if (capture)
      CaptureExtraSurface(d, hdr.Get(), capturePrefix + L"-output-hdr.dds");
    if (eye == 0)
      client->leftAppliedFrame = frame;
    else
      client->rightAppliedFrame = frame;
    if (sample)
      Log("handoff_timing frame=%llu eye=%u copies_ms=%.3f depth_ms=%.3f "
          "d3d9_wait_ms=%.3f helper_wait_ms=%.3f output_ms=%.3f",
          (unsigned long long)frame, eye, elapsed(beforeCopies, beforeDepth),
          elapsed(beforeDepth, afterDepth), elapsed(afterDepth, afterWait),
          elapsed(afterWait, afterHelper), elapsed(afterHelper, stamp()));
    if (frame % 120 == 0)
      Log("spatial_output_applied frame=%llu eye=%u native=%ux%u wall_ms=%llu "
          "identity=%u temporal=%u dlaa=%u",
          (unsigned long long)frame, eye, hd.Width, hd.Height,
          (unsigned long long)(GetTickCount64() - start), p->identity,
          p->temporal, p->dlaa);
    return true;
  } catch (const std::exception &e) {
    reload.Fail();
    Log("spatial_bypass reason=%s original_hdr_retained=1", e.what());
    return false;
  }
}
} // namespace ttwvrnr
