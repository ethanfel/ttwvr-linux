// SPDX-License-Identifier: GPL-3.0-only
#include "DepthTransfer.h"
#include "DxvkInterfaces.h"
#include "Support.h"
#include <memory>
#include <stdexcept>
#include <utility>
#include <wrl/client.h>
namespace ttwvrnr {
namespace {
using Microsoft::WRL::ComPtr;
void Check(VkResult r) {
  if (r != VK_SUCCESS)
    throw std::runtime_error("depth GPU transfer");
}
struct Transfer {
  ComPtr<IDirect3DDevice9> owner;
  ComPtr<ID3D9VkInteropDevice> interop;
  VkDevice device{};
  VkQueue queue{};
  VkCommandPool pool{};
  VkCommandBuffer cmd{};
  VkBuffer buffer{};
  VkDeviceMemory memory{};
  VkFence fence{};
  uint32_t width = 0, height = 0;
  bool submitted = false;
#define PROC(n) PFN_##n n = nullptr;
  PROC(vkCreateBuffer)
  PROC(vkGetBufferMemoryRequirements)
  PROC(vkAllocateMemory) PROC(vkBindBufferMemory) PROC(vkCreateCommandPool)
      PROC(vkAllocateCommandBuffers) PROC(vkBeginCommandBuffer)
          PROC(vkEndCommandBuffer) PROC(vkResetCommandPool) PROC(vkCreateFence)
              PROC(vkResetFences) PROC(vkWaitForFences)
                  PROC(vkCmdPipelineBarrier) PROC(vkCmdCopyImageToBuffer)
                      PROC(vkCmdCopyBufferToImage) PROC(vkQueueSubmit)
#undef PROC
                          void Init(IDirect3DDevice9 *d, uint32_t w,
                                    uint32_t h) {
    owner = d;
    if (FAILED(d->QueryInterface(IID_PPV_ARGS(&interop))))
      throw std::runtime_error("depth DXVK interface");
    width = w;
    height = h;
    VkInstance instance{};
    VkPhysicalDevice physical{};
    uint32_t family = 0;
    interop->GetVulkanHandles(&instance, &physical, &device);
    interop->GetSubmissionQueue(&queue, nullptr, &family);
    auto module = GetModuleHandleW(L"winevulkan.dll");
    if (!module)
      throw std::runtime_error("winevulkan");
    auto gd = reinterpret_cast<PFN_vkGetDeviceProcAddr>(
        GetProcAddress(module, "vkGetDeviceProcAddr"));
    auto gi = reinterpret_cast<PFN_vkGetInstanceProcAddr>(
        GetProcAddress(module, "vkGetInstanceProcAddr"));
    if (!gd || !gi)
      throw std::runtime_error("Vulkan procedures");
#define LOAD(n)                                                                \
  n = reinterpret_cast<PFN_##n>(gd(device, #n));                               \
  if (!n)                                                                      \
    throw std::runtime_error(#n);
    LOAD(vkCreateBuffer)
    LOAD(vkGetBufferMemoryRequirements)
    LOAD(vkAllocateMemory) LOAD(vkBindBufferMemory) LOAD(vkCreateCommandPool)
        LOAD(vkAllocateCommandBuffers) LOAD(vkBeginCommandBuffer) LOAD(
            vkEndCommandBuffer) LOAD(vkResetCommandPool) LOAD(vkCreateFence)
            LOAD(vkResetFences) LOAD(vkWaitForFences) LOAD(vkCmdPipelineBarrier)
                LOAD(vkCmdCopyImageToBuffer) LOAD(vkCmdCopyBufferToImage)
                    LOAD(vkQueueSubmit)
#undef LOAD
                        auto gm = reinterpret_cast<
                            PFN_vkGetPhysicalDeviceMemoryProperties>(gi(
                            instance, "vkGetPhysicalDeviceMemoryProperties"));
    if (!gm)
      throw std::runtime_error("Vulkan memory procedures");
    VkPhysicalDeviceMemoryProperties props{};
    gm(physical, &props);
    VkBufferCreateInfo bi{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    bi.size = VkDeviceSize(w) * h * 4;
    bi.usage =
        VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    Check(vkCreateBuffer(device, &bi, nullptr, &buffer));
    VkMemoryRequirements req{};
    vkGetBufferMemoryRequirements(device, buffer, &req);
    unsigned type = props.memoryTypeCount;
    for (unsigned i = 0; i < props.memoryTypeCount; ++i)
      if ((req.memoryTypeBits & (1u << i)) &&
          (props.memoryTypes[i].propertyFlags &
           VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)) {
        type = i;
        break;
      }
    if (type == props.memoryTypeCount)
      throw std::runtime_error("depth device-local memory");
    VkMemoryAllocateInfo ai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    ai.allocationSize = req.size;
    ai.memoryTypeIndex = type;
    Check(vkAllocateMemory(device, &ai, nullptr, &memory));
    Check(vkBindBufferMemory(device, buffer, memory, 0));
    VkCommandPoolCreateInfo pi{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
    pi.queueFamilyIndex = family;
    Check(vkCreateCommandPool(device, &pi, nullptr, &pool));
    VkCommandBufferAllocateInfo ca{
        VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    ca.commandPool = pool;
    ca.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    ca.commandBufferCount = 1;
    Check(vkAllocateCommandBuffers(device, &ca, &cmd));
    VkFenceCreateInfo fi{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
    Check(vkCreateFence(device, &fi, nullptr, &fence));
  }
  void Copy(IDirect3DDevice9 *d, IDirect3DSurface9 *source,
            IDirect3DSurface9 *output) {
    if (d != owner.Get())
      throw std::runtime_error("depth device changed");
    if (submitted)
      Check(vkWaitForFences(device, 1, &fence, VK_TRUE, 5000000000ull));
    ComPtr<ID3D9VkInteropTexture> src, dst;
    if (FAILED(source->QueryInterface(IID_PPV_ARGS(&src))) ||
        FAILED(output->QueryInterface(IID_PPV_ARGS(&dst))))
      throw std::runtime_error("depth image interop");
    VkImage image[2]{};
    VkImageLayout layout[2]{};
    VkImageCreateInfo desc[2] = {{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO},
                                 {VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO}};
    if (FAILED(src->GetVulkanImageInfo(&image[0], &layout[0], &desc[0])) ||
        FAILED(dst->GetVulkanImageInfo(&image[1], &layout[1], &desc[1])))
      throw std::runtime_error("depth image info");
    if (desc[0].format != VK_FORMAT_D24_UNORM_S8_UINT ||
        desc[1].format != VK_FORMAT_R8G8B8A8_UNORM)
      throw std::runtime_error("depth unverified format");
    for (auto &v : desc)
      if (v.extent.width != width || v.extent.height != height ||
          v.extent.depth != 1 || v.samples != VK_SAMPLE_COUNT_1_BIT)
        throw std::runtime_error("depth unverified dimensions");
    if (!(desc[0].usage & VK_IMAGE_USAGE_TRANSFER_SRC_BIT) ||
        !(desc[1].usage & VK_IMAGE_USAGE_TRANSFER_DST_BIT))
      throw std::runtime_error("depth copy usage");
    Check(vkResetCommandPool(device, pool, 0));
    VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    Check(vkBeginCommandBuffer(cmd, &begin));
    VkImageMemoryBarrier b[2]{};
    for (unsigned i = 0; i < 2; ++i) {
      b[i].sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
      b[i].srcAccessMask =
          VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
      b[i].dstAccessMask =
          i ? VK_ACCESS_TRANSFER_WRITE_BIT : VK_ACCESS_TRANSFER_READ_BIT;
      b[i].oldLayout = layout[i];
      b[i].newLayout = i ? VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL
                         : VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
      b[i].srcQueueFamilyIndex = b[i].dstQueueFamilyIndex =
          VK_QUEUE_FAMILY_IGNORED;
      b[i].image = image[i];
      b[i].subresourceRange = {
          static_cast<VkImageAspectFlags>(i ? VK_IMAGE_ASPECT_COLOR_BIT
                                            : VK_IMAGE_ASPECT_DEPTH_BIT |
                                                  VK_IMAGE_ASPECT_STENCIL_BIT),
          0, 1, 0, 1};
    }
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
                         VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0,
                         nullptr, 2, b);
    VkBufferImageCopy region{};
    region.imageSubresource = {VK_IMAGE_ASPECT_DEPTH_BIT, 0, 0, 1};
    region.imageExtent = desc[0].extent;
    vkCmdCopyImageToBuffer(cmd, image[0], VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                           buffer, 1, &region);
    VkBufferMemoryBarrier bb{VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER};
    bb.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    bb.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    bb.srcQueueFamilyIndex = bb.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    bb.buffer = buffer;
    bb.size = VK_WHOLE_SIZE;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT,
                         VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 1, &bb,
                         0, nullptr);
    region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    vkCmdCopyBufferToImage(cmd, buffer, image[1],
                           VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
    for (auto &v : b) {
      std::swap(v.oldLayout, v.newLayout);
      v.srcAccessMask = v.dstAccessMask;
      v.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
    }
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT,
                         VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 0, nullptr, 0,
                         nullptr, 2, b);
    Check(vkEndCommandBuffer(cmd));
    Check(vkResetFences(device, 1, &fence));
    interop->FlushRenderingCommands();
    interop->LockSubmissionQueue();
    VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    submit.commandBufferCount = 1;
    submit.pCommandBuffers = &cmd;
    const auto result = vkQueueSubmit(queue, 1, &submit, fence);
    interop->ReleaseSubmissionQueue();
    Check(result);
    submitted = true;
    Check(vkWaitForFences(device, 1, &fence, VK_TRUE, 5000000000ull));
  }
};
// Lifetime is deliberately process-bound for this experimental adapter. The
// game device can outlive OpenXR sessions; never destroy in-flight Vulkan work.
Transfer *transfer = nullptr;
bool failed = false;
} // namespace
bool TransferDepthBits(IDirect3DDevice9 *d, IDirect3DSurface9 *depth,
                       IDirect3DSurface9 *packed) {
  if (failed || !d || !depth || !packed)
    return false;
  try {
    if (!transfer) {
      D3DSURFACE_DESC desc{};
      if (FAILED(depth->GetDesc(&desc)))
        return false;
      transfer = new Transfer;
      transfer->Init(d, desc.Width, desc.Height);
    }
    transfer->Copy(d, depth, packed);
    return true;
  } catch (const std::exception &e) {
    Log("depth_transport_failed reason=%s bypass=1", e.what());
    failed = true;
    return false;
  }
}
} // namespace ttwvrnr
