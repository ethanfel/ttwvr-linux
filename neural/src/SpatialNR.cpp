// SPDX-License-Identifier: GPL-3.0-only
#include "SpatialNR.h"
#include "DepthTransfer.h"
#include "FrameDelivery.h"
#include "GpuTimeline.h"
#include "HostProtocol.h"
#include "MenuControl.h"
#include "MenuInput.h"
#include "NativeGuides.h"
#include "SceneCapture.h"
#include "SuperResolution.h"
#include "Support.h"
#include "VRSettings.h"
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstring>
#include <d3d11_4.h>
#include <memory>
#include <mutex>
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
std::recursive_mutex processingMutex;
ReloadState reload;
// Survives transport resizing. Timeline values must never restart when SR
// changes the shared image dimensions.
GpuTimeline *sharedTimeline = nullptr;
IDirect3DDevice9 *timelineDevice = nullptr;
uint64_t timelineSerial = 0;
bool timelineAttempted = false;
// D3D9 world rendering and D3D11 OpenXR copies have different Vulkan queues.
// Keep separate monotonic timelines: signaling a common counter from both
// queues could execute a lower value after a higher value.
GpuTimeline *sharedFinalTimeline = nullptr;
ID3D11Device *finalTimelineDevice = nullptr;
uint64_t finalSerial = 0;
struct ContextGuard {
  ComPtr<ID3D11Multithread> protection;
  explicit ContextGuard(ID3D11DeviceContext *context) {
    context->QueryInterface(IID_PPV_ARGS(&protection));
    if (protection)
      protection->Enter();
  }
  ~ContextGuard() {
    if (protection)
      protection->Leave();
  }
};
struct FinalImages {
  ComPtr<ID3D11Device> device;
  ComPtr<ID3D11DeviceContext> context;
  struct Eye {
    ComPtr<ID3D11Texture2D> color, real, generated;
    HANDLE colorHandle = nullptr, realHandle = nullptr,
           generatedHandle = nullptr;
    uint64_t frame = ~uint64_t(0);
    bool interpolated = false;
  } eyes[2];
  GpuTimeline *timeline = nullptr;
};
struct Client {
  ComPtr<IDirect3DDevice9> device;
  Texture color, depth, bloom, motion, output;
  std::shared_ptr<FinalImages> final;
  HANDLE mapping = nullptr, request = nullptr, reply = nullptr,
         process = nullptr;
  HostProtocol *p = nullptr;
  GpuTimeline *timeline = nullptr;
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
    const unsigned width = reload.requested.sr ? SRInputSize(reload.requested.sr).width : hdr.Width;
    const unsigned height = reload.requested.sr ? SRInputSize(reload.requested.sr).height : hdr.Height;
    if (reload.requested.sr)
      output.Init(d, OutputWidth, OutputHeight, D3DFMT_A16B16G16R16F);
    color.Init(d, width, height, D3DFMT_A16B16G16R16F);
    depth.Init(d, width, height, D3DFMT_A8B8G8R8);
    bloom.Init(d, b.Width, b.Height, D3DFMT_A16B16G16R16F);
    motion.Init(d, width, height, D3DFMT_A16B16G16R16F);
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
    p->width = width;
    p->height = height;
    p->sr = reload.requested.sr;
    p->fg = reload.requested.fg;
    p->outputWidth = OutputWidth;
    p->outputHeight = OutputHeight;
    p->outputHandle = reinterpret_cast<uintptr_t>(output.handle);
    p->colorHandle = reinterpret_cast<uintptr_t>(color.handle);
    p->depthHandle = reinterpret_cast<uintptr_t>(depth.handle);
    p->bloomHandle = reinterpret_cast<uintptr_t>(bloom.handle);
    p->parentPid = GetCurrentProcessId();
    p->motionHandle = reinterpret_cast<uintptr_t>(motion.handle);
    const auto ini = Directory() + L"\\TTWVRNeuralRendering.ini";
    if (p->fg ||
        GetPrivateProfileIntW(L"diagnostics", L"gpu_handoff", 0, ini.c_str())) {
      if (!timelineAttempted) {
        timelineAttempted = true;
        auto candidate = new GpuTimeline;
        try {
          candidate->Init(d);
          sharedTimeline = candidate;
          timelineDevice = d;
          Log("gpu_handoff_ready mode=opaque_timeline experimental=1");
        } catch (const std::exception &e) {
          Log("gpu_handoff_unavailable reason=%s fallback=cpu", e.what());
        }
      }
      if (sharedTimeline && timelineDevice != d)
        throw std::runtime_error("timeline device changed");
      timeline = sharedTimeline;
      if (timeline) {
        p->inputTimeline = timeline->InputHandle();
        p->outputTimeline = timeline->OutputHandle();
      }
    }
    request = CreateEventW(nullptr, FALSE, FALSE, (name + L"-request").c_str());
    reply = CreateEventW(nullptr, FALSE, FALSE, (name + L"-reply").c_str());
    if (!request || !reply)
      throw std::runtime_error("NR events");
  }
  void Start() {
    const auto &s = reload.requested;
    ResetEvent(request);
    ResetEvent(reply);
    p->fg = s.fg;
    p->neural = s.neural;
    final.reset();
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
    Log("spatial_host_start revision=%u native=%ux%u nr=%u dlaa=%u sr=%u "
        "model=%u "
        "scale=%u identity=%u",
        p->revision, p->width, p->height, p->neural, p->dlaa, p->sr, p->preset,
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
    if (reload.NeedStart() && p->sr == reload.requested.sr &&
        p->fg == reload.requested.fg)
      Start();
  }
};
Client *client = nullptr;
std::atomic<bool> enabled{false};
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
  std::lock_guard lock(processingMutex);
  return reload.CanApply() && client && client->p && !client->p->identity &&
         client->leftAppliedFrame == frame &&
         client->rightAppliedFrame == frame;
}
bool SpatialApplied(uint64_t frame) {
  std::lock_guard lock(processingMutex);
  return AllApplied(frame) && client->p->neural;
}
bool DlaaApplied(uint64_t frame) {
  std::lock_guard lock(processingMutex);
  return AllApplied(frame) && client->p->dlaa;
}
unsigned SrApplied(uint64_t frame) {
  std::lock_guard lock(processingMutex);
  return AllApplied(frame) ? client->p->sr : 0;
}
unsigned SuperResolutionReady() {
  std::lock_guard lock(processingMutex);
  return enabled && reload.CanApply() && client ? client->p->sr : 0;
}
bool TemporalNRApplied(uint64_t frame) {
  std::lock_guard lock(processingMutex);
  return SpatialApplied(frame) && client->p->temporal;
}
void ProcessingJitter(uint64_t frame, float &x, float &y) {
  std::lock_guard lock(processingMutex);
  if (AllApplied(frame)) {
    x = client->p->jitterX;
    y = client->p->jitterY;
  }
}
// This gate is sampled by every draw hook. Do not acquire a Windows mutex
// thousands of times per frame just to read a published on/off state.
bool SceneProcessingEnabled() {
  return enabled.load(std::memory_order_relaxed);
}
bool DlaaJitterReady() {
  std::lock_guard lock(processingMutex);
  return enabled && reload.CanApply() && client &&
         (client->p->dlaa || (client->p->sr && SuperResolutionLow()));
}
bool SpatialEnabled(uint64_t frame) {
  std::lock_guard lock(processingMutex);
  try {
    PollMenuInput();
    static uint64_t polled = 0;
    const auto now = GetTickCount64();
    if (!reload.revision || now - polled >= 250) {
      polled = now;
      auto settings = ReadLiveSettings();
      if (CurrentMsaaSamples())
        settings.neural = settings.dlaa = settings.sr = settings.fg = 0;
      bool changed = reload.Request(settings, !reload.revision);
      const bool retry =
          DeleteFileW((Directory() + L"\\neural.request").c_str()) != FALSE;
      if (ReadMenuRequest(reload.revision, settings) || retry) {
        if (CurrentMsaaSamples())
          settings.neural = settings.dlaa = settings.sr = settings.fg = 0;
        changed = reload.Request(settings, true);
      }
      if (changed)
        Log("live_settings_request revision=%u nr=%u dlaa=%u sr=%u model=%u "
            "scale=%u",
            reload.revision, settings.neural, settings.dlaa, settings.sr,
            settings.preset, settings.resolution);
    }
    if (client) {
      client->Advance();
      // The old helper must release its shared images before resizing
      // transport.
      if (!client->process && (client->p->sr != reload.requested.sr ||
                               client->p->fg != reload.requested.fg)) {
        delete client;
        client = nullptr;
      }
    }
  } catch (const std::exception &e) {
    reload.Fail();
    enabled = false;
    Log("spatial_bypass reason=%s original_hdr_retained=1", e.what());
  }
  enabled = reload.requested.Active() && !reload.fault &&
            reload.phase != ReloadState::Stopping;
  PublishMenuState(reload, frame && AllApplied(frame - 1));
  return enabled;
}
bool ApplySpatialNR(IDirect3DDevice9 *d, uint64_t frame, unsigned eye,
                    bool capture) {
  std::lock_guard lock(processingMutex);
  if (CurrentMsaaSamples() || reload.fault || !enabled || eye > 1)
    return false;
  try {
    auto hdr = Surface(d, 1), bloom = Surface(d, 0);
    ComPtr<IDirect3DSurface9> depth;
    if (SuperResolutionLow())
      depth = SuperResolutionDepth();
    else
      Check(d->GetDepthStencilSurface(&depth));
    if (!depth)
      return false;
    D3DSURFACE_DESC hd{}, bd{}, dd{};
    Check(hdr->GetDesc(&hd));
    Check(bloom->GetDesc(&bd));
    Check(depth->GetDesc(&dd));
    if (hd.Format != D3DFMT_A16B16G16R16F ||
        bd.Format != D3DFMT_A16B16G16R16F || dd.Format != D3DFMT_D24S8 ||
        hd.Width != (SuperResolutionLow() ? SuperResolutionWidth() : OutputWidth) ||
        hd.Height != (SuperResolutionLow() ? SuperResolutionHeight() : OutputHeight) ||
        dd.Width != hd.Width || dd.Height != hd.Height ||
        bd.Width != OutputWidth / 4 || bd.Height != OutputHeight / 4 ||
        dd.MultiSampleType != D3DMULTISAMPLE_NONE)
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
    if (!reload.CanApply() || (client->p->sr && !SuperResolutionLow()))
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
    if (!TransferDepthBits(d, depth.Get(), client->depth.surface.Get(),
                           !client->timeline))
      throw std::runtime_error("depth bit transport");
    const auto afterDepth = stamp();
    // The default path waits for the depth/copy fence. The optional timeline
    // path signals the same queue below; the helper waits on that signal
    // before decoding any shared inputs.
    const auto afterWait = afterDepth;
    auto *p = client->p;
    p->frame = frame;
    p->eye = eye;
    p->hdrParam = c1[0];
    p->reset = client->p->temporal ? guides.reset : 1;
    p->jitterX = guides.jitterX;
    p->jitterY = guides.jitterY;
    p->viewSpaceToMeters = guides.viewSpaceToMeters;
    std::memcpy(p->view, guides.view, sizeof(p->view));
    std::memcpy(p->projection, guides.projection, sizeof(p->projection));
    std::memcpy(p->clipToPrevious, guides.clipToPrevious,
                sizeof(p->clipToPrevious));
    if (client->timeline)
      client->timeline->SignalInput(p->serial = ++timelineSerial);
    p->command = 1;
    p->result = 0;
    if (!SetEvent(client->request))
      throw std::runtime_error("NR request signal");
    HANDLE waits[] = {client->process, client->reply};
    if (WaitForMultipleObjects(2, waits, FALSE, 5000) != WAIT_OBJECT_0 + 1 ||
        p->result != 1)
      throw std::runtime_error("NR request timeout/failure");
    if (client->timeline)
      client->timeline->WaitOutput(p->serial);
    const auto afterHelper = stamp();
    // This is the first write to the game's HDR. The CPU path has completed
    // the helper writes; the timeline path has enqueued a wait after the helper
    // confirmed submission of its output signal. Failure before that handoff
    // retains the original HDR. No unconfirmed future signal is ever waited on.
    if (p->sr) {
      if (!SuperResolutionBindOutput(d, client->output.texture.Get()))
        throw std::runtime_error("SR tone-map output binding");
    } else if (p->neural || p->dlaa) {
      Check(d->StretchRect(client->color.surface.Get(), nullptr, hdr.Get(),
                           nullptr, D3DTEXF_NONE));
    }
    if (capture)
      CaptureExtraSurface(d, p->sr ? client->output.surface.Get() : hdr.Get(),
                          capturePrefix + L"-output-hdr.dds");
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
          "identity=%u temporal=%u dlaa=%u sr=%u output=%ux%u",
          (unsigned long long)frame, eye, hd.Width, hd.Height,
          (unsigned long long)(GetTickCount64() - start), p->identity,
          p->temporal, p->dlaa, p->sr, p->outputWidth, p->outputHeight);
    return true;
  } catch (const std::exception &e) {
    reload.Fail();
    enabled = false;
    Log("spatial_bypass reason=%s original_hdr_retained=1", e.what());
    return false;
  }
}
void StopSceneProcessing() {
  std::lock_guard lock(processingMutex);
  enabled = false;
  // Called only after the presentation worker has joined, outside the DLL
  // loader lock and before the downstream session/device is destroyed.
  SuperResolutionFrame(false);
  if (client && client->process) {
    client->p->command = 2;
    if (!SetEvent(client->request) ||
        WaitForSingleObject(client->process, 5000) != WAIT_OBJECT_0) {
      reload.Fail();
      Log("scene_shutdown helper_exit=0 resources_retained=1");
      return;
    }
    CloseHandle(client->process);
    client->process = nullptr;
  }
  delete client;
  client = nullptr;
  reload = {};
  Log("scene_shutdown helper_exit=1");
}
bool FrameGenerationRequested() {
  std::lock_guard lock(processingMutex);
  return reload.requested.fg && !reload.fault && !CurrentMsaaSamples();
}
bool GenerateFrameEye(ID3D11Device *device, ID3D11Texture2D *source,
                      uint64_t frame, unsigned eye) {
  std::lock_guard lock(processingMutex);
  if (!FrameGenerationRequested() || !reload.CanApply() || !client ||
      !client->p->fg || eye > 1 || !source ||
      (eye == 0 ? client->leftAppliedFrame : client->rightAppliedFrame) !=
          frame)
    return false;
  try {
    auto *p = client->p;
    D3D11_TEXTURE2D_DESC desc{};
    source->GetDesc(&desc);
    if (desc.Width != OutputWidth || desc.Height != OutputHeight ||
        desc.ArraySize != 1 || desc.SampleDesc.Count != 1 ||
        desc.MipLevels != 1 ||
        (desc.Format != DXGI_FORMAT_B8G8R8A8_UNORM &&
         desc.Format != DXGI_FORMAT_B8G8R8A8_UNORM_SRGB &&
         desc.Format != DXGI_FORMAT_B8G8R8A8_TYPELESS))
      return false;
    if (!client->final) {
      auto f = std::make_shared<FinalImages>();
      f->device = device;
      device->GetImmediateContext(&f->context);
      if (!sharedFinalTimeline) {
        auto *t = new GpuTimeline;
        t->Init(device, 0, 0, 0);
        sharedFinalTimeline = t;
        finalTimelineDevice = device;
      }
      if (finalTimelineDevice != device)
        throw std::runtime_error("FG device changed");
      f->timeline = sharedFinalTimeline;
      for (auto &e : f->eyes) {
        auto create = [&](DXGI_FORMAT format, ComPtr<ID3D11Texture2D> &texture,
                          HANDLE &handle) {
          D3D11_TEXTURE2D_DESC td{};
          td.Width = OutputWidth;
          td.Height = OutputHeight;
          td.MipLevels = td.ArraySize = td.SampleDesc.Count = 1;
          td.Format = format;
          td.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
          td.MiscFlags = D3D11_RESOURCE_MISC_SHARED;
          Check(device->CreateTexture2D(&td, nullptr, &texture));
          ComPtr<IDXGIResource> resource;
          Check(texture.As(&resource));
          Check(resource->GetSharedHandle(&handle));
        };
        create(DXGI_FORMAT_B8G8R8A8_UNORM, e.color, e.colorHandle);
        create(DXGI_FORMAT_R8G8B8A8_UNORM, e.real, e.realHandle);
        create(DXGI_FORMAT_R8G8B8A8_UNORM, e.generated, e.generatedHandle);
      }
      client->final = std::move(f);
      Log("fg_transport_ready size=%ux%u separate_d3d11_timeline=1",
          OutputWidth, OutputHeight);
    }
    auto &f = *client->final;
    auto &e = f.eyes[eye];
    if (f.device.Get() != device)
      throw std::runtime_error("FG source device changed");
    e.frame = ~uint64_t(0);
    e.interpolated = false;
    {
      ContextGuard guard(f.context.Get());
      f.context->CopyResource(e.color.Get(), source);
      p->fgColorHandle = uintptr_t(e.colorHandle);
      p->fgRealHandle = uintptr_t(e.realHandle);
      p->fgGeneratedHandle = uintptr_t(e.generatedHandle);
      p->fgInputTimeline = f.timeline->InputHandle();
      p->fgOutputTimeline = f.timeline->OutputHandle();
      p->frame = frame;
      p->eye = eye;
      p->command = 3;
      p->result = 0;
      f.timeline->SignalInput(p->fgSerial = ++finalSerial, true);
    }
    if (!SetEvent(client->request))
      throw std::runtime_error("FG request signal");
    HANDLE waits[] = {client->process, client->reply};
    if (WaitForMultipleObjects(2, waits, FALSE, 5000) != WAIT_OBJECT_0 + 1 ||
        p->result != 1)
      throw std::runtime_error("FG request timeout/failure");
    {
      ContextGuard guard(f.context.Get());
      f.timeline->WaitOutput(p->fgSerial);
    }
    e.frame = frame;
    e.interpolated = p->fgInterpolated;
    return true;
  } catch (const std::exception &e) {
    reload.Fail();
    enabled = false;
    Log("fg_bypass reason=%s original_frame_retained=1", e.what());
    return false;
  }
}
StereoFrameTextures GeneratedFrame(uint64_t frame) {
  std::lock_guard lock(processingMutex);
  StereoFrameTextures result;
  if (!FrameGenerationRequested() || !reload.CanApply() || !client ||
      !client->final)
    return result;
  auto f = client->final;
  if (f->eyes[0].frame != frame || f->eyes[1].frame != frame)
    return result;
  result.frame = frame;
  result.owner = f;
  result.interpolated = f->eyes[0].interpolated && f->eyes[1].interpolated;
  for (unsigned e = 0; e < 2; ++e) {
    result.real[e] = f->eyes[e].real.Get();
    result.generated[e] = f->eyes[e].generated.Get();
    result.format[e] = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
  }
  return result;
}
} // namespace ttwvrnr
