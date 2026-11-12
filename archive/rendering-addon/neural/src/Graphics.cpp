// SPDX-License-Identifier: GPL-3.0-only
// Derived from Ethanfel TF2VRNeuralRendering; see THIRD-PARTY-NOTICES.md.
#include "Graphics.h"
#include "Bindings.h"
#include "FrameDelivery.h"
#include "Support.h"
#include <cerrno>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <d3d11_4.h>
#include <limits>
#include <locale>
#include <sstream>
#include <vector>
#include <wrl/client.h>
namespace ttwvrnr {
using Microsoft::WRL::ComPtr;
bool CopyPresentationImage(ID3D11Device *device, ID3D11Texture2D *destination,
                           ID3D11Texture2D *source) {
  if (!device || !destination || !source)
    return false;
  D3D11_TEXTURE2D_DESC d{}, s{};
  destination->GetDesc(&d);
  source->GetDesc(&s);
  auto rgba = [](DXGI_FORMAT f) {
    return f == DXGI_FORMAT_R8G8B8A8_UNORM ||
           f == DXGI_FORMAT_R8G8B8A8_UNORM_SRGB ||
           f == DXGI_FORMAT_R8G8B8A8_TYPELESS;
  };
  if (d.Width != s.Width || d.Height != s.Height ||
      d.ArraySize != s.ArraySize || d.MipLevels != s.MipLevels ||
      d.SampleDesc.Count != s.SampleDesc.Count || !rgba(d.Format) ||
      !rgba(s.Format))
    return false;
  ComPtr<ID3D11DeviceContext> context;
  device->GetImmediateContext(&context);
  ComPtr<ID3D11Multithread> protection;
  context.As(&protection);
  if (protection)
    protection->Enter();
  context->CopyResource(destination, source);
  context->Flush();
  if (protection)
    protection->Leave();
  return SUCCEEDED(device->GetDeviceRemovedReason());
}
bool DrainPresentation(ID3D11Device *device) {
  if (!device)
    return false;
  ComPtr<ID3D11DeviceContext> context;
  device->GetImmediateContext(&context);
  ComPtr<ID3D11Query> query;
  D3D11_QUERY_DESC desc{D3D11_QUERY_EVENT, 0};
  if (FAILED(device->CreateQuery(&desc, &query)))
    return false;
  context->End(query.Get());
  context->Flush();
  const auto deadline = GetTickCount64() + 5000;
  HRESULT hr;
  while ((hr = context->GetData(query.Get(), nullptr, 0,
                                D3D11_ASYNC_GETDATA_DONOTFLUSH)) == S_FALSE) {
    if (GetTickCount64() >= deadline)
      return false;
    Sleep(1);
  }
  return hr == S_OK;
}
bool SetPresentationThreading(ID3D11Device *device, bool enabled,
                              bool &previous) {
  if (!device)
    return false;
  ComPtr<ID3D11DeviceContext> context;
  device->GetImmediateContext(&context);
  ComPtr<ID3D11Multithread> protection;
  if (FAILED(context.As(&protection)))
    return false;
  previous =
      protection->SetMultithreadProtected(enabled ? TRUE : FALSE) != FALSE;
  return true;
}
StereoFrameTextures CopyFrameTextures(ID3D11Device *device,
                                      const StereoFrameTextures &source) {
  struct Images {
    ComPtr<ID3D11Texture2D> real[2], generated[2];
  };
  if (!source || !device)
    return {};
  auto owned = std::make_shared<Images>();
  StereoFrameTextures out = source;
  out.owner = owned;
  ComPtr<ID3D11DeviceContext> context;
  device->GetImmediateContext(&context);
  ComPtr<ID3D11Multithread> protection;
  context.As(&protection);
  for (unsigned e = 0; e < 2; ++e)
    for (unsigned generated = 0; generated < 2; ++generated) {
      auto *input = generated ? source.generated[e] : source.real[e];
      auto &output = generated ? owned->generated[e] : owned->real[e];
      D3D11_TEXTURE2D_DESC desc{};
      input->GetDesc(&desc);
      if (desc.ArraySize != 1 || desc.SampleDesc.Count != 1 ||
          desc.MipLevels != 1)
        return {};
      desc.Usage = D3D11_USAGE_DEFAULT;
      desc.CPUAccessFlags = desc.MiscFlags = 0;
      desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
      if (FAILED(device->CreateTexture2D(&desc, nullptr, &output)))
        return {};
      if (protection)
        protection->Enter();
      context->CopyResource(output.Get(), input);
      if (protection)
        protection->Leave();
      if (generated)
        out.generated[e] = output.Get();
      else
        out.real[e] = output.Get();
    }
  if (protection)
    protection->Enter();
  context->Flush();
  if (protection)
    protection->Leave();
  return out;
}
namespace {
unsigned BytesPerPixel(DXGI_FORMAT f) {
  switch (f) {
  case DXGI_FORMAT_R16_FLOAT:
    return 2;
  case DXGI_FORMAT_R8G8B8A8_UNORM:
  case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:
  case DXGI_FORMAT_B8G8R8A8_UNORM:
  case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB:
  case DXGI_FORMAT_R10G10B10A2_UNORM:
  case DXGI_FORMAT_R32_FLOAT:
  case DXGI_FORMAT_D32_FLOAT:
  case DXGI_FORMAT_D24_UNORM_S8_UINT:
  case DXGI_FORMAT_R16G16_FLOAT:
    return 4;
  case DXGI_FORMAT_R16G16B16A16_FLOAT:
  case DXGI_FORMAT_R16G16B16A16_UNORM:
    return 8;
  default:
    return 0;
  }
}
struct DdsHeader {
  uint32_t magic = 0x20534444, size = 124, flags = 0x100f, height = 0,
           width = 0, pitch = 0, depth = 0, mips = 0;
  uint32_t reserved[11]{};
  uint32_t pfSize = 32, pfFlags = 4, fourcc = 0x30315844, bits = 0, r = 0,
           g = 0, b = 0, a = 0;
  uint32_t caps = 0x1000, caps2 = 0, caps3 = 0, caps4 = 0, reserved2 = 0;
  uint32_t format = 0, dimension = 3, misc = 0, arraySize = 1, alpha = 0;
};
static_assert(sizeof(DdsHeader) == 148);
class Snapshot final : public CapturedImage {
public:
  ComPtr<ID3D11DeviceContext> context;
  ComPtr<ID3D11Texture2D> texture;
  D3D11_TEXTURE2D_DESC desc{};
  DXGI_FORMAT format{};
  std::string bindings;
  std::vector<uint8_t> pixels;
  bool cpuReady = false;
  const std::string &BindingsJson() const override { return bindings; }
  bool PrepareForAsyncWrite() override {
    if (cpuReady)
      return true;
    const auto bpp = BytesPerPixel(format);
    if (!context || !texture || !bpp)
      return false;
    const size_t pitch = size_t(desc.Width) * bpp;
    const size_t sliceBytes = pitch * desc.Height;
    std::vector<uint8_t> owned(sliceBytes * desc.ArraySize);
    for (UINT slice = 0; slice < desc.ArraySize; ++slice) {
      const auto sub = D3D11CalcSubresource(0, slice, desc.MipLevels);
      D3D11_MAPPED_SUBRESOURCE map{};
      const auto hr = context->Map(texture.Get(), sub, D3D11_MAP_READ, 0, &map);
      if (FAILED(hr)) {
        Log("capture_readback_failed hr=0x%08lx",
            static_cast<unsigned long>(hr));
        return false;
      }
      const bool valid = map.pData && map.RowPitch >= pitch;
      if (valid)
        for (UINT y = 0; y < desc.Height; ++y)
          std::memcpy(owned.data() + slice * sliceBytes + y * pitch,
                      static_cast<const uint8_t *>(map.pData) +
                          size_t(y) * map.RowPitch,
                      pitch);
      context->Unmap(texture.Get(), sub);
      if (!valid)
        return false;
    }
    pixels = std::move(owned);
    // Never send GPU resources or the immediate context to the IO worker.
    texture.Reset();
    context.Reset();
    cpuReady = true;
    return true;
  }
  bool WriteView(const ViewLayout &view,
                 const std::wstring &filename) override {
    const auto bpp = BytesPerPixel(format);
    if (!bpp || !Fits(desc.Width, desc.Height, desc.ArraySize, view)) {
      Log("capture_view_rejected format=%u size=%ux%u array=%u slice=%u "
          "rect=%d,%d,%d,%d",
          static_cast<unsigned>(format), desc.Width, desc.Height,
          desc.ArraySize, view.slice, view.x, view.y, view.width, view.height);
      return false;
    }
    const auto sub = D3D11CalcSubresource(0, view.slice, desc.MipLevels);
    D3D11_MAPPED_SUBRESOURCE map{};
    if (cpuReady) {
      map.RowPitch = desc.Width * bpp;
      map.pData =
          pixels.data() + size_t(view.slice) * desc.Height * map.RowPitch;
    } else {
      const auto hr = context->Map(texture.Get(), sub, D3D11_MAP_READ, 0, &map);
      if (FAILED(hr)) {
        Log("capture_map_failed hr=0x%08lx", static_cast<unsigned long>(hr));
        return false;
      }
    }
    FILE *f = _wfopen(filename.c_str(), L"wb");
    bool ok = false;
    if (f) {
      DdsHeader h;
      h.width = static_cast<uint32_t>(view.width);
      h.height = static_cast<uint32_t>(view.height);
      h.pitch = h.width * bpp;
      h.format = static_cast<uint32_t>(format);
      ok = map.RowPitch >= (static_cast<uint64_t>(view.x) + h.width) * bpp &&
           std::fwrite(&h, sizeof(h), 1, f) == 1;
      for (uint32_t y = 0; ok && y < h.height; ++y) {
        const auto row = static_cast<const uint8_t *>(map.pData) +
                         (static_cast<uint64_t>(view.y) + y) * map.RowPitch +
                         static_cast<uint64_t>(view.x) * bpp;
        ok = std::fwrite(row, h.pitch, 1, f) == 1;
      }
      if (std::fclose(f))
        ok = false;
    } else
      Log("capture_file_open_failed errno=%d", errno);
    if (!cpuReady)
      context->Unmap(texture.Get(), sub);
    if (!ok) {
      Log("capture_write_failed errno=%d row_pitch=%u", errno, map.RowPitch);
      DeleteFileW(filename.c_str());
    }
    return ok;
  }
};
// Only inspect the currently bound views, and take owned copies now. Never
// retain a game texture for later readback: it may be reused for the other eye.

} // namespace
std::shared_ptr<CapturedImage>
CaptureImage(ID3D11Device *device, ID3D11Texture2D *source, int64_t xrFormat,
             bool inspectBindings, bool captureCandidates) {
  if (!device || !source || xrFormat < 0 ||
      xrFormat > std::numeric_limits<uint32_t>::max()) {
    Log("capture_invalid_source device=%p source=%p format=%lld",
        static_cast<void *>(device), static_cast<void *>(source),
        static_cast<long long>(xrFormat));
    return {};
  }
  const auto format = static_cast<DXGI_FORMAT>(xrFormat);
  const auto bpp = BytesPerPixel(format);
  if (!bpp) {
    Log("capture_unsupported_format=%lld", static_cast<long long>(xrFormat));
    return {};
  }
  D3D11_TEXTURE2D_DESC desc{};
  source->GetDesc(&desc);
  if (!desc.Width || !desc.Height || desc.MipLevels != 1 || !desc.ArraySize ||
      desc.ArraySize > 4 ||
      static_cast<uint64_t>(desc.Width) * desc.Height * desc.ArraySize * bpp >
          256ull * 1024 * 1024) {
    Log("capture_rejected_size=%ux%u arrays=%u mips=%u", desc.Width,
        desc.Height, desc.ArraySize, desc.MipLevels);
    return {};
  }
  auto result = std::make_shared<Snapshot>();
  device->GetImmediateContext(result->context.GetAddressOf());
  if (!result->context) {
    Log("capture_missing_immediate_context");
    return {};
  }
  if (inspectBindings) {
    try {
      result->bindings = InspectBindings(result->context.Get(), source);
    } catch (...) {
      Log("capture_bindings_failed exception; continuing color capture");
    }
  }
  result->format = format;
  result->desc = desc;
  result->desc.SampleDesc = {1, 0};
  result->desc.Usage = D3D11_USAGE_STAGING;
  result->desc.BindFlags = result->desc.MiscFlags = 0;
  result->desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
  // Depth-compatible staging resources have no depth binding. Use the
  // typeless member of the same format family; CopyResource preserves bits.
  if (format == DXGI_FORMAT_D24_UNORM_S8_UINT)
    result->desc.Format = DXGI_FORMAT_R24G8_TYPELESS;
  if (format == DXGI_FORMAT_D32_FLOAT)
    result->desc.Format = DXGI_FORMAT_R32_TYPELESS;
  auto hr = device->CreateTexture2D(&result->desc, nullptr,
                                    result->texture.GetAddressOf());
  if (FAILED(hr)) {
    Log("capture_staging_failed hr=0x%08lx", static_cast<unsigned long>(hr));
    return {};
  }
  ComPtr<ID3D11Texture2D> resolved;
  if (desc.SampleDesc.Count > 1) {
    UINT support = 0;
    if (FAILED(device->CheckFormatSupport(format, &support)) ||
        !(support & D3D11_FORMAT_SUPPORT_MULTISAMPLE_RESOLVE)) {
      Log("capture_resolve_unsupported format=%u",
          static_cast<unsigned>(format));
      return {};
    }
    auto r = result->desc;
    r.Usage = D3D11_USAGE_DEFAULT;
    r.CPUAccessFlags = 0;
    hr = device->CreateTexture2D(&r, nullptr, resolved.GetAddressOf());
    if (FAILED(hr)) {
      Log("capture_resolve_texture_failed hr=0x%08lx",
          static_cast<unsigned long>(hr));
      return {};
    }
    for (UINT slice = 0; slice < desc.ArraySize; ++slice)
      result->context->ResolveSubresource(resolved.Get(), slice, source, slice,
                                          format);
    result->context->CopyResource(result->texture.Get(), resolved.Get());
  } else
    result->context->CopyResource(result->texture.Get(), source);
  (void)captureCandidates; // D3D9 candidates are recorded independently.
  // Copy commands precede the downstream release in this immediate context.
  // Mapping the owned copy later waits for those commands, not the headset.
  return result;
}
} // namespace ttwvrnr
