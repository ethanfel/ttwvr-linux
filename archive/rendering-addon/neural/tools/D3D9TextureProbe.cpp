// SPDX-License-Identifier: GPL-3.0-only
// Standalone GPU transport proof. CPU reads verify pixels only in this probe.
#include "../src/DepthTransfer.h"
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <d3d11.h>
#include <d3d9.h>
#include <windows.h>
#include <wrl/client.h>
using Microsoft::WRL::ComPtr;
void Check(HRESULT hr, const char *op) {
  if (FAILED(hr)) {
    printf("FAIL %s hr=%08lx\n", op, (unsigned long)hr);
    exit(2);
  }
}
void Wait9(IDirect3DDevice9 *d) {
  ComPtr<IDirect3DQuery9> q;
  Check(d->CreateQuery(D3DQUERYTYPE_EVENT, &q), "query9");
  Check(q->Issue(D3DISSUE_END), "issue9");
  auto start = GetTickCount64();
  HRESULT r;
  while ((r = q->GetData(nullptr, 0, D3DGETDATA_FLUSH)) == S_FALSE) {
    if (GetTickCount64() - start > 5000) {
      puts("FAIL timeout9");
      exit(3);
    }
    Sleep(0);
  }
  Check(r, "wait9");
}
void Wait11(ID3D11Device *d, ID3D11DeviceContext *c) {
  ComPtr<ID3D11Query> q;
  D3D11_QUERY_DESC desc{D3D11_QUERY_EVENT, 0};
  Check(d->CreateQuery(&desc, &q), "query11");
  c->End(q.Get());
  c->Flush();
  auto start = GetTickCount64();
  HRESULT r;
  while ((r = c->GetData(q.Get(), nullptr, 0, 0)) == S_FALSE) {
    if (GetTickCount64() - start > 5000) {
      puts("FAIL timeout11");
      exit(3);
    }
    Sleep(0);
  }
  Check(r, "wait11");
}
int main() {
  SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
  freopen("d3d9-texture-probe.txt", "w", stdout);
  setvbuf(stdout, nullptr, _IONBF, 0);
  auto window = CreateWindowExW(
      0, L"STATIC", L"TTWVR independent transport probe", WS_OVERLAPPED, 0, 0,
      64, 64, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
  ComPtr<IDirect3D9Ex> api;
  Check(Direct3DCreate9Ex(D3D_SDK_VERSION, &api), "api9");
  D3DPRESENT_PARAMETERS pp{};
  pp.BackBufferWidth = pp.BackBufferHeight = 64;
  pp.BackBufferFormat = D3DFMT_X8R8G8B8;
  pp.BackBufferCount = 1;
  pp.SwapEffect = D3DSWAPEFFECT_DISCARD;
  pp.Windowed = TRUE;
  pp.hDeviceWindow = window;
  ComPtr<IDirect3DDevice9Ex> d9;
  Check(api->CreateDeviceEx(0, D3DDEVTYPE_HAL, window,
                            D3DCREATE_HARDWARE_VERTEXPROCESSING, &pp, nullptr,
                            &d9),
        "device9");
  HANDLE handle = nullptr;
  ComPtr<IDirect3DTexture9> t9;
  Check(d9->CreateTexture(64, 64, 1, D3DUSAGE_RENDERTARGET,
                          D3DFMT_A16B16G16R16F, D3DPOOL_DEFAULT, &t9, &handle),
        "shared_texture9");
  ComPtr<IDirect3DSurface9> surface;
  t9->GetSurfaceLevel(0, &surface);
  ComPtr<ID3D11Device> d11;
  ComPtr<ID3D11DeviceContext> c;
  Check(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0,
                          nullptr, 0, D3D11_SDK_VERSION, &d11, nullptr, &c),
        "device11");
  ComPtr<ID3D11Texture2D> t11;
  Check(d11->OpenSharedResource(handle, IID_PPV_ARGS(&t11)), "open9_from11");
  D3D11_TEXTURE2D_DESC desc{};
  t11->GetDesc(&desc);
  printf("format=%u size=%ux%u shared=%u\n", desc.Format, desc.Width,
         desc.Height, desc.MiscFlags);
  if (desc.Format != DXGI_FORMAT_R16G16B16A16_FLOAT)
    return 4;
  ComPtr<ID3D11RenderTargetView> rtv;
  Check(d11->CreateRenderTargetView(t11.Get(), nullptr, &rtv), "rtv11");
  desc.Usage = D3D11_USAGE_STAGING;
  desc.BindFlags = desc.MiscFlags = 0;
  desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
  ComPtr<ID3D11Texture2D> stage11;
  Check(d11->CreateTexture2D(&desc, nullptr, &stage11), "stage11");
  ComPtr<IDirect3DSurface9> stage9;
  Check(d9->CreateOffscreenPlainSurface(64, 64, D3DFMT_A16B16G16R16F,
                                        D3DPOOL_SYSTEMMEM, &stage9, nullptr),
        "stage9");
  unsigned mismatches = 0;
  for (unsigned iter = 0; iter < 100; ++iter) {
    const bool odd = iter & 1;
    Check(d9->ColorFill(surface.Get(), nullptr, odd ? 0xff00ff00 : 0xffff0000),
          "fill9");
    Wait9(d9.Get());
    c->CopyResource(stage11.Get(), t11.Get());
    D3D11_MAPPED_SUBRESOURCE map{};
    Check(c->Map(stage11.Get(), 0, D3D11_MAP_READ, 0, &map), "read11");
    const uint16_t a[4] = {uint16_t(odd ? 0 : 0x3c00),
                           uint16_t(odd ? 0x3c00 : 0), 0, 0x3c00};
    for (unsigned y = 0; y < 64; ++y)
      for (unsigned x = 0; x < 64; ++x)
        mismatches +=
            memcmp(static_cast<char *>(map.pData) + y * map.RowPitch + x * 8, a,
                   8) != 0;
    c->Unmap(stage11.Get(), 0);
    const float b[4] = {odd ? .5f : .75f, .25f, .5f, 1.f};
    c->ClearRenderTargetView(rtv.Get(), b);
    Wait11(d11.Get(), c.Get());
    Check(d9->GetRenderTargetData(surface.Get(), stage9.Get()), "readback9");
    D3DLOCKED_RECT lock{};
    Check(stage9->LockRect(&lock, nullptr, D3DLOCK_READONLY), "read9");
    const uint16_t expected[4] = {uint16_t(odd ? 0x3800 : 0x3a00), 0x3400,
                                  0x3800, 0x3c00};
    for (unsigned y = 0; y < 64; ++y)
      for (unsigned x = 0; x < 64; ++x)
        mismatches +=
            memcmp(static_cast<char *>(lock.pBits) + y * lock.Pitch + x * 8,
                   expected, 8) != 0;
    stage9->UnlockRect();
  }
  printf("round_trips=100 verified_pixels=819200 mismatches=%u\n", mismatches);

  ComPtr<IDirect3DTexture9> depth9;
  Check(d9->CreateTexture(64, 64, 1, D3DUSAGE_DEPTHSTENCIL,
                          (D3DFORMAT)MAKEFOURCC('I', 'N', 'T', 'Z'),
                          D3DPOOL_DEFAULT, &depth9, nullptr),
        "shared_depth9");
  ComPtr<IDirect3DSurface9> depthSurface;
  depth9->GetSurfaceLevel(0, &depthSurface);
  Check(d9->SetRenderTarget(0, surface.Get()), "set_rt9");
  Check(d9->SetDepthStencilSurface(depthSurface.Get()), "set_depth9");
  const float zValues[4] = {.1234567f, .375f, .9876543f, 1.f};
  ComPtr<IDirect3DTexture9> packed9;
  HANDLE packedHandle = nullptr;
  Check(d9->CreateTexture(64, 64, 1, D3DUSAGE_RENDERTARGET, D3DFMT_A8B8G8R8,
                          D3DPOOL_DEFAULT, &packed9, &packedHandle),
        "packed_depth9");
  ComPtr<IDirect3DSurface9> packedSurface;
  packed9->GetSurfaceLevel(0, &packedSurface);
  ComPtr<ID3D11Texture2D> depth11;
  Check(d11->OpenSharedResource(packedHandle, IID_PPV_ARGS(&depth11)),
        "open_packed_depth9_from11");
  depth11->GetDesc(&desc);
  printf("shared_depth_format=%u size=%ux%u\n", desc.Format, desc.Width,
         desc.Height);
  desc.Usage = D3D11_USAGE_STAGING;
  desc.BindFlags = desc.MiscFlags = 0;
  desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
  ComPtr<ID3D11Texture2D> depthStage;
  Check(d11->CreateTexture2D(&desc, nullptr, &depthStage), "stage_depth11");
  // Production queues color and depth before the Vulkan transfer. Its fence
  // must complete BOTH without an additional D3D9 event-query flush/wait.
  for (unsigned iter = 0; iter < 32; ++iter) {
    const bool odd = iter & 1;
    Check(d9->ColorFill(surface.Get(), nullptr, odd ? 0xff00ff00 : 0xffff0000),
          "pending_color9");
    for (unsigned q = 0; q < 4; ++q) {
      D3DRECT r{LONG(q % 2 * 32), LONG(q / 2 * 32), LONG(q % 2 * 32 + 32),
                LONG(q / 2 * 32 + 32)};
      Check(d9->Clear(1, &r, D3DCLEAR_ZBUFFER, 0, zValues[(q + iter) % 4], 0),
            "pending_depth9");
    }
    if (!ttwvrnr::TransferDepthBits(d9.Get(), depthSurface.Get(),
                                    packedSurface.Get()))
      return 6;
    c->CopyResource(stage11.Get(), t11.Get());
    D3D11_MAPPED_SUBRESOURCE map{};
    Check(c->Map(stage11.Get(), 0, D3D11_MAP_READ, 0, &map), "fenced_color11");
    const uint16_t expected[4] = {uint16_t(odd ? 0 : 0x3c00),
                                  uint16_t(odd ? 0x3c00 : 0), 0, 0x3c00};
    for (unsigned y = 0; y < 64; ++y)
      for (unsigned x = 0; x < 64; ++x)
        mismatches += std::memcmp(static_cast<char *>(map.pData) +
                                      y * map.RowPitch + x * 8,
                                  expected, sizeof(expected)) != 0;
    c->Unmap(stage11.Get(), 0);
    c->CopyResource(depthStage.Get(), depth11.Get());
    Check(c->Map(depthStage.Get(), 0, D3D11_MAP_READ, 0, &map),
          "fenced_depth11");
    for (unsigned y = 0; y < 64; ++y)
      for (unsigned x = 0; x < 64; ++x) {
        uint32_t value;
        memcpy(&value,
               static_cast<char *>(map.pData) + y * map.RowPitch + x * 4, 4);
        mismatches +=
            std::abs(float(value & 0xffffff) / 16777215.f -
                     zValues[((y / 32) * 2 + x / 32 + iter) % 4]) > 1e-6f;
      }
    c->Unmap(depthStage.Get(), 0);
  }
  printf("fence_only_round_trips=32 verified_color_depth_pixels=262144 "
         "total_mismatches=%u\n",
         mismatches);
  DestroyWindow(window);
  return mismatches ? 5 : 0;
}
