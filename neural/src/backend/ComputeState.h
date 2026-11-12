// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <d3d11.h>
namespace ttwvrnr {
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
} // namespace ttwvrnr
