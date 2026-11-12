// SPDX-License-Identifier: GPL-3.0-only
#include "../src/HostProtocol.h"
#include "../src/GpuTimeline.h"
#include "../src/LiveSettings.h"
#include "../src/Support.h"
#include "../src/backend/NeuralBackend.h"
#include <algorithm>
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
// Reuse the completion query. Once explicitly flushed, polling must not
// flush again: the reply still waits for every final D3D11 write to complete.
class GpuCompletion {
  ComPtr<ID3D11Query> query;
  ComPtr<ID3D11DeviceContext> context;
public:
  GpuCompletion(ID3D11Device *d, ID3D11DeviceContext *c) : context(c) {
    D3D11_QUERY_DESC desc{D3D11_QUERY_EVENT, 0};
    Check(d->CreateQuery(&desc, &query));
  }
  void Wait() {
    context->End(query.Get());
    context->Flush();
    const auto start = GetTickCount64();
    HRESULT result;
    while ((result = context->GetData(query.Get(), nullptr, 0,
                                     D3D11_ASYNC_GETDATA_DONOTFLUSH)) == S_FALSE) {
      if (GetTickCount64() - start > 5000)
        throw std::runtime_error("host GPU timeout");
      Sleep(0);
    }
    Check(result);
  }
};

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
  if (!p || p->magic != 0x54544e52 || p->version != 4)
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
    LiveSettings settings;
    settings.neural = p->neural; settings.dlaa = p->dlaa;
    settings.temporal = p->temporal; settings.identity = p->identity;
    settings.profile = p->profile; settings.resolution = p->resolution;
    settings.preset = p->preset; settings.strength = p->strength;
    settings.tone = p->tone; settings.detail = p->detail;
    if (!settings.Valid()) throw std::runtime_error("host invalid settings snapshot");
    ComPtr<ID3D11Device> d;
    ComPtr<ID3D11DeviceContext> c;
    Check(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0,
                            nullptr, 0, D3D11_SDK_VERSION, &d, nullptr, &c));
    GpuCompletion completion(d.Get(), c.Get());
    GpuTimeline *timeline = nullptr;
    if (p->inputTimeline && p->outputTimeline) {
      timeline = new GpuTimeline;
      timeline->Init(d.Get(), p->parentPid, p->inputTimeline, p->outputTimeline);
      Log("gpu_handoff_host_ready mode=opaque_timeline");
    }
    ComPtr<ID3D11Texture2D> hdr, packed, bloom, objectMotion;
    Check(d->OpenSharedResource(
        reinterpret_cast<HANDLE>(uintptr_t(p->colorHandle)),
        IID_PPV_ARGS(&hdr)));
    Check(d->OpenSharedResource(
        reinterpret_cast<HANDLE>(uintptr_t(p->depthHandle)),
        IID_PPV_ARGS(&packed)));
    Check(d->OpenSharedResource(
        reinterpret_cast<HANDLE>(uintptr_t(p->bloomHandle)),
        IID_PPV_ARGS(&bloom)));
    Check(d->OpenSharedResource(
        reinterpret_cast<HANDLE>(uintptr_t(p->motionHandle)),
        IID_PPV_ARGS(&objectMotion)));
    D3D11_TEXTURE2D_DESC td{};
    hdr->GetDesc(&td);
    if (td.Width != p->width || td.Height != p->height ||
        td.Format != DXGI_FORMAT_R16G16B16A16_FLOAT)
      throw std::runtime_error("host HDR description");
    packed->GetDesc(&td);
    if (td.Width != p->width || td.Height != p->height ||
        td.Format != DXGI_FORMAT_R8G8B8A8_UNORM)
      throw std::runtime_error("host packed depth description");
    objectMotion->GetDesc(&td);
    if (td.Width != p->width || td.Height != p->height ||
        td.Format != DXGI_FORMAT_R16G16B16A16_FLOAT)
      throw std::runtime_error("host motion description");
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
    ComPtr<ID3D11ShaderResourceView> objectSrv;
    Check(d->CreateShaderResourceView(objectMotion.Get(), nullptr, &objectSrv));
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
    const char *source = R"(
cbuffer Constants:register(b0) {
  float hdrParam; uint width; uint height; uint temporal;
  row_major float4x4 clipToPrevious;
  float2 jitter; uint reset; uint padding;
};
Texture2D<float4> packed:register(t0);
Texture2D<float4> bloom:register(t1);
Texture2D<float4> objectMotion:register(t2);
RWTexture2D<float> depth:register(u0);
RWTexture2D<float> exposure:register(u1);
RWTexture2D<float2> motion:register(u2);
[numthreads(8,8,1)]void main(uint3 p:SV_DispatchThreadID) {
 if(p.x>=width||p.y>=height)return;
 float3 b=round(packed.Load(int3(p.xy,0)).rgb*255);
 float z=dot(b,float3(1,256,65536))/16777215.0;
 depth[p.xy]=z;
 float2 mv=0;
 if(temporal && !reset) {
   float2 center=float2(p.xy)+jitter;
   float4 clip=float4(center*float2(2.0/width,-2.0/height)+float2(-1,1),z,1);
   float4 previous=mul(clip,clipToPrevious);
   if(previous.w>1e-6) {
     float2 pixel=(previous.xy/previous.w*float2(.5,-.5)+.5)*float2(width,height);
     mv=pixel-center;
   }
   float4 object=objectMotion.Load(int3(p.xy,0));
   if(object.z>.5)mv=object.xy;
 }
 motion[p.xy]=mv;
 if(p.x==0&&p.y==0)exposure[uint2(0,0)]=hdrParam/max(bloom.Load(int3(0,0,0)).a,hdrParam);
})";
    ComPtr<ID3DBlob> code, errors;
    Check(D3DCompile(source, strlen(source), nullptr, nullptr, nullptr, "main",
                     "cs_5_0", D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &code,
                     &errors));
    ComPtr<ID3D11ComputeShader> decode;
    Check(d->CreateComputeShader(code->GetBufferPointer(),
                                 code->GetBufferSize(), nullptr, &decode));
    D3D11_BUFFER_DESC cbDesc{};
    struct Constants {
      float hdrParam;
      uint32_t width, height, temporal;
      float clipToPrevious[16], jitterX, jitterY;
      uint32_t reset, padding;
    };
    static_assert(sizeof(Constants) == 96);
    cbDesc.ByteWidth = sizeof(Constants);
    cbDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    ComPtr<ID3D11Buffer> constants;
    Check(d->CreateBuffer(&cbDesc, nullptr, &constants));
    NeuralBackend backend;
    NeuralOptions options;
    // Immutable configuration snapshot published before this helper starts.
    options.resolutionPercent = p->resolution;
    options.intensity = p->strength / 100.f;
    options.tone = p->tone / 100.f;
    options.detail = p->detail / 100.f;
    options.preset = p->preset;
    options.profileGpu = p->profile != 0;
    options.passthrough = p->identity != 0;
    options.neural = p->neural != 0;
    options.dlaa = p->dlaa != 0;
    options.temporalGuides = p->temporal != 0;

    if (!backend.Init(d.Get(), p->width, p->height, Directory() + L"\\runtime",
                      options))
      throw std::runtime_error("host NR initialization");
    p->result = 1;
    SetEvent(reply);
    Log("host_ready native_resolution=1 neural=%u dlaa=%u temporal=%u "
        "identity=%u",
        p->neural, p->dlaa, p->temporal, p->identity);
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
      if (timeline) timeline->WaitInput(p->serial);
      backend.LogCompletedGpuTiming();
      Constants parameters{};
      parameters.hdrParam = p->hdrParam;
      parameters.width = p->width;
      parameters.height = p->height;
      parameters.temporal = p->temporal;
      parameters.reset = p->reset;
      parameters.jitterX = p->jitterX;
      parameters.jitterY = p->jitterY;
      std::memcpy(parameters.clipToPrevious, p->clipToPrevious,
                  sizeof(parameters.clipToPrevious));
      for (float v : parameters.clipToPrevious)
        if (!std::isfinite(v))
          throw std::runtime_error("host invalid camera");
      if (!std::isfinite(p->jitterX) || !std::isfinite(p->jitterY) ||
          std::abs(p->jitterX) > .5f || std::abs(p->jitterY) > .5f)
        throw std::runtime_error("host invalid jitter");
      c->UpdateSubresource(constants.Get(), 0, nullptr, &parameters, 0, 0);
      auto *cb = constants.Get();
      c->CSSetConstantBuffers(0, 1, &cb);
      ID3D11ShaderResourceView *srvs[] = {input.Get(), bloomSrv.Get(),
                                          objectSrv.Get()};
      ID3D11UnorderedAccessView *uavs[] = {depthUav.Get(), exposureUav.Get(),
                                           motionUav.Get()};
      c->CSSetShader(decode.Get(), nullptr, 0);
      c->CSSetShaderResources(0, 3, srvs);
      c->CSSetUnorderedAccessViews(0, 3, uavs, nullptr);
      c->Dispatch((p->width + 7) / 8, (p->height + 7) / 8, 1);
      srvs[0] = srvs[1] = srvs[2] = nullptr;
      uavs[0] = uavs[1] = uavs[2] = nullptr;
      c->CSSetShaderResources(0, 3, srvs);
      c->CSSetUnorderedAccessViews(0, 3, uavs, nullptr);
      auto start = GetTickCount64();
      if (!backend.Render(hdr.Get(), depth.Get(), motion.Get(), exposure.Get(),
                          p->eye, p->frame, p->reset != 0, p->jitterX,
                          p->jitterY))
        throw std::runtime_error("host evaluate");
      if (timeline) timeline->SignalOutput(p->serial);
      else completion.Wait();
      backend.LogCompletedGpuTiming();
      // The client may overwrite the shared request as soon as reply is set.
      const auto completedFrame = p->frame;
      const auto completedEye = p->eye;
      const auto elapsed = GetTickCount64() - start;
      p->result = 1;
      SetEvent(reply);
      if (completedFrame % 120 == 0 && timeline)
        Log("host_submitted frame=%llu eye=%u cpu_submission_ms=%llu gpu_completion=queued",
            (unsigned long long)completedFrame, completedEye, (unsigned long long)elapsed);
      else if (completedFrame % 120 == 0)
        Log("host_completed frame=%llu eye=%u gpu_wait_included_ms=%llu "
            "temporal=%u dlaa=%u",
            (unsigned long long)completedFrame, completedEye,
            (unsigned long long)elapsed, options.temporalGuides,
            options.dlaa);
    }
    completion.Wait();
    backend.LogCompletedGpuTiming();
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
