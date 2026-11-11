// SPDX-License-Identifier: GPL-3.0-only
#include "../src/HostProtocol.h"
#include "../src/LiveSettings.h"
#include "../src/Support.h"
#include "../src/backend/NeuralBackend.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <d3d11_4.h>
#include <d3dcompiler.h>
#include <stdexcept>
#include <wrl/client.h>
using Microsoft::WRL::ComPtr;
using namespace ttwvrnr;
void Check(HRESULT hr) {
  if (FAILED(hr))
    throw std::runtime_error("host D3D operation");
}
// Completion stays inside the 64-bit helper. No cross-process Wine fence
// import. Reuse a monotonic fence rather than creating and polling an event
// query for every eye. The reply is still withheld until all output writes
// complete.
class Completion {
  ComPtr<ID3D11DeviceContext4> context;
  ComPtr<ID3D11Fence> fence;
  HANDLE event = nullptr;
  uint64_t serial = 0;

public:
  ~Completion() {
    if (event)
      CloseHandle(event);
  }
  void Init(ID3D11Device *d, ID3D11DeviceContext *c) {
    ComPtr<ID3D11Device5> device;
    Check(d->QueryInterface(IID_PPV_ARGS(&device)));
    Check(c->QueryInterface(IID_PPV_ARGS(&context)));
    Check(device->CreateFence(0, D3D11_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence)));
    event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    if (!event)
      throw std::runtime_error("host completion event");
  }
  void Wait() {
    const auto value = ++serial;
    Check(context->Signal(fence.Get(), value));
    context->Flush();
    if (fence->GetCompletedValue() < value) {
      Check(fence->SetEventOnCompletion(value, event));
      if (WaitForSingleObject(event, 5000) != WAIT_OBJECT_0)
        throw std::runtime_error("host GPU timeout");
    }
  }
};
class HostGpuTiming {
  ComPtr<ID3D11Query> disjoint, stamps[3];
  bool sample = false;

public:
  void Init(ID3D11Device *d) {
    D3D11_QUERY_DESC q{D3D11_QUERY_TIMESTAMP_DISJOINT, 0};
    Check(d->CreateQuery(&q, &disjoint));
    q.Query = D3D11_QUERY_TIMESTAMP;
    for (auto &t : stamps)
      Check(d->CreateQuery(&q, &t));
  }
  void Begin(ID3D11DeviceContext *c, bool enabled) {
    sample = enabled;
    if (sample) {
      c->Begin(disjoint.Get());
      c->End(stamps[0].Get());
    }
  }
  void Mark(ID3D11DeviceContext *c, unsigned n) {
    if (sample)
      c->End(stamps[n].Get());
    if (sample && n == 2)
      c->End(disjoint.Get());
  }
  void Collect(ID3D11DeviceContext *c, uint64_t frame, unsigned eye) {
    if (!sample)
      return;
    D3D11_QUERY_DATA_TIMESTAMP_DISJOINT data{};
    UINT64 t[3]{};
    if (c->GetData(disjoint.Get(), &data, sizeof(data),
                   D3D11_ASYNC_GETDATA_DONOTFLUSH) != S_OK ||
        data.Disjoint || !data.Frequency)
      return;
    for (unsigned i = 0; i < 3; ++i)
      if (c->GetData(stamps[i].Get(), &t[i], sizeof(t[i]),
                     D3D11_ASYNC_GETDATA_DONOTFLUSH) != S_OK)
        return;
    if (t[0] > t[1] || t[1] > t[2])
      return;
    Log("host_gpu_span frame=%llu eye=%u decode_ms=%.4f backend_ms=%.4f",
        (unsigned long long)frame, eye,
        double(t[1] - t[0]) * 1000 / data.Frequency,
        double(t[2] - t[1]) * 1000 / data.Frequency);
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
    settings.sr = p->sr;
    settings.neural = p->neural;
    settings.dlaa = p->dlaa;
    settings.temporal = p->temporal;
    settings.identity = p->identity;
    settings.profile = p->profile;
    settings.resolution = p->resolution;
    settings.preset = p->preset;
    settings.strength = p->strength;
    settings.tone = p->tone;
    settings.detail = p->detail;
    if (!settings.Valid())
      throw std::runtime_error("host invalid settings snapshot");
    ComPtr<ID3D11Device> d;
    ComPtr<ID3D11DeviceContext> c;
    Check(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0,
                            nullptr, 0, D3D11_SDK_VERSION, &d, nullptr, &c));
    HostGpuTiming hostTiming;
    if (settings.profile)
      hostTiming.Init(d.Get());
    Completion completion;
    completion.Init(d.Get(), c.Get());
    ComPtr<ID3D11Texture2D> hdr, packed, bloom, objectMotion, output;
    if (p->sr) {
      if (p->outputWidth != 2944 || p->outputHeight != 2208)
        throw std::runtime_error("SR output dimensions");
      Check(d->OpenSharedResource(
          reinterpret_cast<HANDLE>(uintptr_t(p->outputHandle)),
          IID_PPV_ARGS(&output)));
      D3D11_TEXTURE2D_DESC od{};
      output->GetDesc(&od);
      if (od.Width != p->outputWidth || od.Height != p->outputHeight ||
          od.Format != DXGI_FORMAT_R16G16B16A16_FLOAT)
        throw std::runtime_error("SR output texture");
    }
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
    options.sr = p->sr != 0;
    options.outputWidth = p->outputWidth;
    options.outputHeight = p->outputHeight;
    options.temporalGuides = p->temporal != 0;

    if (!backend.Init(d.Get(), p->width, p->height, Directory() + L"\\runtime",
                      options))
      throw std::runtime_error("host NR initialization");
    p->result = 1;
    SetEvent(reply);
    Log("host_ready neural=%u dlaa=%u temporal=%u "
        "identity=%u sr=%u input=%ux%u output=%ux%u",
        p->neural, p->dlaa, p->temporal, p->identity, p->sr, p->width,
        p->height, p->outputWidth, p->outputHeight);
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
      hostTiming.Begin(c.Get(), settings.profile && p->frame % 120 == 0);
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
      hostTiming.Mark(c.Get(), 1);
      const auto start = std::chrono::steady_clock::now();
      if (!backend.Render(hdr.Get(), depth.Get(), motion.Get(), exposure.Get(),
                          p->eye, p->frame, p->reset != 0, p->jitterX,
                          p->jitterY, output.Get()))
        throw std::runtime_error("host evaluate");
      hostTiming.Mark(c.Get(), 2);
      const auto submitted = std::chrono::steady_clock::now();
      completion.Wait();
      const auto completed = std::chrono::steady_clock::now();
      hostTiming.Collect(c.Get(), p->frame, p->eye);
      backend.LogCompletedGpuTiming();
      // The client may overwrite the shared request as soon as reply is set.
      const auto completedFrame = p->frame;
      const auto completedEye = p->eye;
      const auto elapsed =
          std::chrono::duration<double, std::milli>(completed - start).count();
      const auto recordMs =
          std::chrono::duration<double, std::milli>(submitted - start).count();
      const auto waitMs =
          std::chrono::duration<double, std::milli>(completed - submitted)
              .count();
      p->result = 1;
      SetEvent(reply);
      if (completedFrame % 120 == 0)
        Log("host_completed frame=%llu eye=%u gpu_wait_included_ms=%.3f "
            "record_ms=%.3f completion_ms=%.3f "
            "temporal=%u dlaa=%u",
            (unsigned long long)completedFrame, completedEye, elapsed, recordMs,
            waitMs, options.temporalGuides, options.dlaa);
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
