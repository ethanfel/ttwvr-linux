// SPDX-License-Identifier: GPL-3.0-only
// Redirect only the HDR texture identified at the verified TTWVR tone-map draw.
// Logical game resources and full-resolution output/HUD remain allocated.
#include "SuperResolution.h"
#include "Support.h"
#include <MinHook.h>
#include <wrl/client.h>
namespace ttwvrnr {
namespace {
using Microsoft::WRL::ComPtr;
using Target = HRESULT(STDMETHODCALLTYPE *)(IDirect3DDevice9 *, DWORD,
                                            IDirect3DSurface9 *);
using Depth = HRESULT(STDMETHODCALLTYPE *)(IDirect3DDevice9 *,
                                           IDirect3DSurface9 *);
using Texture = HRESULT(STDMETHODCALLTYPE *)(IDirect3DDevice9 *, DWORD,
                                             IDirect3DBaseTexture9 *);
using Viewport = HRESULT(STDMETHODCALLTYPE *)(IDirect3DDevice9 *,
                                              const D3DVIEWPORT9 *);
Target target;
Depth depth;
Texture texture;
Viewport viewport;
IDirect3DDevice9 *device;
ComPtr<IDirect3DTexture9> originalColor, lowColor;
ComPtr<IDirect3DSurface9> originalSurface, originalDepth, lowSurface, lowDepth;
ComPtr<IDirect3DBaseTexture9> toneSaved;
unsigned lowMode = 0;
bool active = false, probe = false, installed = false, internal = false;
struct Guard {
  bool saved = internal;
  Guard() { internal = true; }
  ~Guard() { internal = saved; }
};
bool LowTarget(IDirect3DDevice9 *d) {
  ComPtr<IDirect3DSurface9> t;
  return active && SUCCEEDED(d->GetRenderTarget(0, &t)) &&
         t.Get() == lowSurface.Get();
}
HRESULT STDMETHODCALLTYPE SetTarget(IDirect3DDevice9 *d, DWORD n,
                                    IDirect3DSurface9 *p) {
  if (d != device || internal || !active || n != 0)
    return target(d, n, p);
  Guard guard;
  auto *mapped = p == originalSurface.Get() ? lowSurface.Get() : p;
  auto hr = target(d, n, mapped);
  if (FAILED(hr))
    return hr;
  ComPtr<IDirect3DSurface9> ds;
  d->GetDepthStencilSurface(&ds);
  if (ds.Get() == originalDepth.Get() || ds.Get() == lowDepth.Get()) {
    D3DSURFACE_DESC td{};
    if (mapped)
      mapped->GetDesc(&td);
    depth(d, td.Width <= SRInputSize(lowMode).width &&
                     td.Height <= SRInputSize(lowMode).height
                 ? lowDepth.Get()
                 : originalDepth.Get());
  }
  return hr;
}
HRESULT STDMETHODCALLTYPE SetDepth(IDirect3DDevice9 *d, IDirect3DSurface9 *p) {
  if (d == device && !internal && active && p == originalDepth.Get() &&
      LowTarget(d))
    p = lowDepth.Get();
  return depth(d, p);
}
HRESULT STDMETHODCALLTYPE SetTexture(IDirect3DDevice9 *d, DWORD n,
                                     IDirect3DBaseTexture9 *p) {
  if (d == device && !internal && active && p == originalColor.Get())
    p = lowColor.Get();
  return texture(d, n, p);
}
HRESULT STDMETHODCALLTYPE SetViewport(IDirect3DDevice9 *d,
                                      const D3DVIEWPORT9 *v) {
  if (d != device || internal || !v || !LowTarget(d))
    return viewport(d, v);
  auto scaled = *v;
  if (v->X == 0 && v->Y == 0 && v->Width == OutputWidth &&
      v->Height == OutputHeight) {
    scaled.Width = SRInputSize(lowMode).width;
    scaled.Height = SRInputSize(lowMode).height;
  }
  return viewport(d, &scaled);
}
} // namespace
bool InstallSuperResolution(IDirect3DDevice9 *d) {
  const auto ini = Directory() + L"\\TTWVRNeuralRendering.ini";
  probe = GetPrivateProfileIntW(L"super_resolution", L"capture_probe", 0,
                                ini.c_str()) != 0;
  device = d;
  auto **v = *reinterpret_cast<void ***>(d);
  // SetRenderTarget is shared with D3D9Trace: install via its forwarding API.
  struct H {
    unsigned slot;
    void *hook;
    void **original;
  };
  H hooks[] = {{39, (void *)SetDepth, (void **)&depth},
               {65, (void *)SetTexture, (void **)&texture},
               {47, (void *)SetViewport, (void **)&viewport}};
  unsigned created = 0;
  for (auto &h : hooks) {
    if (MH_CreateHook(v[h.slot], h.hook, h.original) != MH_OK)
      break;
    ++created;
  }
  bool ok = created == 3;
  if (ok)
    for (auto &h : hooks)
      if (MH_EnableHook(v[h.slot]) != MH_OK) {
        ok = false;
        break;
      }
  if (!ok) {
    for (unsigned i = 0; i < created; ++i) {
      MH_DisableHook(v[hooks[i].slot]);
      MH_RemoveHook(v[hooks[i].slot]);
    }
    device = nullptr;
    Log("sr_hooks_failed bypass=1");
    return false;
  }
  installed = true;
  Log("sr_adapter_installed output=%ux%u modes=Quality,Balanced,Performance "
      "probe=%u",
      OutputWidth, OutputHeight, probe);
  return true;
}
// Called by the existing SetRenderTarget hook; it owns the one MinHook detour.
HRESULT SuperResolutionSetTarget(IDirect3DDevice9 *d, DWORD n,
                                 IDirect3DSurface9 *p, Target next) {
  target = next;
  return SetTarget(d, n, p);
}
void SuperResolutionObserveTone(IDirect3DDevice9 *d) {
  if (!installed || originalColor || d != device)
    return;
  ComPtr<IDirect3DBaseTexture9> t;
  ComPtr<IDirect3DTexture9> color;
  ComPtr<IDirect3DSurface9> ds;
  if (FAILED(d->GetTexture(1, &t)) || !t || FAILED(t.As(&color)) ||
      FAILED(d->GetDepthStencilSurface(&ds)) || !ds)
    return;
  D3DSURFACE_DESC c{}, z{};
  color->GetLevelDesc(0, &c);
  ds->GetDesc(&z);
  if (c.Width != OutputWidth || c.Height != OutputHeight ||
      c.Format != D3DFMT_A16B16G16R16F || z.Width != c.Width ||
      z.Height != c.Height || z.Format != D3DFMT_D24S8)
    return;
  ComPtr<IDirect3DSurface9> os;
  if (FAILED(color->GetSurfaceLevel(0, &os)))
    return;
  originalColor = color;
  originalSurface = os;
  originalDepth = ds;
  Log("sr_scene_identified hdr=%p depth=%p output=%ux%u", os.Get(), ds.Get(),
      OutputWidth, OutputHeight);
}
void SuperResolutionFrame(unsigned readyMode) {
  // The helper publishes a ready mode only after releasing the previous
  // transport and creating the matching NGX feature. Never resize mid-eye.
  const unsigned mode = ValidSRMode(readyMode) ? (readyMode ? readyMode
                                                  : probe   ? 1
                                                            : 0)
                                               : 0;
  if (active && (!mode || mode != lowMode)) {
    Guard guard;
    ComPtr<IDirect3DSurface9> rt;
    device->GetRenderTarget(0, &rt);
    if (rt.Get() == lowSurface.Get())
      target(device, 0, originalSurface.Get());
    for (unsigned slot = 0; slot < 16; ++slot) {
      ComPtr<IDirect3DBaseTexture9> t;
      device->GetTexture(slot, &t);
      if (t.Get() == lowColor.Get())
        texture(device, slot, originalColor.Get());
    }
    ComPtr<IDirect3DSurface9> ds;
    device->GetDepthStencilSurface(&ds);
    if (ds.Get() == lowDepth.Get())
      depth(device, originalDepth.Get());
    // SetRenderTarget can reset the viewport; explicitly restore the native
    // viewport if the reduced one is still bound.
    D3DVIEWPORT9 vp{};
    if (SUCCEEDED(device->GetViewport(&vp)) && vp.X == 0 && vp.Y == 0 &&
        vp.Width == SRInputSize(lowMode).width &&
        vp.Height == SRInputSize(lowMode).height) {
      vp.Width = OutputWidth;
      vp.Height = OutputHeight;
      Guard guard;
      viewport(device, &vp);
    }
    active = false;
    Log("sr_scene_rendering low=0 mode=Off input=%ux%u", OutputWidth,
        OutputHeight);
  }
  if (mode && originalColor && mode != lowMode) {
    // Allocate a complete replacement before releasing the previous resources.
    // Their old bindings were restored above; failure keeps the native bypass.
    Guard guard;
    const auto size = SRInputSize(mode);
    ComPtr<IDirect3DTexture9> lc;
    ComPtr<IDirect3DSurface9> ls, ld;
    if (FAILED(device->CreateTexture(
            size.width, size.height, 1, D3DUSAGE_RENDERTARGET,
            D3DFMT_A16B16G16R16F, D3DPOOL_DEFAULT, &lc, nullptr)) ||
        FAILED(lc->GetSurfaceLevel(0, &ls)) ||
        FAILED(device->CreateDepthStencilSurface(
            size.width, size.height, D3DFMT_D24S8, D3DMULTISAMPLE_NONE, 0,
            FALSE, &ld, nullptr))) {
      Log("sr_adapter_allocation_failed mode=%s bypass=1", SRName(mode));
      return;
    }
    lowColor = lc;
    lowSurface = ls;
    lowDepth = ld;
    lowMode = mode;
  }
  const bool next = mode && lowMode == mode && !!lowColor;
  if (active != next)
    Log("sr_scene_rendering low=%u mode=%s input=%ux%u output=%ux%u", next,
        SRName(next ? mode : 0), SRInputSize(next ? mode : 0).width,
        SRInputSize(next ? mode : 0).height, OutputWidth, OutputHeight);
  active = next;
}
unsigned SuperResolutionMode() { return active ? lowMode : 0; }
unsigned SuperResolutionWidth() {
  return SRInputSize(SuperResolutionMode()).width;
}
unsigned SuperResolutionHeight() {
  return SRInputSize(SuperResolutionMode()).height;
}

bool SuperResolutionLow() { return active; }
IDirect3DSurface9 *SuperResolutionDepth() {
  return active ? lowDepth.Get() : nullptr;
}
bool SuperResolutionBindOutput(IDirect3DDevice9 *d, IDirect3DTexture9 *out) {
  if (!active || !out || d != device)
    return false;
  Guard guard;
  if (FAILED(d->GetTexture(1, &toneSaved)) || !toneSaved)
    return false;
  if (FAILED(texture(d, 1, out))) {
    toneSaved.Reset();
    return false;
  }
  return true;
}
void SuperResolutionRestoreTone(IDirect3DDevice9 *d) {
  if (!toneSaved)
    return;
  Guard guard;
  texture(d, 1, toneSaved.Get());
  toneSaved.Reset();
}
} // namespace ttwvrnr
