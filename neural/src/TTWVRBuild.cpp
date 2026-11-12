// SPDX-License-Identifier: GPL-3.0-only
#include "TTWVRBuild.h"
#include <bcrypt.h>
#include <cstdio>
#include <cstring>
namespace ttwvrnr {
namespace {
constexpr TTWVRBuild builds[] = {
    {"1.1.0",
     "8a3a71b48d362984fe346c73b68dfbd476d6e138d6831f26f10c6f506cd03880",
     0x4f5770,
     0x19710e8,
     0x4f5a00,
     0x3f0aa0,
     0x3f2120,
     {0x183afdc, 0x183afe0, 0x183afe4},
     0xf9d848,
     0x521d50,
     0x50b5b0,
     0x50ee90,
     0x559510,
     0x564de0,
     0x51cc90,
     0x51dce0,
     0xef72a4,
     0x19ae10b,
     0x19ae8f0},
    // Supplied TTWVR1.1.3.zip; its bundled meta.ini still says 1.1.2.
    {"1.1.3",
     "4c45d6e5adb5da76dd3d08bf0639d360761d3dc50d891ea0bd19b51e4fb006a0",
     0x510850,
     0x1998098,
     0x510ae0,
     0x40b730,
     0x40d180,
     {0x1861f84, 0x1861f88, 0x1861f8c},
     0xfbd848,
     0x53ce30,
     0x526690,
     0x529f70,
     0x574490,
     0x57fd60,
     0x537d70,
     0x538dc0,
     0xf172b4,
     0x19d50bb,
     0x19d58a0},
};
} // namespace
const TTWVRBuild *IdentifyTTWVRBuild(HMODULE module) {
  if (!module)
    return nullptr;
  wchar_t path[32768]{};
  if (!GetModuleFileNameW(module, path, 32768))
    return nullptr;
  auto file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, nullptr,
                          OPEN_EXISTING, 0, nullptr);
  if (file == INVALID_HANDLE_VALUE)
    return nullptr;
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
  if (!ok)
    return nullptr;
  char hex[65]{};
  for (unsigned i = 0; i < 32; ++i)
    std::snprintf(hex + i * 2, 3, "%02x", digest[i]);
  for (const auto &build : builds)
    if (!std::strcmp(hex, build.sha256))
      return &build;
  return nullptr;
}
} // namespace ttwvrnr
