// SPDX-License-Identifier: GPL-3.0-only
// TTWVR discovery only. No resource is classified as a neural input by its
// slot.
#include "D3D9Trace.h"
#include "NativeGuides.h"
#include "SceneCapture.h"
#include "SceneJitter.h"
#include "SpatialNR.h"
#include "SuperResolution.h"
#include "Support.h"
#include <MinHook.h>
#include <array>
#include <atomic>
#include <bcrypt.h>
#include <cstdio>
#include <cstring>
#include <d3d9.h>
#include <map>
#include <mutex>
#include <sstream>
#include <vector>
#include <wrl/client.h>
namespace ttwvrnr {
namespace {
using Microsoft::WRL::ComPtr;
using SetRT = HRESULT(STDMETHODCALLTYPE *)(IDirect3DDevice9 *, DWORD,
                                           IDirect3DSurface9 *);
using Clear = HRESULT(STDMETHODCALLTYPE *)(IDirect3DDevice9 *, DWORD,
                                           const D3DRECT *, DWORD, D3DCOLOR,
                                           float, DWORD);
using Draw = HRESULT(STDMETHODCALLTYPE *)(IDirect3DDevice9 *, D3DPRIMITIVETYPE,
                                          UINT, UINT);
using DrawIndexed = HRESULT(STDMETHODCALLTYPE *)(IDirect3DDevice9 *,
                                                 D3DPRIMITIVETYPE, INT, UINT,
                                                 UINT, UINT, UINT);
using DrawUP = HRESULT(STDMETHODCALLTYPE *)(IDirect3DDevice9 *,
                                            D3DPRIMITIVETYPE, UINT,
                                            const void *, UINT);
using DrawIndexedUP = HRESULT(STDMETHODCALLTYPE *)(IDirect3DDevice9 *,
                                                   D3DPRIMITIVETYPE, UINT, UINT,
                                                   UINT, const void *,
                                                   D3DFORMAT, const void *,
                                                   UINT);
using Stretch = HRESULT(STDMETHODCALLTYPE *)(IDirect3DDevice9 *,
                                             IDirect3DSurface9 *, const RECT *,
                                             IDirect3DSurface9 *, const RECT *,
                                             D3DTEXTUREFILTERTYPE);
SetRT setRT;
Clear clear;
Draw draw;
DrawIndexed indexed;
DrawUP up;
DrawIndexedUP indexedUp;
Stretch stretch;
std::atomic<bool> armed{false};
std::mutex mutex;
uint64_t frame = 0, ordinal = 0;
unsigned dropped = 0;
std::vector<std::string> events;
std::map<uintptr_t, std::string> shaders;
std::vector<ComPtr<IUnknown>> retainedShaders;
thread_local bool observing = false;
thread_local int pendingTone = -1;
unsigned tonePass = 0;
bool captureInputs = false;
std::atomic<bool> spatial{false};
std::string Surface(IDirect3DSurface9 *p) {
  if (!p)
    return "null";
  D3DSURFACE_DESC d{};
  if (FAILED(p->GetDesc(&d)))
    return "null";
  ComPtr<IDirect3DTexture9> container;
  p->GetContainer(IID_PPV_ARGS(&container));
  std::ostringstream s;
  s << "{\"id\":" << reinterpret_cast<uintptr_t>(p)
    << ",\"texture\":" << reinterpret_cast<uintptr_t>(container.Get())
    << ",\"width\":" << d.Width << ",\"height\":" << d.Height
    << ",\"format\":" << unsigned(d.Format)
    << ",\"samples\":" << d.MultiSampleType << ",\"usage\":" << d.Usage << '}';
  return s.str();
}
template <class T> std::string Shader(T *p, const char *stage) {
  if (!p)
    return "null";
  const auto id = reinterpret_cast<uintptr_t>(p);
  const auto it = shaders.find(id);
  if (it != shaders.end())
    return it->second;
  UINT size = 0;
  if (FAILED(p->GetFunction(nullptr, &size)) || size > 1024 * 1024)
    return "null";
  std::vector<unsigned char> bytes(size);
  if (FAILED(p->GetFunction(bytes.data(), &size)))
    return "null";
  uint64_t hash = 14695981039346656037ull;
  for (auto b : bytes) {
    hash ^= b;
    hash *= 1099511628211ull;
  }
  const auto name = std::string(stage) + "-" + std::to_string(hash);
  const auto path = CaptureDirectory() + L"\\" +
                    std::wstring(name.begin(), name.end()) + L".bin";
  if (FILE *out = _wfopen(path.c_str(), L"wb")) {
    fwrite(bytes.data(), 1, size, out);
    fclose(out);
  }
  const auto value = "\"" + name + "\"";
  retainedShaders.emplace_back(
      p); // Prevent pointer reuse after shader destruction.
  shaders[id] = value;
  return value;
}
void Observe(IDirect3DDevice9 *d, const char *op,
             IDirect3DSurface9 *extra = nullptr, unsigned arg = 0) {
  if ((!armed.load(std::memory_order_relaxed) &&
       !spatial.load(std::memory_order_relaxed) && !NativeGuidesEnabled()) ||
      observing)
    return;
  if (!armed.load(std::memory_order_relaxed) && std::strcmp(op, "draw_indexed"))
    return;
  observing = true;
  try {
    std::lock_guard lock(mutex);
    ++ordinal;
    if (events.size() >= 12000) {
      ++dropped;
      observing = false;
      return;
    }
    ComPtr<IDirect3DSurface9> rt, ds;
    d->GetRenderTarget(0, &rt);
    d->GetDepthStencilSurface(&ds);
    ComPtr<IDirect3DPixelShader9> ps;
    ComPtr<IDirect3DVertexShader9> vs;
    d->GetPixelShader(&ps);
    d->GetVertexShader(&vs);
    const auto pixelName = Shader(ps.Get(), "ps");
    if (NativeGuidesEnabled() && !std::strcmp(op, "draw_indexed") &&
        Shader(vs.Get(), "vs") == "\"vs-6149896215606588438\"") {
      D3DSURFACE_DESC scene{};
      if (rt && SUCCEEDED(rt->GetDesc(&scene)) && scene.Width == (SuperResolutionLow()?InputWidth:OutputWidth) &&
          scene.Height == (SuperResolutionLow()?InputHeight:OutputHeight) && scene.Format == D3DFMT_A16B16G16R16F)
        NativeGuidesObserveScene(d, frame);
    }
    if ((captureInputs || spatial.load(std::memory_order_relaxed) ||
         NativeGuidesEnabled()) &&
        !std::strcmp(op, "draw_indexed") &&
        pixelName == "\"ps-18030291181409727554\"" && tonePass < 2) {
      D3DSURFACE_DESC desc{};
      if (rt)
        rt->GetDesc(&desc);
      if (desc.Format == D3DFMT_X8R8G8B8 && desc.Width == 2944 &&
          desc.Height == 2208) {
        const auto selectedTone = tonePass++;
        SuperResolutionObserveTone(d);
        NativeGuidesBeforeTone(d, frame, selectedTone, captureInputs);
        if (spatial.load(std::memory_order_relaxed))
          ApplySpatialNR(d, frame, selectedTone, captureInputs);
        if (captureInputs)
          CaptureSceneBeforeTone(d, frame, selectedTone);
        pendingTone = static_cast<int>(selectedTone);
      }
    }
    if (!armed.load(std::memory_order_relaxed)) {
      observing = false;
      return;
    }
    D3DVIEWPORT9 viewport{};
    d->GetViewport(&viewport);
    std::ostringstream s;
    s << "{\"ordinal\":" << ordinal << ",\"operation\":\"" << op
      << "\",\"device\":" << reinterpret_cast<uintptr_t>(d)
      << ",\"argument\":" << arg << ",\"target\":" << Surface(rt.Get())
      << ",\"depth\":" << Surface(ds.Get()) << ",\"extra\":" << Surface(extra)
      << ",\"pixel_shader\":" << Shader(ps.Get(), "ps")
      << ",\"vertex_shader\":" << Shader(vs.Get(), "vs") << ",\"viewport\":["
      << viewport.X << ',' << viewport.Y << ',' << viewport.Width << ','
      << viewport.Height << ',' << viewport.MinZ << ',' << viewport.MaxZ
      << "],\"textures\":[";
    bool comma = false;
    for (unsigned slot = 0; slot < 16; ++slot) {
      ComPtr<IDirect3DBaseTexture9> base;
      d->GetTexture(slot, &base);
      if (!base || base->GetType() != D3DRTYPE_TEXTURE)
        continue;
      ComPtr<IDirect3DTexture9> t;
      if (FAILED(base.As(&t)))
        continue;
      ComPtr<IDirect3DSurface9> surface;
      t->GetSurfaceLevel(0, &surface);
      if (comma)
        s << ',';
      comma = true;
      s << "{\"slot\":" << slot << ",\"surface\":" << Surface(surface.Get())
        << '}';
    }
    s << "]}";
    events.push_back(s.str());
  } catch (...) {
    Log("d3d9_trace_failure bypass=1");
    armed = false;
  }
  observing = false;
}
HRESULT STDMETHODCALLTYPE HookSetRT(IDirect3DDevice9 *d, DWORD n,
                                    IDirect3DSurface9 *p) {
  Observe(d, "before_set_target", p, n);
  return SuperResolutionSetTarget(d, n, p, setRT);
}
HRESULT STDMETHODCALLTYPE HookClear(IDirect3DDevice9 *d, DWORD n,
                                    const D3DRECT *r, DWORD flags,
                                    D3DCOLOR color, float z, DWORD stencil) {
  Observe(d, "before_clear", nullptr, flags);
  return clear(d, n, r, flags, color, z, stencil);
}
HRESULT STDMETHODCALLTYPE HookDraw(IDirect3DDevice9 *d, D3DPRIMITIVETYPE t,
                                   UINT start, UINT count) {
  Observe(d, "draw", nullptr, count);
  SceneJitter jitter(d, observing);
  return draw(d, t, start, count);
}
HRESULT STDMETHODCALLTYPE HookIndexed(IDirect3DDevice9 *d, D3DPRIMITIVETYPE t,
                                      INT b, UINT min, UINT n, UINT start,
                                      UINT count) {
  pendingTone = -1;
  Observe(d, "draw_indexed", nullptr, count);
  const auto selected = pendingTone;
  pendingTone = -1;
  SceneJitter jitter(d, observing);
  const auto result = indexed(d, t, b, min, n, start, count);
  if (selected >= 0) SuperResolutionRestoreTone(d);
  if (captureInputs && selected >= 0 && SUCCEEDED(result)) {
    observing = true;
    CaptureSceneAfterTone(d, frame, static_cast<unsigned>(selected));
    observing = false;
  }
  return result;
}
HRESULT STDMETHODCALLTYPE HookUP(IDirect3DDevice9 *d, D3DPRIMITIVETYPE t,
                                 UINT n, const void *p, UINT stride) {
  Observe(d, "draw_up", nullptr, n);
  SceneJitter jitter(d, observing);
  return up(d, t, n, p, stride);
}
HRESULT STDMETHODCALLTYPE HookIndexedUP(IDirect3DDevice9 *d, D3DPRIMITIVETYPE t,
                                        UINT min, UINT n, UINT count,
                                        const void *idx, D3DFORMAT f,
                                        const void *v, UINT stride) {
  Observe(d, "draw_indexed_up", nullptr, count);
  SceneJitter jitter(d, observing);
  return indexedUp(d, t, min, n, count, idx, f, v, stride);
}
HRESULT STDMETHODCALLTYPE HookStretch(IDirect3DDevice9 *d,
                                      IDirect3DSurface9 *src, const RECT *a,
                                      IDirect3DSurface9 *dst, const RECT *b,
                                      D3DTEXTUREFILTERTYPE f) {
  Observe(d, "stretch_source", src);
  Observe(d, "stretch_destination", dst);
  return stretch(d, src, a, dst, b, f);
}
} // namespace
bool InstallD3D9Trace() {
  static bool attempted = false, ready = false;
  if (attempted)
    return ready;
  attempted = true;
  const auto mh = MH_Initialize();
  if (mh != MH_OK && mh != MH_ERROR_ALREADY_INITIALIZED)
    return false;
  // Read-only accessor verified in the supplied TTWVR 1.1.0, never another
  // build. Its getter is MOV EAX,[module+0x19710e8]; RET. Creating another D3D9
  // device overwrites this TTWVR global and leaves the mod pointing at a dead
  // device.
  auto module = GetModuleHandleW(L"TTWVR.dll");
  if (!module)
    return false;
  wchar_t path[32768]{};
  if (!GetModuleFileNameW(module, path, 32768))
    return false;
  HANDLE file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, nullptr,
                            OPEN_EXISTING, 0, nullptr);
  if (file == INVALID_HANDLE_VALUE)
    return false;
  BCRYPT_ALG_HANDLE algorithm = nullptr;
  BCRYPT_HASH_HANDLE hash = nullptr;
  bool ok = BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM,
                                        nullptr, 0) >= 0;
  if (ok)
    ok = BCryptCreateHash(algorithm, &hash, nullptr, 0, nullptr, 0, 0) >= 0;
  std::array<unsigned char, 65536> bytes{};
  DWORD read = 0;
  while (ok) {
    if (!ReadFile(file, bytes.data(), bytes.size(), &read, nullptr)) {
      ok = false;
      break;
    }
    if (!read)
      break;
    ok = BCryptHashData(hash, bytes.data(), read, 0) >= 0;
  }
  std::array<unsigned char, 32> digest{};
  if (ok)
    ok = BCryptFinishHash(hash, digest.data(), digest.size(), 0) >= 0;
  if (hash)
    BCryptDestroyHash(hash);
  if (algorithm)
    BCryptCloseAlgorithmProvider(algorithm, 0);
  CloseHandle(file);
  // Keep the readable expected value in the diagnostic as well.
  const char *expectedHex =
      "8a3a71b48d362984fe346c73b68dfbd476d6e138d6831f26f10c6f506cd03880";
  char actual[65]{};
  for (unsigned i = 0; i < 32; ++i)
    std::snprintf(actual + 2 * i, 3, "%02x", digest[i]);
  if (!ok || std::strcmp(actual, expectedHex)) {
    Log("d3d9_trace_refused reason=ttwvr_hash actual=%s", actual);
    return false;
  }
  auto *base = reinterpret_cast<unsigned char *>(module);
  auto *getter = base + 0x4f5770;
  uintptr_t address = 0;
  std::memcpy(&address, getter + 1, sizeof(address));
  if (getter[0] != 0xa1 || getter[5] != 0xc3 ||
      address != reinterpret_cast<uintptr_t>(base + 0x19710e8))
    return false;
  IDirect3DDevice9 *device = nullptr;
  std::memcpy(&device, reinterpret_cast<void *>(address), sizeof(device));
  if (!device) {
    Log("d3d9_trace_refused reason=no_game_device");
    return false;
  }
  D3DDEVICE_CREATION_PARAMETERS creation{};
  if (FAILED(device->GetCreationParameters(&creation)))
    return false;
  DWORD process = 0;
  GetWindowThreadProcessId(creation.hFocusWindow, &process);
  if (process != GetCurrentProcessId())
    return false;
  auto **table = *reinterpret_cast<void ***>(device);
  struct Hook {
    unsigned index;
    void *detour;
    void **original;
  };
  Hook hooks[] = {{37, (void *)HookSetRT, (void **)&setRT},
                  {43, (void *)HookClear, (void **)&clear},
                  {81, (void *)HookDraw, (void **)&draw},
                  {82, (void *)HookIndexed, (void **)&indexed},
                  {83, (void *)HookUP, (void **)&up},
                  {84, (void *)HookIndexedUP, (void **)&indexedUp},
                  {34, (void *)HookStretch, (void **)&stretch}};
  std::vector<void *> installed;
  ready = true;
  for (auto &h : hooks) {
    auto status = MH_CreateHook(table[h.index], h.detour, h.original);
    if (status == MH_OK) {
      installed.push_back(table[h.index]);
      status = MH_EnableHook(table[h.index]);
    }
    if (status != MH_OK) {
      Log("d3d9_trace_hook_failed index=%u status=%d", h.index, status);
      ready = false;
      break;
    }
  }
  if (!ready)
    for (auto *p : installed) {
      MH_DisableHook(p);
      MH_RemoveHook(p);
    }
  // Borrowed device, owned by TTWVR. No reference or state is changed.
  if (ready)
    InstallNativeGuides(module);
  if (ready) InstallSuperResolution(device);
  Log("d3d9_trace_ready=%d capture_only=1", ready);
  return ready;
}
void ArmD3D9Trace(bool enabled, uint64_t nextFrame) {
  std::lock_guard lock(mutex);
  armed = false;
  frame = nextFrame;
  ordinal = 0;
  dropped = 0;
  tonePass = 0;
  events.clear();
  captureInputs =
      GetPrivateProfileIntW(
          L"capture", L"scene_inputs", 0,
          (Directory() + L"\\TTWVRNeuralRendering.ini").c_str()) != 0;
  captureInputs = captureInputs && enabled;
  SceneJitterFrame(nextFrame);
  spatial = SpatialEnabled(nextFrame);
  SuperResolutionFrame(SuperResolutionReady());
  NativeGuidesFrame(nextFrame);
  armed = enabled;
}
void FinishD3D9Trace(uint64_t completedFrame, bool readback) {
  armed = false;
  std::lock_guard lock(mutex);
  if (readback)
    FinishSceneCapture();
  if (events.empty())
    return;
  const auto path =
      CaptureDirectory() + L"\\d3d9-frame-" + std::to_wstring(frame) + L".json";
  if (FILE *out = _wfopen(path.c_str(), L"wb")) {
    fprintf(out,
            "{\"schema\":1,\"frame\":%llu,\"end_frame\":%llu,\"guides_"
            "verified\":false,\"dropped\":%u,\"events\":[",
            (unsigned long long)frame, (unsigned long long)completedFrame,
            dropped);
    for (size_t i = 0; i < events.size(); ++i) {
      if (i)
        fputc(',', out);
      fwrite(events[i].data(), 1, events[i].size(), out);
    }
    fputs("]}\n", out);
    fclose(out);
  }
  Log("d3d9_trace_saved frame=%llu events=%zu dropped=%u",
      (unsigned long long)frame, events.size(), dropped);
  events.clear();
}
} // namespace ttwvrnr
