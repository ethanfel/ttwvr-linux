// SPDX-License-Identifier: GPL-3.0-only
#include "SceneCapture.h"
#include "DxvkInterfaces.h"
#include "Support.h"
#include <cmath>
#include <cstdio>
#include <memory>
#include <stdexcept>
#include <vector>
#include <wrl/client.h>
namespace ttwvrnr {
namespace {
using Microsoft::WRL::ComPtr;
struct DdsHeader {
  uint32_t magic = 0x20534444, size = 124, flags = 0x100f, height = 0,
           width = 0, pitch = 0, depth = 0, mips = 0, reserved[11]{},
           pfSize = 32, pfFlags = 4, fourcc = 0x30315844, bits = 0, r = 0,
           g = 0, b = 0, a = 0, caps = 0x1000, caps2 = 0, caps3 = 0, caps4 = 0,
           reserved2 = 0, format = 0, dimension = 3, misc = 0, arraySize = 1,
           alpha = 0;
};
static_assert(sizeof(DdsHeader) == 148);
void Save(const std::wstring &path, unsigned width, unsigned height,
          unsigned format, unsigned bpp, const void *data, unsigned rowPitch) {
  FILE *out = _wfopen(path.c_str(), L"wb");
  if (!out)
    return;
  DdsHeader h;
  h.width = width;
  h.height = height;
  h.pitch = width * bpp;
  h.format = format;
  bool ok = fwrite(&h, sizeof(h), 1, out) == 1;
  for (unsigned y = 0; y < height && ok; ++y)
    ok = fwrite(static_cast<const char *>(data) + size_t(y) * rowPitch, 1,
                h.pitch, out) == h.pitch;
  if (fclose(out))
    ok = false;
  Log("scene_capture_saved format=%u size=%ux%u ok=%d", format, width, height,
      ok);
  if (!ok)
    DeleteFileW(path.c_str());
}
struct Color {
  ComPtr<IDirect3DDevice9> device;
  ComPtr<IDirect3DSurface9> copy;
  std::wstring path;
  D3DSURFACE_DESC desc{};
};
std::vector<Color> colors;
std::wstring Prefix(uint64_t frame, unsigned pass) {
  return CaptureDirectory() + L"\\scene-frame-" + std::to_wstring(frame) +
         L"-pass-" + std::to_wstring(pass);
}
void Copy(IDirect3DDevice9 *d, IDirect3DSurface9 *surface,
          const std::wstring &path) {
  if (!surface || colors.size() >= 32)
    return;
  D3DSURFACE_DESC desc{};
  if (FAILED(surface->GetDesc(&desc)) ||
      desc.MultiSampleType != D3DMULTISAMPLE_NONE)
    return;
  if (desc.Format != D3DFMT_A16B16G16R16F && desc.Format != D3DFMT_X8R8G8B8 &&
      desc.Format != D3DFMT_A8R8G8B8)
    return;
  if (uint64_t(desc.Width) * desc.Height * 8 > 64ull * 1024 * 1024)
    return;
  Color c;
  c.device = d;
  c.desc = desc;
  c.path = path;
  auto hr =
      d->CreateRenderTarget(desc.Width, desc.Height, desc.Format,
                            D3DMULTISAMPLE_NONE, 0, FALSE, &c.copy, nullptr);
  if (SUCCEEDED(hr))
    hr = d->StretchRect(surface, nullptr, c.copy.Get(), nullptr, D3DTEXF_NONE);
  if (FAILED(hr)) {
    Log("scene_copy_failed hr=%08lx", (unsigned long)hr);
    return;
  }
  colors.push_back(std::move(c));
}
void CopyTexture(IDirect3DDevice9 *d, unsigned slot, const std::wstring &path) {
  ComPtr<IDirect3DBaseTexture9> base;
  d->GetTexture(slot, &base);
  ComPtr<IDirect3DTexture9> t;
  if (!base || FAILED(base.As(&t)))
    return;
  ComPtr<IDirect3DSurface9> surface;
  t->GetSurfaceLevel(0, &surface);
  Copy(d, surface.Get(), path);
}
// Owned diagnostic depth readback. Flush -> same queue copy -> restore layout.
// No runtime-owned OpenXR image is accessed here.
struct Depth {
  VkDevice device{};
  VkBuffer buffer{};
  VkDeviceMemory memory{};
  VkCommandPool pool{};
  VkFence fence{};
  unsigned width = 0, height = 0;
  VkFormat format{};
  std::wstring path;
// clang-format off
#define PROC(name) PFN_##name name = nullptr;
  PROC(vkCreateBuffer)
  PROC(vkGetBufferMemoryRequirements)
  PROC(vkAllocateMemory)
  PROC(vkBindBufferMemory)
  PROC(vkCreateCommandPool)
  PROC(vkAllocateCommandBuffers)
  PROC(vkBeginCommandBuffer)
  PROC(vkEndCommandBuffer)
  PROC(vkCmdPipelineBarrier)
  PROC(vkCmdCopyImageToBuffer)
  PROC(vkCreateFence)
  PROC(vkQueueSubmit)
  PROC(vkWaitForFences)
  PROC(vkMapMemory)
  PROC(vkUnmapMemory)
  PROC(vkDestroyBuffer)
  PROC(vkFreeMemory)
  PROC(vkDestroyCommandPool)
  PROC(vkDestroyFence)
#undef PROC
// clang-format on
  ~Depth() {
    if (fence && vkWaitForFences) {
      auto r = vkWaitForFences(device, 1, &fence, VK_TRUE, 5000000000ull);
      if (r != VK_SUCCESS && r != VK_ERROR_DEVICE_LOST) {
        Log("scene_depth_teardown_pending resources_retained_until_exit=1");
        return;
      }
    }
    if (pool)
      vkDestroyCommandPool(device, pool, nullptr);
    if (buffer)
      vkDestroyBuffer(device, buffer, nullptr);
    if (memory)
      vkFreeMemory(device, memory, nullptr);
    if (fence)
      vkDestroyFence(device, fence, nullptr);
  }
  void Read() {
    if (vkWaitForFences(device, 1, &fence, VK_TRUE, 5000000000ull) !=
        VK_SUCCESS) {
      Log("scene_depth_timeout");
      return;
    }
    void *data = nullptr;
    if (vkMapMemory(device, memory, 0, VK_WHOLE_SIZE, 0, &data) != VK_SUCCESS)
      return;
    // D24_UNORM depth copies use a 32-bit X8_D24 element; retain raw bits for
    // verification.
    Save(path, width, height,
         format == VK_FORMAT_D32_SFLOAT_S8_UINT ||
                 format == VK_FORMAT_D32_SFLOAT
             ? 41
             : 42,
         4, data, width * 4);
    vkUnmapMemory(device, memory);
  }
};
std::vector<std::unique_ptr<Depth>> depths;
void Check(VkResult r) {
  if (r != VK_SUCCESS)
    throw std::runtime_error("Vulkan depth capture");
}
void CopyDepth(IDirect3DDevice9 *d, IDirect3DSurface9 *surface,
               const std::wstring &path) {
  if (!surface || depths.size() >= 4)
    return;
  try {
    ComPtr<ID3D9VkInteropDevice> interop;
    ComPtr<ID3D9VkInteropTexture> texture;
    if (FAILED(d->QueryInterface(IID_PPV_ARGS(&interop))) ||
        FAILED(surface->QueryInterface(IID_PPV_ARGS(&texture)))) {
      Log("scene_depth_no_interop");
      return;
    }
    auto p = std::make_unique<Depth>();
    p->path = path;
    VkInstance instance{};
    VkPhysicalDevice physical{};
    interop->GetVulkanHandles(&instance, &physical, &p->device);
    VkQueue queue{};
    uint32_t family = 0;
    interop->GetSubmissionQueue(&queue, nullptr, &family);
    VkImage image{};
    VkImageLayout layout{};
    VkImageCreateInfo info{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    if (FAILED(texture->GetVulkanImageInfo(&image, &layout, &info)))
      return;
    p->width = info.extent.width;
    p->height = info.extent.height;
    p->format = info.format;
    Log("scene_depth_source format=%u layout=%u usage=%u size=%ux%u",
        info.format, layout, info.usage, p->width, p->height);
    if (VkDeviceSize(p->width) * p->height * 4 > 64ull * 1024 * 1024 ||
        info.samples != VK_SAMPLE_COUNT_1_BIT ||
        !(info.usage & VK_IMAGE_USAGE_TRANSFER_SRC_BIT) ||
        (info.format != VK_FORMAT_D24_UNORM_S8_UINT &&
         info.format != VK_FORMAT_D32_SFLOAT_S8_UINT &&
         info.format != VK_FORMAT_D32_SFLOAT))
      return;
    const auto module = GetModuleHandleW(L"winevulkan.dll");
    if (!module)
      return;
    const auto getInstance = reinterpret_cast<PFN_vkGetInstanceProcAddr>(
        GetProcAddress(module, "vkGetInstanceProcAddr"));
    const auto getDevice = reinterpret_cast<PFN_vkGetDeviceProcAddr>(
        GetProcAddress(module, "vkGetDeviceProcAddr"));
    if (!getInstance || !getDevice)
      return;
#define LOAD(name)                                                             \
  p->name = reinterpret_cast<PFN_##name>(getDevice(p->device, #name));         \
  if (!p->name)                                                                \
    return;
    LOAD(vkCreateBuffer)
    LOAD(vkGetBufferMemoryRequirements)
    LOAD(vkAllocateMemory) LOAD(vkBindBufferMemory) LOAD(vkCreateCommandPool)
        LOAD(vkAllocateCommandBuffers) LOAD(vkBeginCommandBuffer)
            LOAD(vkEndCommandBuffer) LOAD(vkCmdPipelineBarrier) LOAD(
                vkCmdCopyImageToBuffer) LOAD(vkCreateFence) LOAD(vkQueueSubmit)
                LOAD(vkWaitForFences) LOAD(vkMapMemory) LOAD(vkUnmapMemory)
                    LOAD(vkDestroyBuffer) LOAD(vkFreeMemory) LOAD(
                        vkDestroyCommandPool) LOAD(vkDestroyFence)
#undef LOAD
                        auto getMemory = reinterpret_cast<
                            PFN_vkGetPhysicalDeviceMemoryProperties>(
                            getInstance(instance,
                                        "vkGetPhysicalDeviceMemoryProperties"));
    if (!getMemory)
      return;
    VkPhysicalDeviceMemoryProperties props{};
    getMemory(physical, &props);
    VkBufferCreateInfo bi{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    bi.size = VkDeviceSize(p->width) * p->height * 4;
    bi.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    Check(p->vkCreateBuffer(p->device, &bi, nullptr, &p->buffer));
    VkMemoryRequirements req{};
    p->vkGetBufferMemoryRequirements(p->device, p->buffer, &req);
    unsigned type = props.memoryTypeCount;
    for (unsigned i = 0; i < props.memoryTypeCount; ++i)
      if ((req.memoryTypeBits & (1u << i)) &&
          (props.memoryTypes[i].propertyFlags &
           (VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
            VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)) ==
              (VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
               VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)) {
        type = i;
        break;
      }
    if (type == props.memoryTypeCount)
      return;
    VkMemoryAllocateInfo ai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    ai.allocationSize = req.size;
    ai.memoryTypeIndex = type;
    Check(p->vkAllocateMemory(p->device, &ai, nullptr, &p->memory));
    Check(p->vkBindBufferMemory(p->device, p->buffer, p->memory, 0));
    VkCommandPoolCreateInfo pi{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
    pi.queueFamilyIndex = family;
    Check(p->vkCreateCommandPool(p->device, &pi, nullptr, &p->pool));
    VkCommandBufferAllocateInfo ca{
        VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    ca.commandPool = p->pool;
    ca.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    ca.commandBufferCount = 1;
    VkCommandBuffer cmd{};
    Check(p->vkAllocateCommandBuffers(p->device, &ca, &cmd));
    VkCommandBufferBeginInfo cb{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    cb.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    Check(p->vkBeginCommandBuffer(cmd, &cb));
    VkImageMemoryBarrier barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
    barrier.srcAccessMask =
        VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    barrier.oldLayout = layout;
    barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    barrier.srcQueueFamilyIndex = barrier.dstQueueFamilyIndex =
        VK_QUEUE_FAMILY_IGNORED;
    barrier.image = image;
    barrier.subresourceRange = {
        VK_IMAGE_ASPECT_DEPTH_BIT |
            (info.format == VK_FORMAT_D32_SFLOAT
                 ? 0u
                 : static_cast<unsigned>(VK_IMAGE_ASPECT_STENCIL_BIT)),
        0, 1, 0, 1};
    p->vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
                            VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0,
                            nullptr, 1, &barrier);
    VkBufferImageCopy region{};
    region.imageSubresource = {VK_IMAGE_ASPECT_DEPTH_BIT, 0, 0, 1};
    region.imageExtent = info.extent;
    p->vkCmdCopyImageToBuffer(cmd, image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                              p->buffer, 1, &region);
    VkBufferMemoryBarrier host{VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER};
    host.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    host.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
    host.srcQueueFamilyIndex = host.dstQueueFamilyIndex =
        VK_QUEUE_FAMILY_IGNORED;
    host.buffer = p->buffer;
    host.size = VK_WHOLE_SIZE;
    p->vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT,
                            VK_PIPELINE_STAGE_HOST_BIT, 0, 0, nullptr, 1, &host,
                            0, nullptr);
    std::swap(barrier.oldLayout, barrier.newLayout);
    barrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    barrier.dstAccessMask =
        VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
    p->vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT,
                            VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 0, nullptr,
                            0, nullptr, 1, &barrier);
    Check(p->vkEndCommandBuffer(cmd));
    VkFenceCreateInfo fi{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
    // Only retain a fence after a successful submission; an unsignalled fence
    // must not block teardown.
    VkFence fence{};
    Check(p->vkCreateFence(p->device, &fi, nullptr, &fence));
    interop->FlushRenderingCommands();
    interop->LockSubmissionQueue();
    VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    submit.commandBufferCount = 1;
    submit.pCommandBuffers = &cmd;
    const auto result = p->vkQueueSubmit(queue, 1, &submit, fence);
    interop->ReleaseSubmissionQueue();
    if (result != VK_SUCCESS) {
      p->vkDestroyFence(p->device, fence, nullptr);
      Check(result);
    }
    p->fence = fence;
    depths.push_back(std::move(p));
  } catch (...) {
    Log("scene_depth_capture_failed bypass=1");
  }
}
} // namespace
void CaptureExtraSurface(IDirect3DDevice9 *d, IDirect3DSurface9 *s,
                         const std::wstring &path) {
  Copy(d, s, path);
}
void CaptureSceneBeforeTone(IDirect3DDevice9 *d, uint64_t frame,
                            unsigned pass) {
  const auto prefix = Prefix(frame, pass);
  CopyTexture(d, 1, prefix + L"-hdr.dds");
  CopyTexture(d, 0, prefix + L"-bloom.dds");
  CopyTexture(d, 2, prefix + L"-luminance-candidate.dds");
  ComPtr<IDirect3DSurface9> depth;
  d->GetDepthStencilSurface(&depth);
  CopyDepth(d, depth.Get(), prefix + L"-depth-raw.dds");
  float constants[32][4]{};
  d->GetPixelShaderConstantF(0, &constants[0][0], 32);
  DWORD srgbWrite = 0, srgbRead[3]{};
  d->GetRenderState(D3DRS_SRGBWRITEENABLE, &srgbWrite);
  for (unsigned i = 0; i < 3; ++i)
    d->GetSamplerState(i, D3DSAMP_SRGBTEXTURE, &srgbRead[i]);
  FILE *out = _wfopen((prefix + L"-constants.json").c_str(), L"wb");
  if (out) {
    fprintf(out,
            "{\"frame\":%llu,\"pass\":%u,\"eye_verified\":false,\"srgb_write\":"
            "%lu,\"srgb_read\":[%lu,%lu,%lu],\"pixel_constants\":[",
            (unsigned long long)frame, pass, srgbWrite, srgbRead[0],
            srgbRead[1], srgbRead[2]);
    for (unsigned i = 0; i < 32; ++i) {
      fputs(i ? ",[" : "[", out);
      for (unsigned j = 0; j < 4; ++j) {
        if (j)
          fputc(',', out);
        if (std::isfinite(constants[i][j]))
          fprintf(out, "%.9g", constants[i][j]);
        else
          fputs("null", out);
      }
      fputc(']', out);
    }
    fputs("]}\n", out);
    fclose(out);
  }
}
void CaptureSceneAfterTone(IDirect3DDevice9 *d, uint64_t frame, unsigned pass) {
  ComPtr<IDirect3DSurface9> target;
  d->GetRenderTarget(0, &target);
  Copy(d, target.Get(), Prefix(frame, pass) + L"-ldr.dds");
}
void FinishSceneCapture() {
  for (auto &c : colors) {
    ComPtr<IDirect3DSurface9> staging;
    auto hr = c.device->CreateOffscreenPlainSurface(
        c.desc.Width, c.desc.Height, c.desc.Format, D3DPOOL_SYSTEMMEM, &staging,
        nullptr);
    if (SUCCEEDED(hr))
      hr = c.device->GetRenderTargetData(c.copy.Get(), staging.Get());
    if (FAILED(hr)) {
      Log("scene_readback_failed hr=%08lx", (unsigned long)hr);
      continue;
    }
    D3DLOCKED_RECT map{};
    if (FAILED(staging->LockRect(&map, nullptr, D3DLOCK_READONLY)))
      continue;
    const bool hdr = c.desc.Format == D3DFMT_A16B16G16R16F;
    Save(c.path, c.desc.Width, c.desc.Height,
         hdr                                ? 10
         : c.desc.Format == D3DFMT_X8R8G8B8 ? 88
                                            : 87,
         hdr ? 8 : 4, map.pBits, map.Pitch);
    staging->UnlockRect();
  }
  colors.clear();
  for (auto &d : depths)
    d->Read();
  depths.clear();
}
} // namespace ttwvrnr
