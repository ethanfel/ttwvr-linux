// SPDX-License-Identifier: GPL-3.0-only
// Exercise the actual GPU handoff with reused HDR/depth images across bitness.
// All CPU pixel verification happens after 32 asynchronous round trips.
#include "../src/DepthTransfer.h"
#include "../src/GpuTimeline.h"
#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cwchar>
#include <io.h>
using namespace ttwvrnr;
using Microsoft::WRL::ComPtr;
constexpr unsigned N = 32, W = 64;
struct Control {
  uint64_t color, depth, input, output;
  uint32_t parent, frame;
};
void Check(HRESULT r) {
  if (FAILED(r)) {
    printf("FAIL hr=%08lx\n", (unsigned long)r);
    std::exit(2);
  }
}
void Win(bool ok) {
  if (!ok) {
    printf("FAIL win32=%lu\n", GetLastError());
    std::exit(3);
  }
}
unsigned Verify(const void *data, unsigned pitch, const uint16_t *pixel) {
  unsigned bad = 0;
  for (unsigned y = 0; y < W; ++y)
    for (unsigned x = 0; x < W; ++x)
      bad += std::memcmp(static_cast<const char *>(data) + y * pitch + x * 8,
                         pixel, 8) != 0;
  return bad;
}
int wmain(int argc, wchar_t **argv) {
  bool child = argc == 3 && !std::wcscmp(argv[1], L"--child");
  if (!child && argc != 2)
    return 2;
  SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
  Win(freopen(child ? "timeline-texture-child.txt"
                    : "timeline-texture-parent.txt",
              "w", stdout));
  _dup2(_fileno(stdout), _fileno(stderr));
  setvbuf(stdout, nullptr, _IONBF, 0);
  std::wstring name = child ? argv[2]
                            : L"Local\\TTWVRTimelineProbe-" +
                                  std::to_wstring(GetCurrentProcessId());
  HANDLE mapping =
      child ? OpenFileMappingW(FILE_MAP_ALL_ACCESS, FALSE, name.c_str())
            : CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE,
                                 0, sizeof(Control), name.c_str());
  Win(mapping);
  auto *p = static_cast<Control *>(
      MapViewOfFile(mapping, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(Control)));
  Win(p);
  HANDLE request =
      CreateEventW(nullptr, FALSE, FALSE, (name + L"-request").c_str());
  HANDLE reply =
      CreateEventW(nullptr, FALSE, FALSE, (name + L"-reply").c_str());
  Win(request && reply);
  unsigned mismatches = 0;
  if (child) {
    ComPtr<ID3D11Device> d;
    ComPtr<ID3D11DeviceContext> c;
    Check(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0,
                            nullptr, 0, D3D11_SDK_VERSION, &d, nullptr, &c));
    GpuTimeline timeline;
    timeline.Init(d.Get(), p->parent, p->input, p->output);
    ComPtr<ID3D11Texture2D> color, depth;
    Check(d->OpenSharedResource(reinterpret_cast<HANDLE>(uintptr_t(p->color)),
                                IID_PPV_ARGS(&color)));
    Check(d->OpenSharedResource(reinterpret_cast<HANDLE>(uintptr_t(p->depth)),
                                IID_PPV_ARGS(&depth)));
    ComPtr<ID3D11RenderTargetView> rtv;
    Check(d->CreateRenderTargetView(color.Get(), nullptr, &rtv));
    std::array<ComPtr<ID3D11Texture2D>, N> colors, depths;
    for (unsigned i = 0; i < N; ++i)
      for (unsigned kind = 0; kind < 2; ++kind) {
        D3D11_TEXTURE2D_DESC desc{};
        (kind ? depth : color)->GetDesc(&desc);
        desc.Usage = D3D11_USAGE_STAGING;
        desc.BindFlags = desc.MiscFlags = 0;
        desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        Check(d->CreateTexture2D(
            &desc, nullptr, (kind ? depths[i] : colors[i]).GetAddressOf()));
      }
    for (unsigned i = 0; i < N; ++i) {
      Win(WaitForSingleObject(request, 10000) == WAIT_OBJECT_0);
      Win(p->frame == i);
      timeline.WaitInput(i + 1);
      c->CopyResource(colors[i].Get(), color.Get());
      c->CopyResource(depths[i].Get(), depth.Get());
      const float fill[] = {.5f, (i & 1) ? 1.f : 0.f, .25f, 1.f};
      c->ClearRenderTargetView(rtv.Get(), fill);
      timeline.SignalOutput(i + 1);
      Win(SetEvent(reply));
    }
    for (unsigned i = 0; i < N; ++i) {
      D3D11_MAPPED_SUBRESOURCE m{};
      Check(c->Map(colors[i].Get(), 0, D3D11_MAP_READ, 0, &m));
      const uint16_t expected[] = {uint16_t((i & 1) ? 0 : 0x3c00),
                                   uint16_t((i & 1) ? 0x3c00 : 0), 0, 0x3c00};
      auto colorBad = Verify(m.pData, m.RowPitch, expected);
      mismatches += colorBad;
      if (i < 3 || colorBad) {
        auto *v = static_cast<uint16_t *>(m.pData);
        printf("input frame=%u color_bad=%u first=%04x,%04x,%04x,%04x\n", i,
               colorBad, v[0], v[1], v[2], v[3]);
      }
      c->Unmap(colors[i].Get(), 0);
      Check(c->Map(depths[i].Get(), 0, D3D11_MAP_READ, 0, &m));
      uint32_t firstDepth = 0;
      std::memcpy(&firstDepth, m.pData, 4);
      if (i < 3)
        printf("depth frame=%u first=%08x\n", i, firstDepth);
      const uint32_t expectedDepth = (i & 1) ? 0xbfffff : 0x3fffff;
      for (unsigned y = 0; y < W; ++y)
        for (unsigned x = 0; x < W; ++x) {
          uint32_t v = 0;
          std::memcpy(
              &v, static_cast<const char *>(m.pData) + y * m.RowPitch + x * 4,
              4);
          mismatches += std::abs(int(v & 0xffffff) - int(expectedDepth)) > 1;
        }
      c->Unmap(depths[i].Get(), 0);
    }
  } else {
    auto window = CreateWindowExW(0, L"STATIC", L"TTWVR timeline probe",
                                  WS_OVERLAPPED, 0, 0, W, W, nullptr, nullptr,
                                  GetModuleHandleW(nullptr), nullptr);
    Win(window);
    ComPtr<IDirect3D9Ex> api;
    Check(Direct3DCreate9Ex(D3D_SDK_VERSION, &api));
    D3DPRESENT_PARAMETERS pp{};
    pp.BackBufferWidth = pp.BackBufferHeight = W;
    pp.BackBufferFormat = D3DFMT_X8R8G8B8;
    pp.BackBufferCount = 1;
    pp.SwapEffect = D3DSWAPEFFECT_DISCARD;
    pp.Windowed = TRUE;
    pp.hDeviceWindow = window;
    ComPtr<IDirect3DDevice9Ex> d;
    Check(api->CreateDeviceEx(0, D3DDEVTYPE_HAL, window,
                              D3DCREATE_HARDWARE_VERTEXPROCESSING, &pp, nullptr,
                              &d));
    auto texture = [&](D3DFORMAT format, ComPtr<IDirect3DTexture9> &t,
                       ComPtr<IDirect3DSurface9> &s) {
      HANDLE h = nullptr;
      Check(d->CreateTexture(W, W, 1, D3DUSAGE_RENDERTARGET, format,
                             D3DPOOL_DEFAULT, &t, &h));
      Check(t->GetSurfaceLevel(0, &s));
      return uintptr_t(h);
    };
    ComPtr<IDirect3DTexture9> color, packed;
    ComPtr<IDirect3DSurface9> surface, packedSurface, depth;
    p->color = texture(D3DFMT_A16B16G16R16F, color, surface);
    p->depth = texture(D3DFMT_A8B8G8R8, packed, packedSurface);
    ComPtr<IDirect3DTexture9> depthTexture;
    Check(d->CreateTexture(W, W, 1, D3DUSAGE_DEPTHSTENCIL,
                           (D3DFORMAT)MAKEFOURCC('I', 'N', 'T', 'Z'),
                           D3DPOOL_DEFAULT, &depthTexture, nullptr));
    Check(depthTexture->GetSurfaceLevel(0, &depth));
    Check(d->SetRenderTarget(0, surface.Get()));
    Check(d->SetDepthStencilSurface(depth.Get()));
    GpuTimeline timeline;
    timeline.Init(d.Get());
    p->input = timeline.InputHandle();
    p->output = timeline.OutputHandle();
    p->parent = GetCurrentProcessId();
    std::array<ComPtr<IDirect3DSurface9>, N> outputs;
    for (auto &s : outputs)
      Check(d->CreateRenderTarget(W, W, D3DFMT_A16B16G16R16F,
                                  D3DMULTISAMPLE_NONE, 0, FALSE, &s, nullptr));
    std::wstring command =
        L"\"" + std::wstring(argv[1]) + L"\" --child \"" + name + L"\"";
    STARTUPINFOW si{};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};
    Win(CreateProcessW(argv[1], command.data(), nullptr, nullptr, FALSE, 0,
                       nullptr, nullptr, &si, &pi));
    for (unsigned i = 0; i < N; ++i) {
      Check(d->ColorFill(surface.Get(), nullptr,
                         (i & 1) ? 0xff00ff00 : 0xffff0000));
      Check(
          d->Clear(0, nullptr, D3DCLEAR_ZBUFFER, 0, (i & 1) ? .75f : .25f, 0));
      Win(TransferDepthBits(
          d.Get(), depth.Get(), packedSurface.Get(),
          GetEnvironmentVariableW(L"TTWVR_PROBE_CPU_DEPTH", nullptr, 0) != 0));
      p->frame = i;
      timeline.SignalInput(i + 1);
      Win(SetEvent(request));
      HANDLE waits[] = {pi.hProcess, reply};
      Win(WaitForMultipleObjects(2, waits, FALSE, 10000) == WAIT_OBJECT_0 + 1);
      timeline.WaitOutput(i + 1);
      Check(d->StretchRect(surface.Get(), nullptr, outputs[i].Get(), nullptr,
                           D3DTEXF_NONE));
    }
    ComPtr<IDirect3DSurface9> staging;
    Check(d->CreateOffscreenPlainSurface(W, W, D3DFMT_A16B16G16R16F,
                                         D3DPOOL_SYSTEMMEM, &staging, nullptr));
    for (unsigned i = 0; i < N; ++i) {
      Check(d->GetRenderTargetData(outputs[i].Get(), staging.Get()));
      D3DLOCKED_RECT m{};
      Check(staging->LockRect(&m, nullptr, D3DLOCK_READONLY));
      const uint16_t expected[] = {0x3800, uint16_t((i & 1) ? 0x3c00 : 0),
                                   0x3400, 0x3c00};
      mismatches += Verify(m.pBits, m.Pitch, expected);
      Check(staging->UnlockRect());
    }
    if (WaitForSingleObject(pi.hProcess, 10000) != WAIT_OBJECT_0) {
      TerminateProcess(pi.hProcess, 4);
      return 4;
    }
    printf("parent_output_mismatches=%u\n", mismatches);
    DWORD code = 0;
    Win(GetExitCodeProcess(pi.hProcess, &code));
    Win(code == 0);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
  }
  printf("%s %zu-bit frames=%u pixels_per_frame=%u mismatches=%u "
         "deferred_readback=1\n",
         mismatches ? "FAIL" : "PASS", sizeof(void *) * 8, N, W * W,
         mismatches);
  return mismatches ? 4 : 0;
}
