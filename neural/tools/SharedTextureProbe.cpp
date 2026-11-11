// SPDX-License-Identifier: GPL-3.0-only
// Cross-process 32/64-bit GPU image verification, independent of game/VR/NGX.
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cwchar>
#include <d3d11_1.h>
#include <dxgi1_2.h>
#include <io.h>
#include <windows.h>
#include <wrl/client.h>
using Microsoft::WRL::ComPtr;
void Check(HRESULT hr, const char *op) {
  if (FAILED(hr)) {
    printf("FAIL %s hr=%08lx\n", op, (unsigned long)hr);
    exit(2);
  }
}
void Win(bool ok, const char *op) {
  if (!ok) {
    printf("FAIL %s win32=%lu\n", op, GetLastError());
    exit(2);
  }
}
void Verify(ID3D11Device *d, ID3D11DeviceContext *c, ID3D11Texture2D *t,
            const float (&expected)[4]) {
  D3D11_TEXTURE2D_DESC desc{};
  t->GetDesc(&desc);
  desc.Usage = D3D11_USAGE_STAGING;
  desc.BindFlags = desc.MiscFlags = 0;
  desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
  ComPtr<ID3D11Texture2D> stage;
  Check(d->CreateTexture2D(&desc, nullptr, &stage), "staging");
  c->CopyResource(stage.Get(), t);
  D3D11_MAPPED_SUBRESOURCE map{};
  Check(c->Map(stage.Get(), 0, D3D11_MAP_READ, 0, &map), "readback");
  unsigned mismatches = 0;
  for (unsigned y = 0; y < desc.Height; ++y) {
    const auto *p = reinterpret_cast<const float *>(
        static_cast<const char *>(map.pData) + y * map.RowPitch);
    for (unsigned x = 0; x < desc.Width; ++x)
      for (unsigned k = 0; k < 4; ++k)
        if (!std::isfinite(p[x * 4 + k]) ||
            std::abs(p[x * 4 + k] - expected[k]) > 1e-6f)
          ++mismatches;
  }
  c->Unmap(stage.Get(), 0);
  printf("pixels_verified=%u mismatches=%u\n", desc.Width * desc.Height,
         mismatches);
  if (mismatches)
    exit(3);
}
int wmain(int argc, wchar_t **argv) {
  SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
  const bool child = argc == 4 && !wcscmp(argv[1], L"--child");
  if (!child && argc != 2)
    return 2;
  if (!freopen(child ? "texture-child.txt" : "texture-parent.txt", "w", stdout))
    return 2;
  _dup2(_fileno(stdout), _fileno(stderr));
  setvbuf(stdout, nullptr, _IONBF, 0);
  printf("process_bits=%zu\n", sizeof(void *) * 8);
  ComPtr<ID3D11Device> base;
  ComPtr<ID3D11DeviceContext> context;
  const D3D_FEATURE_LEVEL level = D3D_FEATURE_LEVEL_11_0;
  Check(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, &level,
                          1, D3D11_SDK_VERSION, &base, nullptr, &context),
        "device");
  ComPtr<ID3D11Device1> d;
  Check(base.As(&d), "device1");
  ComPtr<ID3D11Texture2D> texture;
  HANDLE shared = nullptr;
  HANDLE parent = nullptr;
  const float a[] = {.125f, .25f, .5f, 1.f}, b[] = {.75f, .5f, .25f, 1.f};
  if (child) {
    parent =
        OpenProcess(PROCESS_DUP_HANDLE, FALSE, wcstoul(argv[2], nullptr, 10));
    Win(parent, "parent");
    auto remote =
        reinterpret_cast<HANDLE>(uintptr_t(wcstoull(argv[3], nullptr, 10)));
    Win(DuplicateHandle(parent, remote, GetCurrentProcess(), &shared, 0, FALSE,
                        DUPLICATE_SAME_ACCESS),
        "duplicate");
    Check(d->OpenSharedResource1(shared, IID_PPV_ARGS(&texture)),
          "open_shared_texture");
  } else {
    D3D11_TEXTURE2D_DESC desc{};
    desc.Width = desc.Height = 64;
    desc.ArraySize = desc.MipLevels = desc.SampleDesc.Count = 1;
    desc.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
    desc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
    desc.MiscFlags = D3D11_RESOURCE_MISC_SHARED_NTHANDLE |
                     D3D11_RESOURCE_MISC_SHARED_KEYEDMUTEX;
    Check(d->CreateTexture2D(&desc, nullptr, &texture), "texture");
    ComPtr<IDXGIResource1> resource;
    Check(texture.As(&resource), "resource1");
    Check(resource->CreateSharedHandle(
              nullptr, DXGI_SHARED_RESOURCE_READ | DXGI_SHARED_RESOURCE_WRITE,
              nullptr, &shared),
          "share");
  }
  ComPtr<IDXGIKeyedMutex> mutex;
  Check(texture.As(&mutex), "keyed_mutex");
  ComPtr<ID3D11RenderTargetView> rtv;
  Check(d->CreateRenderTargetView(texture.Get(), nullptr, &rtv), "rtv");
  const auto acquire = mutex->AcquireSync(child ? 1 : 0, 10000);
  if (acquire != S_OK) {
    printf("FAIL acquire=%08lx\n", (unsigned long)acquire);
    return 4;
  }
  if (child)
    Verify(d.Get(), context.Get(), texture.Get(), a);
  context->ClearRenderTargetView(rtv.Get(), child ? b : a);
  Check(mutex->ReleaseSync(child ? 2 : 1), "release");
  context->Flush();
  if (!child) {
    wchar_t command[32768]{};
    swprintf(command, 32768, L"\"%ls\" --child %lu %llu", argv[1],
             GetCurrentProcessId(), (unsigned long long)(uintptr_t)shared);
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};
    Win(CreateProcessW(argv[1], command, nullptr, nullptr, FALSE, 0, nullptr,
                       nullptr, &startup, &process),
        "child");
    if (WaitForSingleObject(process.hProcess, 20000) != WAIT_OBJECT_0) {
      puts("FAIL child timeout");
      TerminateProcess(process.hProcess, 5);
      return 5;
    }
    DWORD code = 0;
    GetExitCodeProcess(process.hProcess, &code);
    printf("child_exit=%lu\n", code);
    if (code)
      return 6;
    const auto result = mutex->AcquireSync(2, 10000);
    if (result != S_OK) {
      printf("FAIL return acquire=%08lx\n", (unsigned long)result);
      return 7;
    }
    Verify(d.Get(), context.Get(), texture.Get(), b);
    Check(mutex->ReleaseSync(3), "final release");
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
  }
  if (parent)
    CloseHandle(parent);
  CloseHandle(shared);
  puts("PASS shared HDR texture and keyed GPU synchronization");
  return 0;
}
