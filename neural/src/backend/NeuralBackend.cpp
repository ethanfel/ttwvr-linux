// SPDX-License-Identifier: GPL-3.0-only
// Adapted from Ethanfel TF2VRNeuralRendering; see THIRD-PARTY-NOTICES.md.
#include "NeuralBackend.h"
#include "../Support.h"
#include "GpuInterop.h"
#include "GpuTiming.h"
#include "NgxLifetime.h"
#include <algorithm>
#include <array>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <d3dcompiler.h>
#include <filesystem>
#include <nvsdk_ngx_defs.h>
#include <nvsdk_ngx_defs_dlssnr.h>
#include <stdexcept>
extern "C" unsigned int ttwvrnr_parameter_get_uint(void *, const char *,
                                                   unsigned int *);
extern "C" unsigned int ttwvrnr_parameter_get_pointer(void *, const char *,
                                                      void **);
extern "C" unsigned int ttwvrnr_parameter_get_int(void *, const char *, int *);
extern "C" void ttwvrnr_parameter_set_int(void *, const char *, int);
extern "C" void ttwvrnr_parameter_set_uint(void *, const char *, unsigned int);
extern "C" void ttwvrnr_parameter_set_float(void *, const char *, float);
extern "C" void ttwvrnr_parameter_set_resource(void *, const char *,
                                               ID3D12Resource *);
namespace ttwvrnr {
using Microsoft::WRL::ComPtr;
namespace {
void Check(HRESULT r, const char *what) {
  if (FAILED(r)) {
    Log("neural_failure operation=%s hr=0x%08lx", what, (unsigned long)r);
    throw std::runtime_error(what);
  }
}
void Ngx(NVSDK_NGX_Result r, const char *what) {
  if (r != NVSDK_NGX_Result_Success) {
    Log("neural_failure operation=%s ngx=0x%08x", what, (unsigned)r);
    throw std::runtime_error(what);
  }
}
// The model receives an opaque, display-referred sRGB proxy. A uniform
// shoulder preserves hue; applying the bounded linear-light neural gain to
// the original HDR preserves its highlight range and alpha. Identical model
// input/output is an identity operation, including over-range highlights.
constexpr const char *Shader = R"(
cbuffer Dimensions : register(b0) { uint2 fullSize; uint2 modelSize; float2 guideOffset; float2 guideMotionCorrection; uint2 outputSize; uint2 padding; };
Texture2D<float4> sourceColor : register(t0);
Texture2D<float> sourceDepth : register(t1);
Texture2D<float2> sourceMotion : register(t2);
Texture2D<float> exposure : register(t3);
Texture2D<float4> neuralOutput : register(t4);
Texture2D<float4> neuralInput : register(t5);
SamplerState linearClamp : register(s0);
RWTexture2D<float4> colorOut : register(u0);
RWTexture2D<float> depthOut : register(u1);
RWTexture2D<float2> motionOut : register(u2);
float3 encode(float3 x) { return lerp(1.055*pow(max(x,0),1.0/2.4)-0.055,12.92*x,x<=0.0031308); }
float3 decode(float3 x) { return lerp(pow(max((x+0.055)/1.055,0),2.4),x/12.92,x<=0.04045); }
[numthreads(8,8,1)] void Prepare(uint3 p:SV_DispatchThreadID) {
 if(any(p.xy>=modelSize)) return;
 float2 uv=(p.xy+0.5)/modelSize;
 uint2 q=uint2(clamp(uv*fullSize+guideOffset,0,float2(fullSize)-1));
 float3 c=max(sourceColor.SampleLevel(linearClamp,uv,0).rgb,0);
 c*=clamp(exposure.Load(int3(0,0,0)),0.0001,10000.0);
 float peak=max(c.r,max(c.g,c.b));
 c/=1.0+peak;
 colorOut[p.xy]=float4(encode(c),1);
 depthOut[p.xy]=sourceDepth.Load(int3(q,0));
 motionOut[p.xy]=sourceMotion.Load(int3(q,0))*(float2(modelSize)/float2(fullSize))
     +guideMotionCorrection*float2(modelSize)/float2(fullSize);
}
[numthreads(8,8,1)] void Resolve(uint3 p:SV_DispatchThreadID) {
 if(any(p.xy>=fullSize)) return;
 float2 uv=(p.xy+0.5)/fullSize;
 float4 original=sourceColor.Load(asint(p));
 float3 a=decode(neuralInput.SampleLevel(linearClamp,uv,0).rgb);
 float3 b=decode(neuralOutput.SampleLevel(linearClamp,uv,0).rgb);
 float3 gain=clamp(b/max(a,0.0001),0.25,4.0);
 gain=lerp(gain,1.0,a<0.0001);
 colorOut[p.xy]=float4(original.rgb*gain,original.a);
}
[numthreads(8,8,1)] void PrepareAA(uint3 p:SV_DispatchThreadID) {
 if(any(p.xy>=fullSize)) return;
 colorOut[p.xy]=float4(max(sourceColor.Load(asint(p)).rgb,0),1);
 depthOut[p.xy]=sourceDepth.Load(asint(p));
 motionOut[p.xy]=sourceMotion.Load(asint(p));
}
[numthreads(8,8,1)] void ResolveAA(uint3 p:SV_DispatchThreadID) {
 if(any(p.xy>=outputSize)) return;
 float alpha=all(outputSize==fullSize)?sourceColor.Load(asint(p)).a:sourceColor.SampleLevel(linearClamp,(p.xy+0.5)/outputSize,0).a;
 colorOut[p.xy]=float4(neuralOutput.Load(asint(p)).rgb,alpha);
}
)";
// Only compute bindings and OM are changed. Preserve every touched slot,
// including dynamic shader class instances and UAV counters (-1 retains them).
struct ContextState {
  ID3D11DeviceContext *c;
  ID3D11ComputeShader *cs = nullptr;
  ID3D11ClassInstance *classes[256]{};
  UINT count = 256;
  ID3D11ShaderResourceView *srvs[6]{};
  ID3D11UnorderedAccessView *uavs[3]{};
  ID3D11Buffer *cb = nullptr;
  ID3D11SamplerState *sampler = nullptr;
  ID3D11RenderTargetView *rt[8]{};
  ID3D11DepthStencilView *ds = nullptr;
  explicit ContextState(ID3D11DeviceContext *ctx) : c(ctx) {
    c->CSGetShader(&cs, classes, &count);
    c->CSGetShaderResources(0, 6, srvs);
    c->CSGetUnorderedAccessViews(0, 3, uavs);
    c->CSGetConstantBuffers(0, 1, &cb);
    c->CSGetSamplers(0, 1, &sampler);
    c->OMGetRenderTargets(8, rt, &ds);
    c->OMSetRenderTargets(0, nullptr, nullptr);
  }
  void Unbind() {
    ID3D11ShaderResourceView *nulls[6]{};
    ID3D11UnorderedAccessView *nu[3]{};
    c->CSSetShaderResources(0, 6, nulls);
    c->CSSetUnorderedAccessViews(0, 3, nu, nullptr);
  }
  ~ContextState() {
    Unbind();
    c->CSSetShader(cs, classes, count);
    c->CSSetConstantBuffers(0, 1, &cb);
    c->CSSetSamplers(0, 1, &sampler);
    c->OMSetRenderTargets(8, rt, ds);
    c->CSSetShaderResources(0, 6, srvs);
    UINT counts[3] = {~0u, ~0u, ~0u};
    c->CSSetUnorderedAccessViews(0, 3, uavs, counts);
    if (cs)
      cs->Release();
    if (cb)
      cb->Release();
    if (sampler)
      sampler->Release();
    if (ds)
      ds->Release();
    for (auto *p : srvs)
      if (p)
        p->Release();
    for (auto *p : uavs)
      if (p)
        p->Release();
    for (auto *p : rt)
      if (p)
        p->Release();
    for (UINT i = 0; i < count; ++i)
      if (classes[i])
        classes[i]->Release();
  }
};
} // namespace
struct NeuralBackend::Impl {
  GpuInterop gpu;
  GpuTiming timing;
  ComPtr<ID3D11Device> device;
  ComPtr<ID3D11ComputeShader> prepare, resolve, prepareAA, resolveAA;
  ComPtr<ID3D11Buffer> dimensions;
  ComPtr<ID3D11SamplerState> sampler;
  unsigned fullW = 0, fullH = 0, w = 0, h = 0, outW = 0, outH = 0;
  NeuralOptions options;
  HMODULE core = nullptr;
  bool initialized = false, failed = false;
  using Allocate = NVSDK_NGX_Result(NVSDK_CONV *)(NVSDK_NGX_Parameter **);
  using Create = NVSDK_NGX_Result(NVSDK_CONV *)(ID3D12GraphicsCommandList *,
                                                NVSDK_NGX_Feature,
                                                NVSDK_NGX_Parameter *,
                                                NVSDK_NGX_Handle **);
  using Evaluate = NVSDK_NGX_Result(NVSDK_CONV *)(
      ID3D12GraphicsCommandList *, const NVSDK_NGX_Handle *,
      const NVSDK_NGX_Parameter *, PFN_NVSDK_NGX_ProgressCallback);
  using Release = NVSDK_NGX_Result(NVSDK_CONV *)(NVSDK_NGX_Handle *);
  using Destroy = NVSDK_NGX_Result(NVSDK_CONV *)(NVSDK_NGX_Parameter *);
  using ShutdownFn = NVSDK_NGX_Result(NVSDK_CONV *)(ID3D12Device *,
                                                    unsigned int *);
  Allocate allocate = nullptr;
  Create create = nullptr;
  Evaluate evaluate = nullptr;
  Release release = nullptr;
  Destroy destroy = nullptr;
  ShutdownFn shutdown = nullptr;
  NVSDK_NGX_Parameter *caps = nullptr;
  struct Eye {
    SharedTexture color, depth, motion, output;
    ComPtr<ID3D11Texture2D> resolved;
    ComPtr<ID3D11UnorderedAccessView> colorUav, depthUav, motionUav,
        resolvedUav;
    ComPtr<ID3D11ShaderResourceView> inputSrv, outputSrv;
    NVSDK_NGX_Parameter *params = nullptr;
    NVSDK_NGX_Handle *feature = nullptr;
    SharedTexture aaColor, aaDepth, aaMotion, aaOutput, aaExposure;
    ComPtr<ID3D11UnorderedAccessView> aaColorUav, aaDepthUav, aaMotionUav;
    ComPtr<ID3D11ShaderResourceView> aaOutputSrv;
    NVSDK_NGX_Parameter *aaParams = nullptr;
    NVSDK_NGX_Handle *aaFeature = nullptr;
    uint64_t previousFrame = ~0ull, evaluations = 0;
    float previousJitterX = 0, previousJitterY = 0;
  };
  std::array<Eye, 2> eyes;
  template <class T> void Proc(T &p, const char *name) {
    p = reinterpret_cast<T>(GetProcAddress(core, name));
    if (!p)
      throw std::runtime_error(name);
  }
  ComPtr<ID3D11ShaderResourceView> Srv(ID3D11Texture2D *t, DXGI_FORMAT format) {
    ComPtr<ID3D11ShaderResourceView> v;
    D3D11_SHADER_RESOURCE_VIEW_DESC d{};
    d.Format = format;
    d.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
    d.Texture2D.MipLevels = 1;
    Check(device->CreateShaderResourceView(t, &d, &v), "srv");
    return v;
  }
  void Uav(ID3D11Texture2D *t, ComPtr<ID3D11UnorderedAccessView> &v) {
    Check(device->CreateUnorderedAccessView(t, nullptr, &v), "uav");
  }
  bool Init(ID3D11Device *d, unsigned width, unsigned height,
            const std::wstring &directory, const NeuralOptions &opt) {
    if ((opt.dlaa || opt.sr) && !opt.temporalGuides)
      throw std::runtime_error(
          "TTW DLAA requires verified motion and jitter; disabled");
    device = d;
    fullW = width;
    fullH = height;
    options = opt;
    outW = opt.sr ? opt.outputWidth : width;
    outH = opt.sr ? opt.outputHeight : height;
    if (opt.sr && (opt.neural || opt.dlaa || opt.passthrough || outW <= width ||
                   outH <= height))
      throw std::runtime_error(
          "SR needs exclusive lower-resolution scene input");
    w = std::max(32u,
                 width * std::clamp(opt.resolutionPercent, 25u, 100u) / 100);
    h = std::max(32u,
                 height * std::clamp(opt.resolutionPercent, 25u, 100u) / 100);
    if (!gpu.Init(d))
      return false;
    if (opt.profileGpu)
      timing.Init(gpu, opt.sr);
    for (auto pair : {std::pair<const char *, ID3D11ComputeShader **>{
                          "Prepare", prepare.GetAddressOf()},
                      {"Resolve", resolve.GetAddressOf()},
                      {"PrepareAA", prepareAA.GetAddressOf()},
                      {"ResolveAA", resolveAA.GetAddressOf()}}) {
      ComPtr<ID3DBlob> code, errors;
      auto hr = D3DCompile(Shader, std::strlen(Shader), "TTWVR NR conversions",
                           nullptr, nullptr, pair.first, "cs_5_0",
                           D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &code, &errors);
      if (FAILED(hr) && errors)
        Log("neural_shader_error %s",
            static_cast<char *>(errors->GetBufferPointer()));
      Check(hr, "compile");
      Check(d->CreateComputeShader(code->GetBufferPointer(),
                                   code->GetBufferSize(), nullptr, pair.second),
            "shader");
    }
    struct Dimensions {
      UINT fullW, fullH, w, h;
      float offsetX, offsetY, motionX, motionY;
      UINT outW, outH, pad0, pad1;
    };
    Dimensions dims{fullW, fullH, w, h, 0, 0, 0, 0, outW, outH, 0, 0};
    D3D11_BUFFER_DESC bd{};
    bd.ByteWidth = sizeof(dims);
    bd.Usage = D3D11_USAGE_DEFAULT;
    bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    D3D11_SUBRESOURCE_DATA initial{&dims, 0, 0};
    Check(d->CreateBuffer(&bd, &initial, &dimensions), "dimensions");
    D3D11_SAMPLER_DESC sd{};
    sd.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    sd.AddressU = sd.AddressV = sd.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    sd.MaxLOD = D3D11_FLOAT32_MAX;
    Check(d->CreateSamplerState(&sd, &sampler), "sampler");
    if (!opt.passthrough) {
      const auto runtime = directory + L"\\nvngx_dlssnr.dll";
      if (opt.neural && !std::filesystem::exists(runtime))
        throw std::runtime_error("nvngx_dlssnr.dll missing");
      if ((opt.dlaa || opt.sr) &&
          !std::filesystem::exists(directory + L"\\nvngx_dlss.dll"))
        throw std::runtime_error("nvngx_dlss.dll missing");
      core = LoadLibraryW(L"_nvngx.dll");
      if (!core)
        throw std::runtime_error("driver core missing");
      using Init = NVSDK_NGX_Result(NVSDK_CONV *)(
          const char *, NVSDK_NGX_EngineType, const char *, const wchar_t *,
          ID3D12Device *, NVSDK_NGX_Version,
          const NVSDK_NGX_FeatureCommonInfo *);
      Init init = nullptr;
      Allocate capabilities = nullptr;
      Proc(init, "NVSDK_NGX_D3D12_Init_ProjectID");
      Proc(capabilities, "NVSDK_NGX_D3D12_GetCapabilityParameters");
      Proc(allocate, "NVSDK_NGX_D3D12_AllocateParameters");
      Proc(create, "NVSDK_NGX_D3D12_CreateFeature");
      Proc(evaluate, "NVSDK_NGX_D3D12_EvaluateFeature");
      Proc(release, "NVSDK_NGX_D3D12_ReleaseFeature");
      Proc(destroy, "NVSDK_NGX_D3D12_DestroyParameters");
      Proc(shutdown, "NVSDK_NGX_D3D12_Shutdown1");
      const wchar_t *search = directory.c_str();
      NVSDK_NGX_FeatureCommonInfo info{};
      info.PathListInfo.Path = &search;
      info.PathListInfo.Length = 1;
      Ngx(init("6fb77588-f5b7-4363-a8d3-4607354a043e",
               NVSDK_NGX_ENGINE_TYPE_CUSTOM, "TTWVR-NR-0.1",
               Directory().c_str(), gpu.Device12(), NVSDK_NGX_Version_API,
               &info),
          "init");
      initialized = true;
      JoinNgxRuntime(gpu.Device12());
      Ngx(capabilities(&caps), "capabilities");
      for (const char *key :
           {opt.neural ? "DLSSNR.Available" : nullptr,
            (opt.dlaa || opt.sr) ? "SuperSampling.Available" : nullptr}) {
        if (!key)
          continue;
        int available = 0;
        Ngx(static_cast<NVSDK_NGX_Result>(
                ttwvrnr_parameter_get_int(caps, key, &available)),
            "availability");
        if (!available)
          throw std::runtime_error(key);
      }
    }
    if (opt.sr) {
      void *callback = nullptr;
      Ngx(static_cast<NVSDK_NGX_Result>(ttwvrnr_parameter_get_pointer(
              caps, NVSDK_NGX_Parameter_DLSSOptimalSettingsCallback,
              &callback)),
          "sr_optimal_callback");
      if (!callback)
        throw std::runtime_error("SR optimal-settings callback unavailable");
      ttwvrnr_parameter_set_uint(caps, NVSDK_NGX_Parameter_Width, outW);
      ttwvrnr_parameter_set_uint(caps, NVSDK_NGX_Parameter_Height, outH);
      ttwvrnr_parameter_set_int(caps, NVSDK_NGX_Parameter_PerfQualityValue,
                                NVSDK_NGX_PerfQuality_Value_MaxQuality);
      ttwvrnr_parameter_set_int(caps, NVSDK_NGX_Parameter_RTXValue, 0);
      Ngx(reinterpret_cast<PFN_NVSDK_NGX_DLSS_GetOptimalSettingsCallback>(
              callback)(caps),
          "sr_optimal_settings");
      unsigned recommendedW = 0, recommendedH = 0;
      Ngx(static_cast<NVSDK_NGX_Result>(ttwvrnr_parameter_get_uint(
              caps, NVSDK_NGX_Parameter_OutWidth, &recommendedW)),
          "sr_optimal_width");
      Ngx(static_cast<NVSDK_NGX_Result>(ttwvrnr_parameter_get_uint(
              caps, NVSDK_NGX_Parameter_OutHeight, &recommendedH)),
          "sr_optimal_height");
      Log("sr_optimal_settings input=%ux%u recommended=%ux%u output=%ux%u "
          "quality=1",
          fullW, fullH, recommendedW, recommendedH, outW, outH);
      if (recommendedW != fullW || recommendedH != fullH)
        throw std::runtime_error(
            "SR scene dimensions do not match SDK Quality recommendation");
    }
    for (unsigned index = 0; index < 2; ++index) {
      auto &e = eyes[index];
      if (!gpu.CreateTexture(w, h, DXGI_FORMAT_R16G16B16A16_FLOAT, e.color) ||
          !gpu.CreateTexture(w, h, DXGI_FORMAT_R32_FLOAT, e.depth) ||
          !gpu.CreateTexture(w, h, DXGI_FORMAT_R16G16_FLOAT, e.motion) ||
          !gpu.CreateTexture(w, h, DXGI_FORMAT_R16G16B16A16_FLOAT, e.output))
        return false;
      Uav(e.color.d11.Get(), e.colorUav);
      Uav(e.depth.d11.Get(), e.depthUav);
      Uav(e.motion.d11.Get(), e.motionUav);
      e.inputSrv = Srv(e.color.d11.Get(), DXGI_FORMAT_R16G16B16A16_FLOAT);
      e.outputSrv = Srv(e.output.d11.Get(), DXGI_FORMAT_R16G16B16A16_FLOAT);
      D3D11_TEXTURE2D_DESC td{};
      td.Width = outW;
      td.Height = outH;
      td.MipLevels = td.ArraySize = td.SampleDesc.Count = 1;
      td.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
      td.BindFlags = D3D11_BIND_UNORDERED_ACCESS;
      Check(d->CreateTexture2D(&td, nullptr, &e.resolved), "resolved");
      Uav(e.resolved.Get(), e.resolvedUav);
      if ((opt.dlaa || opt.sr) && !opt.passthrough) {
        if (!gpu.CreateTexture(fullW, fullH, DXGI_FORMAT_R16G16B16A16_FLOAT,
                               e.aaColor) ||
            !gpu.CreateTexture(fullW, fullH, DXGI_FORMAT_R32_FLOAT,
                               e.aaDepth) ||
            !gpu.CreateTexture(fullW, fullH, DXGI_FORMAT_R16G16_FLOAT,
                               e.aaMotion) ||
            !gpu.CreateTexture(outW, outH, DXGI_FORMAT_R16G16B16A16_FLOAT,
                               e.aaOutput) ||
            !gpu.CreateTexture(1, 1, DXGI_FORMAT_R16_FLOAT, e.aaExposure))
          return false;
        Uav(e.aaColor.d11.Get(), e.aaColorUav);
        Uav(e.aaDepth.d11.Get(), e.aaDepthUav);
        Uav(e.aaMotion.d11.Get(), e.aaMotionUav);
        e.aaOutputSrv =
            Srv(e.aaOutput.d11.Get(), DXGI_FORMAT_R16G16B16A16_FLOAT);
        Ngx(allocate(&e.aaParams), "dlaa_parameters");
        auto *p = e.aaParams;
        for (auto v :
             {std::pair<const char *, unsigned>{NVSDK_NGX_Parameter_Width,
                                                fullW},
              {NVSDK_NGX_Parameter_Height, fullH},
              {NVSDK_NGX_Parameter_OutWidth, outW},
              {NVSDK_NGX_Parameter_OutHeight, outH},
              {NVSDK_NGX_Parameter_CreationNodeMask, 1},
              {NVSDK_NGX_Parameter_VisibilityNodeMask, 1},
              {opt.sr ? NVSDK_NGX_Parameter_DLSS_Hint_Render_Preset_Quality
                      : NVSDK_NGX_Parameter_DLSS_Hint_Render_Preset_DLAA,
               NVSDK_NGX_DLSS_Hint_Render_Preset_K}})
          ttwvrnr_parameter_set_uint(p, v.first, v.second);
        ttwvrnr_parameter_set_int(p, NVSDK_NGX_Parameter_PerfQualityValue,
                                  opt.sr
                                      ? NVSDK_NGX_PerfQuality_Value_MaxQuality
                                      : NVSDK_NGX_PerfQuality_Value_DLAA);
        ttwvrnr_parameter_set_int(p,
                                  NVSDK_NGX_Parameter_DLSS_Feature_Create_Flags,
                                  NVSDK_NGX_DLSS_Feature_Flags_IsHDR |
                                      NVSDK_NGX_DLSS_Feature_Flags_MVLowRes);
        auto *list = gpu.Begin();
        if (!list)
          throw std::runtime_error("dlaa_create_list");
        Ngx(create(list, NVSDK_NGX_Feature_SuperSampling, p, &e.aaFeature),
            "dlaa_create");
        if (!gpu.Submit() || !gpu.Drain())
          throw std::runtime_error("dlaa_create_submit");
        ttwvrnr_parameter_set_resource(p, NVSDK_NGX_Parameter_Color,
                                       e.aaColor.d12.Get());
        ttwvrnr_parameter_set_resource(p, NVSDK_NGX_Parameter_Depth,
                                       e.aaDepth.d12.Get());
        ttwvrnr_parameter_set_resource(p, NVSDK_NGX_Parameter_MotionVectors,
                                       e.aaMotion.d12.Get());
        ttwvrnr_parameter_set_resource(p, NVSDK_NGX_Parameter_Output,
                                       e.aaOutput.d12.Get());
        ttwvrnr_parameter_set_resource(p, NVSDK_NGX_Parameter_ExposureTexture,
                                       e.aaExposure.d12.Get());
        ttwvrnr_parameter_set_uint(
            p, NVSDK_NGX_Parameter_DLSS_Render_Subrect_Dimensions_Width, fullW);
        ttwvrnr_parameter_set_uint(
            p, NVSDK_NGX_Parameter_DLSS_Render_Subrect_Dimensions_Height,
            fullH);
        for (const char *key :
             {NVSDK_NGX_Parameter_MV_Scale_X, NVSDK_NGX_Parameter_MV_Scale_Y,
              NVSDK_NGX_Parameter_DLSS_Pre_Exposure,
              NVSDK_NGX_Parameter_DLSS_Exposure_Scale})
          ttwvrnr_parameter_set_float(p, key, 1.f);
        Log("%s_feature_created eye=%u feature=%p input=%ux%u output=%ux%u "
            "preset=K",
            opt.sr ? "sr" : "dlaa", index, e.aaFeature, fullW, fullH, outW,
            outH);
      }
      if (!opt.neural)
        continue;
      if (opt.passthrough)
        continue;
      Ngx(allocate(&e.params), "parameters");
      ttwvrnr_parameter_set_uint(e.params, "DLSSNR.Width", w);
      ttwvrnr_parameter_set_uint(e.params, "DLSSNR.Height", h);
      ttwvrnr_parameter_set_uint(e.params,
                                 NVSDK_NGX_Parameter_DLSSNR_Hint_Render_Preset,
                                 std::clamp(opt.preset, 1u, 7u));
      ttwvrnr_parameter_set_float(e.params, "DLSSNR.ScalingRatio", 1.f);
      auto *list = gpu.Begin();
      if (!list)
        throw std::runtime_error("create command list");
      Ngx(create(list, NVSDK_NGX_Feature_DLSSNR, e.params, &e.feature),
          "feature_create");
      if (!gpu.Submit() || !gpu.Drain())
        throw std::runtime_error("feature creation submit");
      for (const char *name : {"Color", "Depth", "MVec", "Output"}) {
        char key[80];
        for (auto value : {std::pair<const char *, UINT>{"SubrectBaseX", 0},
                           {"SubrectBaseY", 0},
                           {"SubrectWidth", w},
                           {"SubrectHeight", h}}) {
          std::snprintf(key, sizeof(key), "DLSSNR.%s%s", name, value.first);
          ttwvrnr_parameter_set_uint(e.params, key, value.second);
        }
      }
      ttwvrnr_parameter_set_resource(e.params, "DLSSNR.Color",
                                     e.color.d12.Get());
      ttwvrnr_parameter_set_resource(e.params, "DLSSNR.Depth",
                                     e.depth.d12.Get());
      ttwvrnr_parameter_set_resource(e.params, "DLSSNR.MVec",
                                     e.motion.d12.Get());
      ttwvrnr_parameter_set_resource(e.params, "DLSSNR.Output",
                                     e.output.d12.Get());
      for (auto p : {std::pair<const char *, int>{"Enabled", 1},
                     {"DepthInverted", 0},
                     {"UICorrection", 0},
                     {"UseAutoMask", 0}}) {
        std::string key = std::string("DLSSNR.") + p.first;
        ttwvrnr_parameter_set_int(e.params, key.c_str(), p.second);
      }
      ttwvrnr_parameter_set_uint(e.params, "DLSSNR.Style", 0);
      for (const char *name : {"MVecScaleX", "MVecScaleY"}) {
        std::string key = std::string("DLSSNR.") + name;
        ttwvrnr_parameter_set_float(e.params, key.c_str(), 1.f);
      }
      ttwvrnr_parameter_set_float(e.params, "DLSSNR.Intensity",
                                  std::clamp(opt.intensity, 0.f, 1.f));
      ttwvrnr_parameter_set_float(e.params, "DLSSNR.LocalToneStrength",
                                  std::clamp(opt.tone, 0.f, 1.f));
      ttwvrnr_parameter_set_float(e.params, "DLSSNR.LocalStructureStrength",
                                  std::clamp(opt.detail, 0.f, 1.f));
      ttwvrnr_parameter_set_float(e.params, "DLSSNR.SkinStructureStrength",
                                  std::clamp(opt.detail, 0.f, 1.f));
      Log("neural_feature_created eye=%u feature=%p model=%ux%u preset=%c "
          "tone=%.2f detail=%.2f intensity=%.2f",
          index, e.feature, w, h, 'A' + opt.preset - 1, opt.tone, opt.detail,
          opt.intensity);
    }
    Log("neural_backend_ready full=%ux%u model=%ux%u separate_eye_features=1 "
        "passthrough=%d",
        fullW, fullH, w, h, opt.passthrough);
    return true;
  }
  bool Render(ID3D11Texture2D *hdr, ID3D11Texture2D *depth,
              ID3D11Texture2D *motion, ID3D11Texture2D *exposure, unsigned eye,
              uint64_t frame, bool reset, float jitterX, float jitterY,
              ID3D11Texture2D *output) {
    if ((options.sr && !output) || failed || eye > 1)
      return false;
    const auto start = std::chrono::steady_clock::now();
    timing.Sample(frame, eye);
    auto &e = eyes[eye];
    auto *c = gpu.Context11();
    D3D11_TEXTURE2D_DESC dd{};
    depth->GetDesc(&dd);
    auto colorSrv = Srv(hdr, DXGI_FORMAT_R16G16B16A16_FLOAT);
    auto depthSrv = Srv(depth, dd.Format == DXGI_FORMAT_R24G8_TYPELESS
                                   ? DXGI_FORMAT_R24_UNORM_X8_TYPELESS
                                   : DXGI_FORMAT_R32_FLOAT);
    auto motionSrv = Srv(motion, DXGI_FORMAT_R16G16_FLOAT);
    auto exposureSrv = Srv(exposure, DXGI_FORMAT_R16_FLOAT);
    ContextState saved(c);
    ID3D11ShaderResourceView *srvs[] = {colorSrv.Get(),  depthSrv.Get(),
                                        motionSrv.Get(), exposureSrv.Get(),
                                        nullptr,         nullptr};
    ID3D11UnorderedAccessView *uavs[] = {e.colorUav.Get(), e.depthUav.Get(),
                                         e.motionUav.Get()};
    auto *cb = dimensions.Get();
    auto *smp = sampler.Get();
    c->CSSetConstantBuffers(0, 1, &cb);
    c->CSSetSamplers(0, 1, &smp);
    reset = reset || e.evaluations == 0 || e.previousFrame + 1 != frame;
    struct Dimensions {
      UINT fullW, fullH, w, h;
      float offsetX, offsetY, motionX, motionY;
      UINT outW, outH, pad0, pad1;
    };
    Dimensions dims{fullW, fullH, w, h, 0, 0, 0, 0, outW, outH, 0, 0};
    if (options.dlaa) {
      // DLAA output is unjittered. Guides remain on the jittered scene grid;
      // sample at its raster displacement. Motion itself excludes jitter.
      dims.offsetX = -jitterX;
      dims.offsetY = -jitterY;
    }
    c->UpdateSubresource(dimensions.Get(), 0, nullptr, &dims, 0, 0);
    if ((options.dlaa || options.sr) && !options.passthrough) {
      ID3D11UnorderedAccessView *aaUavs[] = {
          e.aaColorUav.Get(), e.aaDepthUav.Get(), e.aaMotionUav.Get()};
      c->CSSetShader(prepareAA.Get(), nullptr, 0);
      c->CSSetShaderResources(0, 6, srvs);
      c->CSSetUnorderedAccessViews(0, 3, aaUavs, nullptr);
      c->Dispatch((fullW + 7) / 8, (fullH + 7) / 8, 1);
      saved.Unbind();
      c->CopyResource(e.aaExposure.d11.Get(), exposure);
      auto *list = gpu.Begin();
      if (!list)
        throw std::runtime_error("dlaa_evaluate_list");
      for (auto *t : {&e.aaColor, &e.aaDepth, &e.aaMotion, &e.aaExposure})
        GpuInterop::Transition(list, t->d12.Get(), D3D12_RESOURCE_STATE_COMMON,
                               D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
      GpuInterop::Transition(list, e.aaOutput.d12.Get(),
                             D3D12_RESOURCE_STATE_COMMON,
                             D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
      ttwvrnr_parameter_set_int(e.aaParams, NVSDK_NGX_Parameter_Reset, reset);
      ttwvrnr_parameter_set_float(e.aaParams,
                                  NVSDK_NGX_Parameter_Jitter_Offset_X, jitterX);
      ttwvrnr_parameter_set_float(e.aaParams,
                                  NVSDK_NGX_Parameter_Jitter_Offset_Y, jitterY);
      timing.Begin(list, 0);
      Ngx(evaluate(list, e.aaFeature, e.aaParams, nullptr), "dlaa_evaluate");
      timing.End(list, 0);
      for (auto *t : {&e.aaColor, &e.aaDepth, &e.aaMotion, &e.aaExposure})
        GpuInterop::Transition(list, t->d12.Get(),
                               D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
                               D3D12_RESOURCE_STATE_COMMON);
      GpuInterop::Transition(list, e.aaOutput.d12.Get(),
                             D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                             D3D12_RESOURCE_STATE_COMMON);
      if (!gpu.Submit())
        throw std::runtime_error("dlaa_evaluate_submit");
      srvs[4] = e.aaOutputSrv.Get();
      aaUavs[0] = e.resolvedUav.Get();
      aaUavs[1] = aaUavs[2] = nullptr;
      c->CSSetShader(resolveAA.Get(), nullptr, 0);
      c->CSSetShaderResources(0, 6, srvs);
      c->CSSetUnorderedAccessViews(0, 3, aaUavs, nullptr);
      c->Dispatch((outW + 7) / 8, (outH + 7) / 8, 1);
      saved.Unbind();
      c->CopyResource(options.sr ? output : hdr, e.resolved.Get());
      srvs[4] = nullptr;
      if (reset || e.evaluations < 3 || e.evaluations % 120 == 0)
        Log("%s_evaluate frame=%llu eye=%u reset=%d jitter_x=%.6f "
            "jitter_y=%.6f output_applied=1",
            options.sr ? "sr" : "dlaa", (unsigned long long)frame, eye, reset,
            jitterX, jitterY);
    }
    if (options.neural) {
      c->CSSetShader(prepare.Get(), nullptr, 0);
      c->CSSetShaderResources(0, 6, srvs);
      c->CSSetUnorderedAccessViews(0, 3, uavs, nullptr);
      c->Dispatch((w + 7) / 8, (h + 7) / 8, 1);
      saved.Unbind();
      if (options.passthrough)
        c->CopyResource(e.output.d11.Get(), e.color.d11.Get());
      else {
        auto *list = gpu.Begin();
        if (!list)
          throw std::runtime_error("evaluate command list");
        for (auto *t : {&e.color, &e.depth, &e.motion})
          GpuInterop::Transition(
              list, t->d12.Get(), D3D12_RESOURCE_STATE_COMMON,
              D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        GpuInterop::Transition(list, e.output.d12.Get(),
                               D3D12_RESOURCE_STATE_COMMON,
                               D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        reset = reset || e.evaluations == 0 || e.previousFrame + 1 != frame;
        ttwvrnr_parameter_set_int(e.params, "DLSSNR.Reset", reset ? 1 : 0);
        timing.Begin(list, 1);
        Ngx(evaluate(list, e.feature, e.params, nullptr), "evaluate");
        timing.End(list, 1);
        for (auto *t : {&e.color, &e.depth, &e.motion})
          GpuInterop::Transition(list, t->d12.Get(),
                                 D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
                                 D3D12_RESOURCE_STATE_COMMON);
        GpuInterop::Transition(list, e.output.d12.Get(),
                               D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                               D3D12_RESOURCE_STATE_COMMON);
        if (!gpu.Submit())
          throw std::runtime_error("evaluate submit");
      }
      srvs[4] = e.outputSrv.Get();
      srvs[5] = e.inputSrv.Get();
      uavs[0] = e.resolvedUav.Get();
      uavs[1] = uavs[2] = nullptr;
      c->CSSetShader(resolve.Get(), nullptr, 0);
      c->CSSetShaderResources(0, 6, srvs);
      c->CSSetUnorderedAccessViews(0, 3, uavs, nullptr);
      c->Dispatch((fullW + 7) / 8, (fullH + 7) / 8, 1);
      saved.Unbind();
      // CopyResource permits same-format-family typeless/typed resources.
      c->CopyResource(hdr, e.resolved.Get());
    }
    e.previousFrame = frame;
    e.previousJitterX = jitterX;
    e.previousJitterY = jitterY;
    ++e.evaluations;
    if (options.neural && (e.evaluations <= 3 || e.evaluations % 120 == 0))
      Log("%s frame=%llu eye=%u reset=%d count=%llu cpu_ms=%.3f "
          "output_applied=1",
          options.passthrough ? "conversion_evaluate" : "neural_evaluate",
          (unsigned long long)frame, eye, reset,
          (unsigned long long)e.evaluations,
          std::chrono::duration<double, std::milli>(
              std::chrono::steady_clock::now() - start)
              .count());
    return true;
  }
  bool Shutdown() {
    if (!gpu.Drain())
      return false;
    for (auto &e : eyes) {
      if (e.aaFeature) {
        Ngx(release(e.aaFeature), "dlaa_release");
        e.aaFeature = nullptr;
      }
      if (e.aaParams) {
        Ngx(destroy(e.aaParams), "dlaa_destroy");
        e.aaParams = nullptr;
      }
      if (e.feature) {
        Ngx(release(e.feature), "release_feature");
        e.feature = nullptr;
      }
      if (e.params) {
        Ngx(destroy(e.params), "destroy_parameters");
        e.params = nullptr;
      }
    }
    if (caps) {
      Ngx(destroy(caps), "destroy_capabilities");
      caps = nullptr;
    }
    if (initialized) {
      Ngx(static_cast<NVSDK_NGX_Result>(LeaveNgxRuntime(core)),
          "shared_shutdown");
      initialized = false;
    }
    if (core) {
      FreeLibrary(core);
      core = nullptr;
    }
    Log("neural_backend_shutdown complete=1");
    return true;
  }
};
void NeuralBackend::LogCompletedGpuTiming() {
  if (impl_ && !impl_->failed)
    impl_->timing.Collect(impl_->gpu);
}
NeuralBackend::NeuralBackend() = default;
NeuralBackend::~NeuralBackend() { Shutdown(); }
bool NeuralBackend::Init(ID3D11Device *d, unsigned w, unsigned h,
                         const std::wstring &dir, const NeuralOptions &opt) {
  if (impl_)
    return false;
  impl_ = std::make_unique<Impl>();
  try {
    if (impl_->Init(d, w, h, dir, opt))
      return true;
  } catch (const std::exception &e) {
    Log("neural_init_failed %s", e.what());
  }
  impl_->failed = true;
  return false;
}
bool NeuralBackend::Render(ID3D11Texture2D *c, ID3D11Texture2D *d,
                           ID3D11Texture2D *m, ID3D11Texture2D *x, unsigned eye,
                           uint64_t frame, bool reset, float jitterX,
                           float jitterY, ID3D11Texture2D *output) {
  if (!impl_ || impl_->failed)
    return false;
  try {
    return impl_->Render(c, d, m, x, eye, frame, reset, jitterX, jitterY,
                         output);
  } catch (const std::exception &e) {
    Log("neural_render_failed %s; disabling backend", e.what());
    impl_->failed = true;
    return false;
  }
}
bool NeuralBackend::Shutdown() {
  if (!impl_)
    return true;
  try {
    if (!impl_->failed && impl_->Shutdown()) {
      impl_.reset();
      return true;
    }
  } catch (const std::exception &e) {
    Log("neural_shutdown_failed %s", e.what());
  }
  // A device/queue failure can leave GPU references in flight. Preserve the
  // entire private backend until process exit rather than freeing live work.
  Log("neural_backend_quarantined until_process_exit=1");
  (void)impl_.release();
  return false;
}
} // namespace ttwvrnr
