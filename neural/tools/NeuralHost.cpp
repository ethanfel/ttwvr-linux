// SPDX-License-Identifier: GPL-3.0-only
#include "../src/HostProtocol.h"
#include "../src/Support.h"
#include "../src/backend/NeuralBackend.h"
#include <cmath>
#include <cstdio>
#include <cstring>
#include <d3d11.h>
#include <d3dcompiler.h>
#include <stdexcept>
#include <wrl/client.h>
using Microsoft::WRL::ComPtr;
using namespace ttwvrnr;
void Check(HRESULT hr) {
  if (FAILED(hr))
    throw std::runtime_error("host D3D operation");
}
void Wait(ID3D11Device *d, ID3D11DeviceContext *c) {
  ComPtr<ID3D11Query> q;
  D3D11_QUERY_DESC desc{D3D11_QUERY_EVENT, 0};
  Check(d->CreateQuery(&desc, &q));
  c->End(q.Get());
  c->Flush();
  auto start = GetTickCount64();
  HRESULT r;
  while ((r = c->GetData(q.Get(), nullptr, 0, 0)) == S_FALSE) {
    if (GetTickCount64() - start > 5000)
      throw std::runtime_error("host GPU timeout");
    Sleep(0);
  }
  Check(r);
}
int wmain(int argc, wchar_t **argv) {
  if (argc != 2)
    return 2;
  SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
  std::wstring name = argv[1];
  HANDLE mapping = OpenFileMappingW(FILE_MAP_ALL_ACCESS, FALSE, name.c_str());
  if (!mapping)
    return 3;
  auto *p = static_cast<HostProtocol *>(
      MapViewOfFile(mapping, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(HostProtocol)));
  if (!p || p->magic != 0x54544e52 || p->version != 1)
    return 3;
  HANDLE request = OpenEventW(SYNCHRONIZE, FALSE, (name + L"-request").c_str());
  HANDLE reply =
      OpenEventW(EVENT_MODIFY_STATE, FALSE, (name + L"-reply").c_str());
  HANDLE parent = OpenProcess(SYNCHRONIZE, FALSE, p->parentPid);
  if (!request || !reply || !parent)
    return 3;
  try {
    if (!p->width || !p->height || uint64_t(p->width) * p->height > 16000000)
      throw std::runtime_error("host dimensions");
    ComPtr<ID3D11Device> d;
    ComPtr<ID3D11DeviceContext> c;
    Check(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0,
                            nullptr, 0, D3D11_SDK_VERSION, &d, nullptr, &c));
    ComPtr<ID3D11Texture2D> hdr, packed, bloom;
    Check(d->OpenSharedResource(
        reinterpret_cast<HANDLE>(uintptr_t(p->colorHandle)),
        IID_PPV_ARGS(&hdr)));
    Check(d->OpenSharedResource(
        reinterpret_cast<HANDLE>(uintptr_t(p->depthHandle)),
        IID_PPV_ARGS(&packed)));
    Check(d->OpenSharedResource(
        reinterpret_cast<HANDLE>(uintptr_t(p->bloomHandle)),
        IID_PPV_ARGS(&bloom)));
    D3D11_TEXTURE2D_DESC td{};
    hdr->GetDesc(&td);
    if (td.Width != p->width || td.Height != p->height ||
        td.Format != DXGI_FORMAT_R16G16B16A16_FLOAT)
      throw std::runtime_error("host HDR description");
    packed->GetDesc(&td);
    if (td.Width != p->width || td.Height != p->height ||
        td.Format != DXGI_FORMAT_R8G8B8A8_UNORM)
      throw std::runtime_error("host packed depth description");
    td.MiscFlags = 0;
    td.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
    td.Format = DXGI_FORMAT_R32_FLOAT;
    ComPtr<ID3D11Texture2D> depth, motion, exposure;
    Check(d->CreateTexture2D(&td, nullptr, &depth));
    td.Format = DXGI_FORMAT_R16G16_FLOAT;
    Check(d->CreateTexture2D(&td, nullptr, &motion));
    td.Width = td.Height = 1;
    td.Format = DXGI_FORMAT_R16_FLOAT;
    Check(d->CreateTexture2D(&td, nullptr, &exposure));
    ComPtr<ID3D11ShaderResourceView> bloomSrv;
    Check(d->CreateShaderResourceView(bloom.Get(), nullptr, &bloomSrv));
    ComPtr<ID3D11ShaderResourceView> input;
    Check(d->CreateShaderResourceView(packed.Get(), nullptr, &input));
    ComPtr<ID3D11UnorderedAccessView> depthUav, motionUav, exposureUav;
    Check(d->CreateUnorderedAccessView(depth.Get(), nullptr, &depthUav));
    Check(d->CreateUnorderedAccessView(motion.Get(), nullptr, &motionUav));
    Check(d->CreateUnorderedAccessView(exposure.Get(), nullptr, &exposureUav));
    const float zero[4]{};
    c->ClearUnorderedAccessViewFloat(motionUav.Get(), zero);
    const char *source =
        "cbuffer Constants:register(b0){float hdrParam;};Texture2D<float4> "
        "packed:register(t0);Texture2D<float4> "
        "bloom:register(t1);RWTexture2D<float> "
        "depth:register(u0);RWTexture2D<float> "
        "exposure:register(u1);[numthreads(8,8,1)]void main(uint3 "
        "p:SV_DispatchThreadID){uint "
        "w,h;depth.GetDimensions(w,h);if(p.x>=w||p.y>=h)return;float3 "
        "b=round(packed.Load(int3(p.xy,0)).rgb*255);depth[p.xy]=dot(b,float3(1,"
        "256,65536))/"
        "16777215.0;if(p.x==0&&p.y==0)exposure[uint2(0,0)]=hdrParam/"
        "max(bloom.Load(int3(0,0,0)).a,hdrParam);}";
    ComPtr<ID3DBlob> code, errors;
    Check(D3DCompile(source, strlen(source), nullptr, nullptr, nullptr, "main",
                     "cs_5_0", D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &code,
                     &errors));
    ComPtr<ID3D11ComputeShader> decode;
    Check(d->CreateComputeShader(code->GetBufferPointer(),
                                 code->GetBufferSize(), nullptr, &decode));
    D3D11_BUFFER_DESC cbDesc{};
    cbDesc.ByteWidth = 16;
    cbDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    ComPtr<ID3D11Buffer> constants;
    Check(d->CreateBuffer(&cbDesc, nullptr, &constants));
    NeuralBackend backend;
    NeuralOptions options;
    options.intensity = p->intensity;
    options.passthrough = p->identity != 0;
    options.dlaa = false;
    options.resolutionPercent = 100;
    if (!backend.Init(d.Get(), p->width, p->height, Directory() + L"\\runtime",
                      options))
      throw std::runtime_error("host NR initialization");
    p->result = 1;
    SetEvent(reply);
    Log("host_ready spatial_only=1 native_resolution=1 "
        "reset_every_evaluation=1 identity=%u",
        p->identity);
    HANDLE waits[] = {parent, request};
    while (true) {
      auto status = WaitForMultipleObjects(2, waits, FALSE, 60000);
      if (status == WAIT_OBJECT_0)
        break;
      if (status == WAIT_TIMEOUT)
        continue;
      if (status != WAIT_OBJECT_0 + 1)
        break;
      if (p->command == 2)
        break;
      p->result = 0;
      if (p->command != 1 || p->eye > 1 || !std::isfinite(p->hdrParam) ||
          p->hdrParam <= 0)
        throw std::runtime_error("host request");
      float parameters[4] = {p->hdrParam, 0, 0, 0};
      c->UpdateSubresource(constants.Get(), 0, nullptr, parameters, 0, 0);
      auto *cb = constants.Get();
      c->CSSetConstantBuffers(0, 1, &cb);
      ID3D11ShaderResourceView *srvs[] = {input.Get(), bloomSrv.Get()};
      ID3D11UnorderedAccessView *uavs[] = {depthUav.Get(), exposureUav.Get()};
      c->CSSetShader(decode.Get(), nullptr, 0);
      c->CSSetShaderResources(0, 2, srvs);
      c->CSSetUnorderedAccessViews(0, 2, uavs, nullptr);
      c->Dispatch((p->width + 7) / 8, (p->height + 7) / 8, 1);
      srvs[0] = srvs[1] = nullptr;
      uavs[0] = uavs[1] = nullptr;
      c->CSSetShaderResources(0, 2, srvs);
      c->CSSetUnorderedAccessViews(0, 2, uavs, nullptr);
      auto start = GetTickCount64();
      if (!backend.Render(hdr.Get(), depth.Get(), motion.Get(), exposure.Get(),
                          p->eye, p->frame, true))
        throw std::runtime_error("host evaluate");
      Wait(d.Get(), c.Get());
      p->result = 1;
      SetEvent(reply);
      if (p->frame % 120 == 0)
        Log("host_completed frame=%llu eye=%u gpu_wait_included_ms=%llu "
            "spatial_only=1",
            (unsigned long long)p->frame, p->eye,
            (unsigned long long)(GetTickCount64() - start));
    }
    backend.Shutdown();
  } catch (const std::exception &e) {
    Log("host_failed reason=%s bypass=1", e.what());
    p->result = 2;
    SetEvent(reply);
    return 4;
  }
  UnmapViewOfFile(p);
  CloseHandle(mapping);
  CloseHandle(request);
  CloseHandle(reply);
  CloseHandle(parent);
  return 0;
}
