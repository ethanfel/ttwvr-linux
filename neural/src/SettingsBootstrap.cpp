// SPDX-License-Identifier: GPL-3.0-only
// Minimal xNVSE ABI: no commands/opcodes or save serialization are registered.
#include <MinHook.h>
#include <array>
#include <bcrypt.h>
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
bool Verified(HMODULE module) {
  wchar_t path[32768]{};
  GetModuleFileNameW(module, path, 32768);
  auto file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, nullptr,
                          OPEN_EXISTING, 0, nullptr);
  if (file == INVALID_HANDLE_VALUE)
    return false;
  BCRYPT_ALG_HANDLE alg = nullptr;
  BCRYPT_HASH_HANDLE hash = nullptr;
  bool ok = BCryptOpenAlgorithmProvider(&alg, BCRYPT_SHA256_ALGORITHM, nullptr,
                                        0) >= 0;
  if (ok)
    ok = BCryptCreateHash(alg, &hash, nullptr, 0, nullptr, 0, 0) >= 0;
  std::array<unsigned char, 65536> data{};
  DWORD got = 0;
  while (ok) {
    if (!ReadFile(file, data.data(), data.size(), &got, nullptr)) {
      ok = false;
      break;
    }
    if (!got)
      break;
    ok = BCryptHashData(hash, data.data(), got, 0) >= 0;
  }
  std::array<unsigned char, 32> digest{};
  if (ok)
    ok = BCryptFinishHash(hash, digest.data(), digest.size(), 0) >= 0;
  if (hash)
    BCryptDestroyHash(hash);
  if (alg)
    BCryptCloseAlgorithmProvider(alg, 0);
  CloseHandle(file);
  char hex[65]{};
  for (unsigned i = 0; i < 32; ++i)
    std::snprintf(hex + i * 2, 3, "%02x", digest[i]);
  return ok && !std::strcmp(hex, "8a3a71b48d362984fe346c73b68dfbd476d6e138d6831"
                                 "f26f10c6f506cd03880");
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
  if (!module || !Verified(module)) {
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
      MH_CreateHook(base + 0x4f5a00, (void *)HookForce, (void **)&forceInt) !=
          MH_OK ||
      MH_EnableHook(base + 0x4f5a00) != MH_OK) {
    Log("bootstrap_bypass hook_failed=1");
    return;
  }
  char line[128];
  std::snprintf(line, sizeof(line),
                "bootstrap_ready ttwvr_hash_verified=1 msaa_requested=%u",
                samples);
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
