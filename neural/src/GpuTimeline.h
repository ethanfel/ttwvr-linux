// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "DxvkInterfaces.h"
#include <d3d11.h>
#include <stdexcept>
#include <wrl/client.h>
namespace ttwvrnr {
// Experimental cross-process GPU ordering through opaque Win32 timelines.
// No D3D12-fence metadata is exported. CPU events publish request metadata
// only.
class GpuTimeline {
  Microsoft::WRL::ComPtr<ID3D9VkInteropDevice> d9;
  Microsoft::WRL::ComPtr<IDXGIVkInteropDevice> d11;
  VkDevice device{};
  VkQueue queue{};
  VkSemaphore input{}, output{};
  HANDLE inputHandle = nullptr, outputHandle = nullptr;
  PFN_vkCreateSemaphore vkCreateSemaphore = nullptr;
  PFN_vkQueueSubmit vkQueueSubmit = nullptr;
  PFN_vkGetSemaphoreWin32HandleKHR vkGetSemaphoreWin32HandleKHR = nullptr;
  PFN_vkImportSemaphoreWin32HandleKHR vkImportSemaphoreWin32HandleKHR = nullptr;
  void Check(VkResult r) {
    if (r != VK_SUCCESS)
      throw std::runtime_error("opaque GPU timeline result=" +
                               std::to_string(r));
  }
  template <class T>
  void LoadProc(T &fn, PFN_vkGetDeviceProcAddr get, const char *name) {
    fn = reinterpret_cast<T>(get(device, name));
    if (!fn)
      throw std::runtime_error(name);
  }
  void Load() {
    auto module = GetModuleHandleW(L"winevulkan.dll");
    if (!module)
      throw std::runtime_error("timeline winevulkan");
    auto get = reinterpret_cast<PFN_vkGetDeviceProcAddr>(
        GetProcAddress(module, "vkGetDeviceProcAddr"));
    if (!get)
      throw std::runtime_error("timeline device proc");
    LoadProc(vkCreateSemaphore, get, "vkCreateSemaphore");
    LoadProc(vkQueueSubmit, get, "vkQueueSubmit");
    LoadProc(vkGetSemaphoreWin32HandleKHR, get, "vkGetSemaphoreWin32HandleKHR");
    LoadProc(vkImportSemaphoreWin32HandleKHR, get,
             "vkImportSemaphoreWin32HandleKHR");
  }
  void Make(VkSemaphore &sem, HANDLE &handle, DWORD parent, uint64_t remote) {
    VkExportSemaphoreCreateInfo ei{
        VK_STRUCTURE_TYPE_EXPORT_SEMAPHORE_CREATE_INFO};
    ei.handleTypes = VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_OPAQUE_WIN32_BIT;
    VkSemaphoreTypeCreateInfo type{
        VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO};
    type.semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE;
    type.pNext = parent ? nullptr : &ei;
    VkSemaphoreCreateInfo ci{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
    ci.pNext = &type;
    Check(vkCreateSemaphore(device, &ci, nullptr, &sem));
    if (parent) {
      HANDLE process = OpenProcess(PROCESS_DUP_HANDLE, FALSE, parent);
      if (!process)
        throw std::runtime_error("timeline parent");
      const BOOL ok = DuplicateHandle(
          process, reinterpret_cast<HANDLE>(uintptr_t(remote)),
          GetCurrentProcess(), &handle, 0, FALSE, DUPLICATE_SAME_ACCESS);
      CloseHandle(process);
      if (!ok)
        throw std::runtime_error("timeline duplicate");
      VkImportSemaphoreWin32HandleInfoKHR ii{
          VK_STRUCTURE_TYPE_IMPORT_SEMAPHORE_WIN32_HANDLE_INFO_KHR};
      ii.semaphore = sem;
      ii.handleType = VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_OPAQUE_WIN32_BIT;
      ii.handle = handle;
      Check(vkImportSemaphoreWin32HandleKHR(device, &ii));
    } else {
      VkSemaphoreGetWin32HandleInfoKHR gi{
          VK_STRUCTURE_TYPE_SEMAPHORE_GET_WIN32_HANDLE_INFO_KHR};
      gi.semaphore = sem;
      gi.handleType = VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_OPAQUE_WIN32_BIT;
      Check(vkGetSemaphoreWin32HandleKHR(device, &gi, &handle));
    }
  }
  void Submit(VkSemaphore sem, uint64_t value, bool signal, bool flush) {
    if (flush) {
      if (d9)
        d9->FlushRenderingCommands();
      else
        d11->FlushRenderingCommands();
    }
    VkTimelineSemaphoreSubmitInfo ti{
        VK_STRUCTURE_TYPE_TIMELINE_SEMAPHORE_SUBMIT_INFO};
    VkSubmitInfo si{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    si.pNext = &ti;
    VkPipelineStageFlags stages = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
    if (signal) {
      ti.signalSemaphoreValueCount = 1;
      ti.pSignalSemaphoreValues = &value;
      si.signalSemaphoreCount = 1;
      si.pSignalSemaphores = &sem;
    } else {
      ti.waitSemaphoreValueCount = 1;
      ti.pWaitSemaphoreValues = &value;
      si.waitSemaphoreCount = 1;
      si.pWaitSemaphores = &sem;
      si.pWaitDstStageMask = &stages;
    }
    if (d9)
      d9->LockSubmissionQueue();
    else
      d11->LockSubmissionQueue();
    const auto result = vkQueueSubmit(queue, 1, &si, VK_NULL_HANDLE);
    if (d9)
      d9->ReleaseSubmissionQueue();
    else
      d11->ReleaseSubmissionQueue();
    Check(result);
  }

public:
  // Deliberately retain imported semaphores for process lifetime, like the
  // depth transport. Do not destroy a semaphore referenced by queued GPU work.
  void Init(IDirect3DDevice9 *d) {
    if (FAILED(d->QueryInterface(IID_PPV_ARGS(&d9))))
      throw std::runtime_error("timeline D3D9 interop");
    d9->GetVulkanHandles(nullptr, nullptr, &device);
    d9->GetSubmissionQueue(&queue, nullptr, nullptr);
    Load();
    Make(input, inputHandle, 0, 0);
    Make(output, outputHandle, 0, 0);
  }
  void Init(ID3D11Device *d, DWORD parent, uint64_t in, uint64_t out) {
    if (FAILED(d->QueryInterface(IID_PPV_ARGS(&d11))))
      throw std::runtime_error("timeline D3D11 interop");
    d11->GetVulkanHandles(nullptr, nullptr, &device);
    d11->GetSubmissionQueue(&queue, nullptr);
    Load();
    Make(input, inputHandle, parent, in);
    Make(output, outputHandle, parent, out);
  }
  uint64_t InputHandle() const { return uintptr_t(inputHandle); }
  uint64_t OutputHandle() const { return uintptr_t(outputHandle); }
  void SignalInput(uint64_t value, bool flush = false) { Submit(input, value, true, flush); }
  void WaitInput(uint64_t value) { Submit(input, value, false, true); }
  void SignalOutput(uint64_t value) { Submit(output, value, true, true); }
  void WaitOutput(uint64_t value) { Submit(output, value, false, true); }
};
} // namespace ttwvrnr
