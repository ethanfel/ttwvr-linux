// SPDX-License-Identifier: MIT
// Standalone 32 -> 64-bit Wine opaque Vulkan timeline semaphore probe.
// Build both architectures with Vulkan-Headers; pass probe64.exe to
// probe32.exe.
#define VK_USE_PLATFORM_WIN32_KHR
#define VK_NO_PROTOTYPES
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cwchar>
#include <io.h>
#include <vector>
#include <vulkan/vulkan.h>
#include <windows.h>
static void fail(const char *what, long result) {
  std::fprintf(stderr, "FAIL %s result=%ld\n", what, result);
  std::exit(2);
}
static void check(VkResult r, const char *what) {
  if (r != VK_SUCCESS)
    fail(what, r);
}
struct Vulkan {
  VkInstance instance{};
  VkDevice device{};
  VkQueue queue{};
  VkSemaphore semaphore{};
#define FUNCTIONS(X)                                                           \
  X(vkDestroyInstance)                                                         \
  X(vkEnumeratePhysicalDevices) X(vkGetPhysicalDeviceQueueFamilyProperties)    \
      X(vkCreateDevice) X(vkDestroyDevice) X(vkGetDeviceQueue)                 \
          X(vkCreateSemaphore) X(vkDestroySemaphore)                           \
              X(vkGetSemaphoreWin32HandleKHR)                                  \
                  X(vkImportSemaphoreWin32HandleKHR) X(vkQueueSubmit)          \
                      X(vkWaitSemaphores) X(vkGetSemaphoreCounterValue)        \
                          X(vkDeviceWaitIdle)
#define MEMBER(n) PFN_##n n{};
  FUNCTIONS(MEMBER)
#undef MEMBER
  Vulkan() {
    auto module = LoadLibraryW(L"vulkan-1.dll");
    if (!module)
      fail("LoadLibrary vulkan", GetLastError());
    auto get = reinterpret_cast<PFN_vkGetInstanceProcAddr>(
        GetProcAddress(module, "vkGetInstanceProcAddr"));
    if (!get)
      fail("get proc", 0);
    auto create = reinterpret_cast<PFN_vkCreateInstance>(
        get(nullptr, "vkCreateInstance"));
    VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO};
    app.apiVersion = VK_API_VERSION_1_2;
    VkInstanceCreateInfo ci{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
    ci.pApplicationInfo = &app;
    check(create(&ci, nullptr, &instance), "instance");
#define LOAD(n)                                                                \
  n = reinterpret_cast<PFN_##n>(get(instance, #n));                            \
  if (!n)                                                                      \
    fail(#n, 0);
    LOAD(vkDestroyInstance)
    LOAD(vkEnumeratePhysicalDevices)
        LOAD(vkGetPhysicalDeviceQueueFamilyProperties) LOAD(vkCreateDevice)
            uint32_t count = 0;
    check(vkEnumeratePhysicalDevices(instance, &count, nullptr),
          "physical count");
    if (!count || count > 64)
      fail("physical count range", count);
    std::vector<VkPhysicalDevice> physical(count);
    check(vkEnumeratePhysicalDevices(instance, &count, physical.data()),
          "physical");
    uint32_t families = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(physical[0], &families, nullptr);
    std::vector<VkQueueFamilyProperties> props(families);
    vkGetPhysicalDeviceQueueFamilyProperties(physical[0], &families,
                                             props.data());
    uint32_t family = 0;
    while (family < families &&
           !(props[family].queueFlags & VK_QUEUE_GRAPHICS_BIT))
      ++family;
    if (family == families)
      fail("graphics queue", 0);
    float priority = 1;
    VkDeviceQueueCreateInfo qi{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
    qi.queueFamilyIndex = family;
    qi.queueCount = 1;
    qi.pQueuePriorities = &priority;
    const char *extensions[] = {VK_KHR_EXTERNAL_SEMAPHORE_WIN32_EXTENSION_NAME};
    VkPhysicalDeviceTimelineSemaphoreFeatures timeline{
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TIMELINE_SEMAPHORE_FEATURES};
    timeline.timelineSemaphore = VK_TRUE;
    VkDeviceCreateInfo di{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
    di.pNext = &timeline;
    di.queueCreateInfoCount = 1;
    di.pQueueCreateInfos = &qi;
    di.enabledExtensionCount = 1;
    di.ppEnabledExtensionNames = extensions;
    check(vkCreateDevice(physical[0], &di, nullptr, &device), "device");
    auto gd = reinterpret_cast<PFN_vkGetDeviceProcAddr>(
        get(instance, "vkGetDeviceProcAddr"));
#undef LOAD
#define LOAD(n)                                                                \
  n = reinterpret_cast<PFN_##n>(gd(device, #n));                               \
  if (!n)                                                                      \
    fail(#n, 0);
    LOAD(vkDestroyDevice)
    LOAD(vkGetDeviceQueue) LOAD(vkCreateSemaphore) LOAD(vkDestroySemaphore)
        LOAD(vkGetSemaphoreWin32HandleKHR) LOAD(vkImportSemaphoreWin32HandleKHR)
            LOAD(vkQueueSubmit) LOAD(vkWaitSemaphores)
                LOAD(vkGetSemaphoreCounterValue) LOAD(vkDeviceWaitIdle)
#undef LOAD
                    vkGetDeviceQueue(device, family, 0, &queue);
  }
  void make(bool exporting) {
    VkExportSemaphoreCreateInfo exportInfo{
        VK_STRUCTURE_TYPE_EXPORT_SEMAPHORE_CREATE_INFO};
    exportInfo.handleTypes = VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_OPAQUE_WIN32_BIT;
    VkSemaphoreTypeCreateInfo type{
        VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO};
    type.semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE;
    type.pNext = exporting ? &exportInfo : nullptr;
    VkSemaphoreCreateInfo ci{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
    ci.pNext = &type;
    check(vkCreateSemaphore(device, &ci, nullptr, &semaphore),
          "create timeline");
  }
  void signal(uint64_t value) {
    VkTimelineSemaphoreSubmitInfo timeline{
        VK_STRUCTURE_TYPE_TIMELINE_SEMAPHORE_SUBMIT_INFO};
    timeline.signalSemaphoreValueCount = 1;
    timeline.pSignalSemaphoreValues = &value;
    VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    submit.pNext = &timeline;
    submit.signalSemaphoreCount = 1;
    submit.pSignalSemaphores = &semaphore;
    check(vkQueueSubmit(queue, 1, &submit, VK_NULL_HANDLE), "queue signal");
  }
  void wait(uint64_t value) {
    VkSemaphoreWaitInfo wait{VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO};
    wait.semaphoreCount = 1;
    wait.pSemaphores = &semaphore;
    wait.pValues = &value;
    check(vkWaitSemaphores(device, &wait, 5000000000ull), "wait timeline");
    uint64_t observed = 0;
    check(vkGetSemaphoreCounterValue(device, semaphore, &observed), "counter");
    if (observed < value)
      fail("counter below required value", long(observed));
  }
  ~Vulkan() {
    if (device) {
      vkDeviceWaitIdle(device);
      if (semaphore)
        vkDestroySemaphore(device, semaphore, nullptr);
      vkDestroyDevice(device, nullptr);
    }
    if (instance)
      vkDestroyInstance(instance, nullptr);
  }
};
int wmain(int argc, wchar_t **argv) {
  SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
  bool child = argc == 4 && !std::wcscmp(argv[1], L"--child");
  if (!child && argc != 2)
    return 2;
  if (!std::freopen(child ? "opaque-timeline-child.txt"
                          : "opaque-timeline-parent.txt",
                    "w", stdout))
    return 2;
  if (_dup2(_fileno(stdout), _fileno(stderr)))
    return 2;
  std::setvbuf(stdout, nullptr, _IONBF, 0);
  Vulkan vk;
  vk.make(!child);
  HANDLE shared = nullptr;
  PROCESS_INFORMATION info{};
  if (child) {
    HANDLE parent = OpenProcess(PROCESS_DUP_HANDLE, FALSE,
                                std::wcstoul(argv[2], nullptr, 10));
    if (!parent)
      fail("parent process", GetLastError());
    HANDLE remote = reinterpret_cast<HANDLE>(
        uintptr_t(std::wcstoull(argv[3], nullptr, 10)));
    if (!DuplicateHandle(parent, remote, GetCurrentProcess(), &shared, 0, FALSE,
                         DUPLICATE_SAME_ACCESS))
      fail("duplicate", GetLastError());
    CloseHandle(parent);
    VkImportSemaphoreWin32HandleInfoKHR import{
        VK_STRUCTURE_TYPE_IMPORT_SEMAPHORE_WIN32_HANDLE_INFO_KHR};
    import.semaphore = vk.semaphore;
    import.handleType = VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_OPAQUE_WIN32_BIT;
    import.handle = shared;
    check(vk.vkImportSemaphoreWin32HandleKHR(vk.device, &import),
          "import opaque timeline");
  } else {
    VkSemaphoreGetWin32HandleInfoKHR exportInfo{
        VK_STRUCTURE_TYPE_SEMAPHORE_GET_WIN32_HANDLE_INFO_KHR};
    exportInfo.semaphore = vk.semaphore;
    exportInfo.handleType = VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_OPAQUE_WIN32_BIT;
    check(vk.vkGetSemaphoreWin32HandleKHR(vk.device, &exportInfo, &shared),
          "export opaque timeline");
    wchar_t command[32768];
    if (std::swprintf(command, 32768, L"\"%ls\" --child %lu %llu", argv[1],
                      GetCurrentProcessId(),
                      (unsigned long long)(uintptr_t)shared) < 0)
      return 2;
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    if (!CreateProcessW(argv[1], command, nullptr, nullptr, FALSE, 0, nullptr,
                        nullptr, &startup, &info))
      fail("create child", GetLastError());
  }
  for (uint64_t i = 1; i <= 32; ++i) {
    if (child) {
      vk.wait(2 * i - 1);
      vk.signal(2 * i);
    } else {
      vk.signal(2 * i - 1);
      vk.wait(2 * i);
    }
  }
  vk.wait(64);
  if (!child) {
    if (WaitForSingleObject(info.hProcess, 10000) != WAIT_OBJECT_0) {
      TerminateProcess(info.hProcess, 4);
      fail("child timeout", 4);
    }
    DWORD code = 0;
    if (!GetExitCodeProcess(info.hProcess, &code) || code)
      fail("child exit", code);
    CloseHandle(info.hThread);
    CloseHandle(info.hProcess);
  }
  CloseHandle(shared);
  std::printf("PASS %zu-bit opaque timeline: 32 GPU-signalled round trips, "
              "counter=64\n",
              sizeof(void *) * 8);
  return 0;
}
