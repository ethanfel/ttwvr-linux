// SPDX-License-Identifier: GPL-3.0-only
#include "MenuInput.h"
#include "Support.h"
#include "VRSettings.h"
#include <atomic>
#include <cstdio>
#include <cstring>
#include <xinput.h>
namespace ttwvrnr {
namespace {
std::atomic<unsigned> pending{0};
const volatile WORD *buttons = nullptr;
void Install() {
  // JIP 57.30 replaces FNV's controller wrapper with a jump. Its naked wrapper
  // starts with: mov eax,[esp+8]; mov ecx,<cached state>; cmp eax,ecx; je ...
  // Verify that exact adapter and derive its state pointer from the
  // instruction. Reading the final cached state includes TTWVR and JIP's
  // synthetic buttons, unlike JIP's script-level snapshot, which precedes its
  // input modifiers.
  auto *game = reinterpret_cast<unsigned char *>(GetModuleHandleW(nullptr));
  auto *jip =
      reinterpret_cast<unsigned char *>(GetModuleHandleW(L"jip_nvse.dll"));
  if (!game || !jip)
    return;
  auto imageSize = [](unsigned char *base) -> DWORD {
    auto *dos = reinterpret_cast<IMAGE_DOS_HEADER *>(base);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE)
      return 0;
    auto *nt = reinterpret_cast<IMAGE_NT_HEADERS *>(base + dos->e_lfanew);
    return nt->Signature == IMAGE_NT_SIGNATURE ? nt->OptionalHeader.SizeOfImage
                                               : 0;
  };
  const auto gameSize = imageSize(game), jipSize = imageSize(jip);
  if (gameSize < 0x5f996e + 5 || !jipSize)
    return;
  auto *entry = game + 0x5f996e;
  if (*entry != 0xe9)
    return;
  int32_t displacement;
  std::memcpy(&displacement, entry + 1, sizeof(displacement));
  const uintptr_t targetAddress =
      reinterpret_cast<uintptr_t>(entry + 5) + displacement;
  const auto jipAddress = reinterpret_cast<uintptr_t>(jip);
  if (targetAddress < jipAddress || targetAddress + 13 > jipAddress + jipSize)
    return;
  auto *target = reinterpret_cast<unsigned char *>(targetAddress);
  const unsigned char prefix[] = {0x8b, 0x44, 0x24, 0x08, 0xb9};
  if (std::memcmp(target, prefix, sizeof(prefix)) || target[9] != 0x3b ||
      target[10] != 0xc1 || target[11] != 0x74)
    return;
  uint32_t state;
  std::memcpy(&state, target + 5, sizeof(state));
  const auto gameAddress = reinterpret_cast<uintptr_t>(game);
  if (state < gameAddress ||
      state + sizeof(XINPUT_STATE) > gameAddress + gameSize)
    return;
  buttons = reinterpret_cast<const volatile WORD *>(state + 4);
  Log("menu_input_reader verified_jip_wrapper=1 state_unchanged=1");
}
} // namespace
void PollMenuInput() {
  if (NativeVRSettingsInstalled()) return;
  static bool installed = false;
  if (!installed) {
    installed = true;
    Install();
  }
  DWORD foreground = 0;
  GetWindowThreadProcessId(GetForegroundWindow(), &foreground);
  // Arrows and Enter are already handled by Fallout. Re-injecting them makes
  // selection jump two rows. Only add the missing menu-open and Back keys.
  struct Key {
    int vk;
    unsigned code;
    bool down;
  };
  static Key keys[] = {{VK_F10, 68, false}, {VK_ESCAPE, 1, false}};
  if (buttons) {
    static WORD previous = 0;
    const WORD current = *buttons;
    const auto pressed = current & ~previous;
    previous = current;
    if (pressed & XINPUT_GAMEPAD_X)
      pending.store(1000);
    if (pressed & XINPUT_GAMEPAD_B)
      pending.store(1001);
  }
  for (auto &key : keys) {
    const bool down = foreground == GetCurrentProcessId() &&
                      (GetAsyncKeyState(key.vk) & 0x8000);
    if (down && !key.down)
      pending.store(key.code);
    key.down = down;
  }
  const auto code = pending.exchange(0);
  if (!code)
    return;
  wchar_t path[32768]{};
  GetModuleFileNameW(nullptr, path, 32768);
  std::wstring file(path);
  file.resize(file.find_last_of(L"\\/"));
  file += L"\\Data\\config\\TTWVRNR_input.ini";
  const auto tmp = file + L".tmp";
  static unsigned sequence = GetTickCount() & 0x7fffff;
  auto *out = _wfopen(tmp.c_str(), L"wb");
  if (!out)
    return;
  std::fprintf(out, "[input]\nsequence=%u\nkey=%u\n", ++sequence, code);
  if (std::fclose(out) == 0 &&
      MoveFileExW(tmp.c_str(), file.c_str(), MOVEFILE_REPLACE_EXISTING))
    WritePrivateProfileStringW(nullptr, nullptr, nullptr, file.c_str());
  else
    DeleteFileW(tmp.c_str());
}
} // namespace ttwvrnr
