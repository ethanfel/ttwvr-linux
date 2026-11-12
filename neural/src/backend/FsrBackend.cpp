// SPDX-License-Identifier: GPL-3.0-only
#include "FsrBackend.h"
#include "../GuideMath.h"
#include "../SRMode.h"
#include "../Support.h"
#include "ComputeState.h"
#include "GpuInterop.h"
#include "GpuTiming.h"
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wswitch"
#include "../../third_party/fidelityfx/api/include/dx12/ffx_api_dx12.h"
#pragma GCC diagnostic pop
#include "../../third_party/fidelityfx/upscalers/include/ffx_upscale.h"
#include <algorithm>
#include <array>
#include <chrono>
#include <cstring>
#include <d3dcompiler.h>
#include <stdexcept>
#include <vector>
namespace ttwvrnr {
using Microsoft::WRL::ComPtr;
namespace {
void Check(HRESULT r, const char *what) {
  if (FAILED(r))
    throw std::runtime_error(what);
}
void Ffx(ffxReturnCode_t r, const char *what) {
  if (r != FFX_API_RETURN_OK) {
    Log("fsr_failure operation=%s code=%u", what, unsigned(r));
    throw std::runtime_error(what);
  }
}
void Message(uint32_t type, const wchar_t *text) {
  Log("fsr_message type=%u message=%ls", type, text ? text : L"");
}
constexpr const char *Shader = R"(
cbuffer Sizes : register(b0) { uint2 inputSize; uint2 outputSize; };
Texture2D<float4> original : register(t0);
Texture2D<float4> reconstructed : register(t1);
SamplerState linearClamp : register(s0);
RWTexture2D<float4> destination : register(u0);
[numthreads(8,8,1)] void Prepare(uint3 p:SV_DispatchThreadID) {
 if(any(p.xy>=inputSize)) return;
 destination[p.xy]=float4(max(original.Load(int3(p.xy,0)).rgb,0),1);
}
[numthreads(8,8,1)] void Resolve(uint3 p:SV_DispatchThreadID) {
 if(any(p.xy>=outputSize)) return;
 float alpha=original.SampleLevel(linearClamp,(p.xy+0.5)/outputSize,0).a;
 destination[p.xy]=float4(reconstructed.Load(int3(p.xy,0)).rgb,alpha);
}
)";
} // namespace
struct FsrBackend::Impl {
  GpuInterop gpu;
  GpuTiming timing;
  ComPtr<ID3D11Device> device;
  ComPtr<ID3D11ComputeShader> prepare, resolve;
  ComPtr<ID3D11Buffer> sizes;
  ComPtr<ID3D11SamplerState> sampler;
  HMODULE module = nullptr;
  PfnFfxCreateContext create = nullptr;
  PfnFfxDestroyContext destroy = nullptr;
  PfnFfxDispatch dispatch = nullptr;
  PfnFfxQuery query = nullptr;
  ffxCreateBackendDX12Desc backend{};
  ffxOverrideVersion version{};
  unsigned w = 0, h = 0, ow = 0, oh = 0, mode = 0;
  bool failed = false;
  struct Eye {
    ffxContext context = nullptr;
    ffxCreateContextDescUpscale desc{};
    SharedTexture color, depth, motion, exposure, output;
    ComPtr<ID3D11Texture2D> resolved;
    ComPtr<ID3D11UnorderedAccessView> colorUav, resolvedUav;
    ComPtr<ID3D11ShaderResourceView> outputSrv;
    FrameCamera previousCamera;
    std::chrono::steady_clock::time_point previousTime{};
    uint64_t previousFrame = ~uint64_t(0), count = 0;
  };
  std::array<Eye, 2> eyes;
  ComPtr<ID3D11ShaderResourceView> Srv(ID3D11Texture2D *t) {
    ComPtr<ID3D11ShaderResourceView> v;
    Check(device->CreateShaderResourceView(t, nullptr, &v), "fsr_srv");
    return v;
  }
  bool Init(ID3D11Device *d, unsigned iw, unsigned ih, unsigned outw,
            unsigned outh, unsigned m, const std::wstring &dir, bool profile) {
    if (!IsFSR(m) || iw != SRInputSize(m).width ||
        ih != SRInputSize(m).height || outw != OutputWidth ||
        outh != OutputHeight)
      throw std::runtime_error("FSR dimensions/mode");
    device = d;
    w = iw;
    h = ih;
    ow = outw;
    oh = outh;
    mode = m;
    if (!gpu.Init(d))
      return false;
    if (profile)
      timing.Init(gpu, true);
    // Load only the separately supplied effect DLL; no search-path mutation.
    const auto path = dir + L"\\amd_fidelityfx_upscaler_dx12.dll";
    module = LoadLibraryExW(path.c_str(), nullptr,
                            LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR |
                                LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
    if (!module)
      throw std::runtime_error(
          "amd_fidelityfx_upscaler_dx12.dll missing or cannot load");
    create = reinterpret_cast<PfnFfxCreateContext>(
        GetProcAddress(module, "ffxCreateContext"));
    destroy = reinterpret_cast<PfnFfxDestroyContext>(
        GetProcAddress(module, "ffxDestroyContext"));
    dispatch =
        reinterpret_cast<PfnFfxDispatch>(GetProcAddress(module, "ffxDispatch"));
    query = reinterpret_cast<PfnFfxQuery>(GetProcAddress(module, "ffxQuery"));
    if (!create || !destroy || !dispatch || !query)
      throw std::runtime_error("FSR API exports missing");
    uint64_t count = 0;
    ffxQueryDescGetVersions versions{};
    versions.header.type = FFX_API_QUERY_DESC_TYPE_GET_VERSIONS;
    versions.createDescType = FFX_API_CREATE_CONTEXT_DESC_TYPE_UPSCALE;
    versions.device = gpu.Device12();
    versions.outputCount = &count;
    Ffx(query(nullptr, &versions.header), "versions_count");
    if (!count || count > 32)
      throw std::runtime_error("FSR provider count");
    std::vector<uint64_t> ids(count);
    std::vector<const char *> names(count);
    versions.versionIds = ids.data();
    versions.versionNames = names.data();
    Ffx(query(nullptr, &versions.header), "versions");
    if (count > ids.size())
      throw std::runtime_error("FSR provider count changed");
    bool selected = false;
    for (size_t i = 0; i < count; ++i) {
      Log("fsr_provider name=%s id=%llu", names[i] ? names[i] : "",
          (unsigned long long)ids[i]);
      if (!selected && names[i] && std::strstr(names[i], "3.1.")) {
        version.header.type = FFX_API_DESC_TYPE_OVERRIDE_VERSION;
        version.versionId = ids[i];
        selected = true;
      }
    }
    if (!selected)
      throw std::runtime_error("FSR 3.1 provider unavailable");
    uint32_t rw = 0, rh = 0;
    ffxQueryDescUpscaleGetRenderResolutionFromQualityMode resolution{};
    resolution.header.type =
        FFX_API_QUERY_DESC_TYPE_UPSCALE_GETRENDERRESOLUTIONFROMQUALITYMODE;
    resolution.header.pNext = &version.header;
    resolution.displayWidth = ow;
    resolution.displayHeight = oh;
    resolution.qualityMode = SRQuality(mode);
    resolution.pOutRenderWidth = &rw;
    resolution.pOutRenderHeight = &rh;
    Ffx(query(nullptr, &resolution.header), "resolution_query");
    if (rw != w || rh != h)
      throw std::runtime_error("FSR SDK size mismatch");
    int32_t phases = 0;
    ffxQueryDescUpscaleGetJitterPhaseCount jitter{};
    jitter.header.type = FFX_API_QUERY_DESC_TYPE_UPSCALE_GETJITTERPHASECOUNT;
    jitter.header.pNext = &version.header;
    jitter.renderWidth = w;
    jitter.displayWidth = ow;
    jitter.pOutPhaseCount = &phases;
    Ffx(query(nullptr, &jitter.header), "jitter_query");
    if (phases != int32_t(SRJitterPhases(mode)))
      throw std::runtime_error("FSR jitter phase mismatch");
    backend.header.type = FFX_API_CREATE_CONTEXT_DESC_TYPE_BACKEND_DX12;
    backend.header.pNext = &version.header;
    backend.device = gpu.Device12();
    for (auto pair : {std::pair<const char *, ID3D11ComputeShader **>{
                          "Prepare", prepare.GetAddressOf()},
                      {"Resolve", resolve.GetAddressOf()}}) {
      ComPtr<ID3DBlob> code, error;
      auto hr = D3DCompile(Shader, std::strlen(Shader), "TTWVR FSR conversions",
                           nullptr, nullptr, pair.first, "cs_5_0",
                           D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &code, &error);
      if (FAILED(hr) && error)
        Log("fsr_shader_error %s",
            static_cast<const char *>(error->GetBufferPointer()));
      Check(hr, "fsr_compile");
      Check(d->CreateComputeShader(code->GetBufferPointer(),
                                   code->GetBufferSize(), nullptr, pair.second),
            "fsr_shader");
    }
    uint32_t dims[] = {w, h, ow, oh};
    D3D11_BUFFER_DESC bd{};
    bd.ByteWidth = sizeof(dims);
    bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    D3D11_SUBRESOURCE_DATA init{dims, 0, 0};
    Check(d->CreateBuffer(&bd, &init, &sizes), "fsr_sizes");
    D3D11_SAMPLER_DESC sd{};
    sd.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    sd.AddressU = sd.AddressV = sd.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    sd.MaxLOD = D3D11_FLOAT32_MAX;
    Check(d->CreateSamplerState(&sd, &sampler), "fsr_sampler");
    for (unsigned eye = 0; eye < 2; ++eye) {
      auto &e = eyes[eye];
      e.desc.header.type = FFX_API_CREATE_CONTEXT_DESC_TYPE_UPSCALE;
      e.desc.header.pNext = &backend.header;
      e.desc.flags = FFX_UPSCALE_ENABLE_HIGH_DYNAMIC_RANGE |
                     FFX_UPSCALE_ENABLE_DEBUG_CHECKING;
      e.desc.maxRenderSize = {w, h};
      e.desc.maxUpscaleSize = {ow, oh};
      e.desc.fpMessage = Message;
      Ffx(create(&e.context, &e.desc.header, nullptr), "create_context");
      if (!gpu.CreateTexture(w, h, DXGI_FORMAT_R16G16B16A16_FLOAT, e.color) ||
          !gpu.CreateTexture(w, h, DXGI_FORMAT_R32_FLOAT, e.depth) ||
          !gpu.CreateTexture(w, h, DXGI_FORMAT_R16G16_FLOAT, e.motion) ||
          !gpu.CreateTexture(1, 1, DXGI_FORMAT_R16_FLOAT, e.exposure) ||
          !gpu.CreateTexture(ow, oh, DXGI_FORMAT_R16G16B16A16_FLOAT, e.output))
        throw std::runtime_error("FSR shared textures");
      Check(
          d->CreateUnorderedAccessView(e.color.d11.Get(), nullptr, &e.colorUav),
          "fsr_color_uav");
      e.outputSrv = Srv(e.output.d11.Get());
      D3D11_TEXTURE2D_DESC td{};
      td.Width = ow;
      td.Height = oh;
      td.ArraySize = td.MipLevels = td.SampleDesc.Count = 1;
      td.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
      td.BindFlags = D3D11_BIND_UNORDERED_ACCESS;
      Check(d->CreateTexture2D(&td, nullptr, &e.resolved), "fsr_resolved");
      Check(d->CreateUnorderedAccessView(e.resolved.Get(), nullptr,
                                         &e.resolvedUav),
            "fsr_resolved_uav");
      Log("fsr_context_created eye=%u input=%ux%u output=%ux%u "
          "jitter_phases=%d",
          eye, w, h, ow, oh, phases);
    }
    return true;
  }
  bool Render(ID3D11Texture2D *color, ID3D11Texture2D *depth,
              ID3D11Texture2D *motion, ID3D11Texture2D *exposure,
              ID3D11Texture2D *output, unsigned eye, uint64_t frame, bool reset,
              float jx, float jy, const FrameCamera &camera, float meters) {
    if (eye > 1 || !color || !depth || !motion || !exposure || !output ||
        !camera.valid || !std::isfinite(meters) || meters <= 0 || meters > 1)
      throw std::runtime_error("FSR input/camera validation");
    auto &e = eyes[eye];
    auto now = std::chrono::steady_clock::now();
    float delta = e.count ? float(std::chrono::duration<double, std::milli>(
                                      now - e.previousTime)
                                      .count())
                          : 16.6667f;
    reset = reset || !e.count || frame != e.previousFrame + 1 ||
            CameraCut(e.previousCamera, camera) || delta > 250;
    delta = std::clamp(delta, 0.1f, 250.f);
    auto *c = gpu.Context11();
    ContextState saved(c);
    auto input = Srv(color);
    auto *cb = sizes.Get();
    auto *sam = sampler.Get();
    ID3D11ShaderResourceView *srvs[] = {input.Get(), nullptr};
    auto *uav = e.colorUav.Get();
    c->CSSetConstantBuffers(0, 1, &cb);
    c->CSSetSamplers(0, 1, &sam);
    c->CSSetShader(prepare.Get(), nullptr, 0);
    c->CSSetShaderResources(0, 2, srvs);
    c->CSSetUnorderedAccessViews(0, 1, &uav, nullptr);
    c->Dispatch((w + 7) / 8, (h + 7) / 8, 1);
    saved.Unbind();
    c->CopyResource(e.depth.d11.Get(), depth);
    c->CopyResource(e.motion.d11.Get(), motion);
    c->CopyResource(e.exposure.d11.Get(), exposure);
    auto *list = gpu.Begin();
    if (!list)
      throw std::runtime_error("FSR command list");
    for (auto *t : {&e.color, &e.depth, &e.motion, &e.exposure})
      GpuInterop::Transition(list, t->d12.Get(), D3D12_RESOURCE_STATE_COMMON,
                             D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    GpuInterop::Transition(list, e.output.d12.Get(),
                           D3D12_RESOURCE_STATE_COMMON,
                           D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    ffxDispatchDescUpscale desc{};
    desc.header.type = FFX_API_DISPATCH_DESC_TYPE_UPSCALE;
    desc.commandList = list;
    desc.color = ffxApiGetResourceDX12(e.color.d12.Get());
    desc.depth = ffxApiGetResourceDX12(e.depth.d12.Get());
    desc.motionVectors = ffxApiGetResourceDX12(e.motion.d12.Get());
    desc.exposure = ffxApiGetResourceDX12(e.exposure.d12.Get());
    desc.output = ffxApiGetResourceDX12(
        e.output.d12.Get(), FFX_API_RESOURCE_STATE_UNORDERED_ACCESS);
    desc.jitterOffset = {RasterJitter(jx), RasterJitter(jy)};
    desc.motionVectorScale = {1, 1};
    desc.renderSize = {w, h};
    desc.upscaleSize = {ow, oh};
    desc.enableSharpening = false;
    desc.sharpness = 0;
    desc.frameTimeDelta = delta;
    desc.preExposure = 1;
    desc.reset = reset;
    desc.cameraNear = camera.nearPlane;
    desc.cameraFar = camera.farPlane;
    desc.cameraFovAngleVertical = camera.fov;
    desc.viewSpaceToMetersFactor = meters;
    timing.Sample(frame, eye);
    timing.Begin(list, 0);
    Ffx(dispatch(&e.context, &desc.header), "dispatch");
    timing.End(list, 0);
    for (auto *t : {&e.color, &e.depth, &e.motion, &e.exposure})
      GpuInterop::Transition(list, t->d12.Get(),
                             D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
                             D3D12_RESOURCE_STATE_COMMON);
    GpuInterop::Transition(list, e.output.d12.Get(),
                           D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                           D3D12_RESOURCE_STATE_COMMON);
    if (!gpu.Submit())
      throw std::runtime_error("FSR submit");
    srvs[1] = e.outputSrv.Get();
    uav = e.resolvedUav.Get();
    c->CSSetShader(resolve.Get(), nullptr, 0);
    c->CSSetShaderResources(0, 2, srvs);
    c->CSSetUnorderedAccessViews(0, 1, &uav, nullptr);
    c->Dispatch((ow + 7) / 8, (oh + 7) / 8, 1);
    saved.Unbind();
    c->CopyResource(output, e.resolved.Get());
    e.previousFrame = frame;
    e.previousTime = now;
    e.previousCamera = camera;
    ++e.count;
    if (reset || e.count <= 3 || e.count % 120 == 0)
      Log("fsr_evaluate frame=%llu eye=%u reset=%u mode=%s jitter_x=%.5f "
          "jitter_y=%.5f near=%.5f far=%.3f fov=%.5f meters=%.7f frame_ms=%.3f "
          "output_applied=1",
          (unsigned long long)frame, eye, unsigned(reset), SRName(mode),
          RasterJitter(jx), RasterJitter(jy), camera.nearPlane, camera.farPlane,
          camera.fov, meters, delta);
    return true;
  }
  bool Shutdown() {
    if (!gpu.Drain())
      return false;
    for (auto &e : eyes)
      if (e.context)
        Ffx(destroy(&e.context, nullptr), "destroy_context");
    if (module) {
      FreeLibrary(module);
      module = nullptr;
    }
    Log("fsr_backend_shutdown complete=1");
    return true;
  }
};
FsrBackend::FsrBackend() = default;
FsrBackend::~FsrBackend() { Shutdown(); }
bool FsrBackend::Init(ID3D11Device *d, unsigned w, unsigned h, unsigned ow,
                      unsigned oh, unsigned mode, const std::wstring &dir,
                      bool profile) {
  if (impl_)
    return false;
  impl_ = std::make_unique<Impl>();
  try {
    if (impl_->Init(d, w, h, ow, oh, mode, dir, profile))
      return true;
  } catch (const std::exception &e) {
    Log("fsr_init_failed reason=%s", e.what());
  }
  impl_->failed = true;
  return false;
}
bool FsrBackend::Render(ID3D11Texture2D *c, ID3D11Texture2D *d,
                        ID3D11Texture2D *m, ID3D11Texture2D *x,
                        ID3D11Texture2D *out, unsigned eye, uint64_t frame,
                        bool reset, float jx, float jy,
                        const FrameCamera &camera, float meters) {
  if (!impl_ || impl_->failed)
    return false;
  try {
    return impl_->Render(c, d, m, x, out, eye, frame, reset, jx, jy, camera,
                         meters);
  } catch (const std::exception &e) {
    Log("fsr_render_failed reason=%s bypass=1", e.what());
    impl_->failed = true;
    return false;
  }
}
void FsrBackend::LogCompletedGpuTiming() {
  if (impl_ && !impl_->failed)
    impl_->timing.Collect(impl_->gpu);
}
bool FsrBackend::Shutdown() {
  if (!impl_)
    return true;
  try {
    if (!impl_->failed && impl_->Shutdown()) {
      impl_.reset();
      return true;
    }
  } catch (const std::exception &e) {
    Log("fsr_shutdown_failed reason=%s", e.what());
  }
  Log("fsr_backend_quarantined until_process_exit=1");
  (void)impl_.release();
  return false;
}
} // namespace ttwvrnr
