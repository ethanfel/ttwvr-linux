// SPDX-License-Identifier: GPL-3.0-only
// Minimal xNVSE ABI: no commands/opcodes or save serialization are registered.
#include "TTWVRBuild.h"
#include <MinHook.h>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <windows.h>
struct PluginInfo {
  uint32_t infoVersion;
  const char *name;
  uint32_t version;
};
struct Nvse {
  uint32_t version, runtime, editor, isEditor;
  void *registerCommand, *setOpcode;
  void *(__cdecl *query)(uint32_t);
  uint32_t(__cdecl *handle)();
};
struct Message {
  const char *sender;
  uint32_t type, length;
  void *data;
};
struct Messaging {
  uint32_t version;
  bool(__cdecl *listen)(uint32_t, const char *, void(__cdecl *)(Message *));
};
namespace {
std::wstring folder;
unsigned samples = 0;
using ForceInt = void(__cdecl *)(void *, unsigned, const char *);
ForceInt forceInt = nullptr;
void Log(const char *text) {
  if (auto *f =
          _wfopen((folder + L"\\TTWVRRenderingAddon.log").c_str(), L"ab")) {
    std::fprintf(f, "%s\n", text);
    std::fclose(f);
  }
}
void __cdecl HookForce(void *setting, unsigned value, const char *name) {
  if (name && !std::strcmp(name, "iMultiSample")) {
    value = samples;
    char line[128];
    std::snprintf(line, sizeof(line),
                  "msaa_startup_override requested=%u setting=%p", samples,
                  setting);
    Log(line);
  }
  forceInt(setting, value, name);
  if (name && !std::strcmp(name, "iMultiSample")) {
    char line[128];
    std::snprintf(line, sizeof(line), "msaa_after_force actual=%u",
                  *(reinterpret_cast<unsigned *>(setting) + 1));
    Log(line);
  }
}
void __cdecl OnMessage(Message *message) {
  if (!message || message->type != 0 || forceInt)
    return; // xNVSE PostLoad
  auto module = GetModuleHandleW(L"TTWVR.dll");
  const auto *layout = ttwvrnr::IdentifyTTWVRBuild(module);
  if (!layout) {
    Log("bootstrap_bypass unsupported_or_missing_ttwvr=1");
    return;
  }
  samples =
      GetPrivateProfileIntW(L"antialiasing", L"msaa_samples", 0,
                            (folder + L"\\TTWVRRenderingAddon.ini").c_str());
  if (samples != 0 && samples != 2 && samples != 4 && samples != 8) {
    Log("bootstrap_bypass invalid_msaa=1");
    return;
  }
  auto *base = reinterpret_cast<unsigned char *>(module);
  if (MH_Initialize() != MH_OK ||
      MH_CreateHook(base + layout->forceInt, (void *)HookForce,
                    (void **)&forceInt) != MH_OK ||
      MH_EnableHook(base + layout->forceInt) != MH_OK) {
    Log("bootstrap_bypass hook_failed=1");
    return;
  }
  char line[128];
  std::snprintf(
      line, sizeof(line),
      "bootstrap_ready ttwvr_hash_verified=1 msaa_requested=%u version=%s",
      samples, layout->version);
  Log(line);
}
} // namespace
extern "C" __declspec(dllexport) bool NVSEPlugin_Query(const Nvse *nvse,
                                                       PluginInfo *info) {
  info->infoVersion = 1;
  info->name = "TTWVR Rendering Addon";
  info->version = 1;
  return nvse && !nvse->isEditor && nvse->version >= 6;
}
extern "C" __declspec(dllexport) bool NVSEPlugin_Load(const Nvse *nvse) {
  HMODULE module = nullptr;
  GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                         GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                     reinterpret_cast<LPCWSTR>(&NVSEPlugin_Load), &module);
  wchar_t path[32768]{};
  GetModuleFileNameW(module, path, 32768);
  folder = path;
  folder.resize(folder.find_last_of(L"\\/"));
  auto *messages = static_cast<Messaging *>(nvse->query(2));
  return messages && messages->listen(nvse->handle(), "NVSE", OnMessage);
}
