// SPDX-License-Identifier: GPL-3.0-only
// Extend only the hash-verified TTWVR native StartMenu options.
#include "VRSettings.h"
#include "MenuControl.h"
#include "Support.h"
#include "TTWVRBuild.h"
#include <MinHook.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <cstring>
#include <shlobj.h>
namespace ttwvrnr {
namespace {
struct Option {
  void *vtable;
  const char *name;
  void (*followup)();
  unsigned flags;
  const char *templateName;
  unsigned value, unknown18, type, count, width;
  const char **names;
  void(__cdecl *selected)(Option *);
};
static_assert(sizeof(Option) == 0x30);
using Constructor = Option *(__cdecl *)(Option *, const char *,
                                        void(__cdecl *)(Option *), unsigned,
                                        unsigned, unsigned, unsigned, ...);
using Append = unsigned(__thiscall *)(void *, Option **);
using Build = void(__cdecl *)();
Build build = nullptr;
unsigned char *ttwvr = nullptr;
const TTWVRBuild *layout = nullptr;
std::atomic<bool> installed{false};
std::atomic<unsigned> actualSamples{0};
std::array<Option *, 9> rows{};
const unsigned samples[] = {0, 0, 2, 4, 8};
const char *aaNames[] = {"Off", "DLAA", "MSAA 2x (restart)",
                         "MSAA 4x (restart)", "MSAA 8x (restart)"};
const char *srNames[] = {"Off", "Quality", "Balanced", "Performance"};
const char *toggleNames[] = {"Off", "On"};
const char *modelNames[] = {"A", "B"};
const char *scaleNames[] = {"25%", "50%", "75%", "100%"};
const char *percentNames[] = {"0%",  "10%", "20%", "30%", "40%", "50%",
                              "60%", "70%", "80%", "90%", "100%"};
std::wstring Ini() { return Directory() + L"\\TTWVRNeuralRendering.ini"; }
unsigned RequestedMsaa() {
  auto v =
      GetPrivateProfileIntW(L"antialiasing", L"msaa_samples", 0, Ini().c_str());
  return v == 2 || v == 4 || v == 8 ? v : 0;
}
unsigned AaIndex(const LiveSettings &s) {
  auto value = RequestedMsaa();
  for (unsigned i = 2; i < 5; ++i)
    if (samples[i] == value)
      return i;
  return s.dlaa ? 1 : 0;
}
bool IsEditing() {
  return reinterpret_cast<bool(__cdecl *)()>(ttwvr + layout->menuEditing)();
}
void Sync(Option *row) {
  const auto s = ReadLiveSettings();
  if (row == rows[0]) {
    row->value = AaIndex(s);
    static const char *activeNames[] = {"Off", "DLAA", "MSAA 2x", "MSAA 4x",
                                        "MSAA 8x"};
    static const char *restartNames[] = {
        "Off (restart)", "DLAA (restart)", "MSAA 2x (restart)",
        "MSAA 4x (restart)", "MSAA 8x (restart)"};
    for (unsigned i = 0; i < 5; ++i)
      row->names[i] =
          samples[i] == actualSamples ? activeNames[i] : restartNames[i];
  } else if (row == rows[1]) {
    row->value = s.neural;
    row->names[1] = actualSamples ? "On (restart)" : "On";
  } else if (row == rows[2])
    row->value = s.preset - 1;
  else if (row == rows[3])
    row->value = s.resolution / 25 - 1;
  else if (row == rows[4])
    row->value = s.strength / 10;
  else if (row == rows[5])
    row->value = s.tone / 10;
  else if (row == rows[6])
    row->value = s.detail / 10;
  else if (row == rows[7]) {
    row->value = s.sr;
    static const char *restart[] = {"Off", "Quality (restart)", "Balanced (restart)", "Performance (restart)"};
    for (unsigned i = 0; i < 4; ++i)
      row->names[i] = actualSamples ? restart[i] : srNames[i];
  } else if (row == rows[8]) {
    row->value = s.fg;
    row->names[1] = actualSamples ? "2x (restart)" : "2x (experimental)";
  }
}
bool SaveMsaa(unsigned value) {
  wchar_t personal[MAX_PATH]{};
  if (FAILED(SHGetFolderPathW(nullptr, CSIDL_PERSONAL, nullptr,
                              SHGFP_TYPE_CURRENT, personal)))
    return false;
  const auto folder = std::wstring(personal) + L"\\My Games\\FalloutNV\\";
  for (const wchar_t *file : {L"FalloutPrefs.ini", L"FalloutCustom.ini"}) {
    const auto path = folder + file;
    if (GetFileAttributesW(path.c_str()) == INVALID_FILE_ATTRIBUTES ||
        !WritePrivateProfileStringW(L"Display", L"iMultiSample",
                                    std::to_wstring(value).c_str(),
                                    path.c_str()))
      return false;
    WritePrivateProfileStringW(nullptr, nullptr, nullptr, path.c_str());
  }
  wchar_t exe[32768]{};
  GetModuleFileNameW(nullptr, exe, 32768);
  std::wstring bootstrap(exe);
  bootstrap.resize(bootstrap.find_last_of(L"\\/"));
  bootstrap += L"\\Data\\NVSE\\Plugins\\TTWVRRenderingAddon.ini";
  if (!WritePrivateProfileStringW(L"antialiasing", L"msaa_samples",
                                  std::to_wstring(value).c_str(),
                                  bootstrap.c_str()))
    return false;
  return WritePrivateProfileStringW(L"antialiasing", L"msaa_samples",
                                    std::to_wstring(value).c_str(),
                                    Ini().c_str()) != FALSE;
}
void RefreshValues() {
  // Same native option/tile list traversed by TTWVR's label updater. Updating
  // user0 refreshes values of linked rows too (e.g. selecting MSAA turns NR
  // off).
  struct Item {
    void *tile;
    Option *option;
  };
  struct Node {
    Item *item;
    Node *next;
  };
  auto *menu = *reinterpret_cast<unsigned char **>(0x11daac0);
  if (!menu)
    return;
  auto *node = reinterpret_cast<Node *>(menu + 0x118);
  for (unsigned n = 0; node && n < 128; ++n, node = node->next) {
    if (!node->item || !node->item->tile)
      continue;
    auto *row = node->item->option;
    if (std::find(rows.begin(), rows.end(), row) == rows.end() || !row ||
        row->value >= row->count)
      continue;
    reinterpret_cast<void(__thiscall *)(void *, unsigned, const char *, bool)>(
        0xa01350)(node->item->tile, 0x1004, row->names[row->value], true);
  }
}
void __cdecl Selected(Option *row) {
  if (!row || !IsEditing()) {
    if (row)
      Sync(row);
    return;
  }
  if (row->value >= row->count) {
    Sync(row);
    return;
  }
  auto s = ReadLiveSettings();
  bool changedAa = false;
  unsigned msaa = RequestedMsaa();
  if (row == rows[0]) {
    msaa = samples[row->value];
    s.dlaa = row->value == 1;
    s.sr = 0;
    if (msaa)
      s.neural = s.fg = 0;
    changedAa = true;
  } else if (row == rows[1]) {
    s.neural = row->value;
    if (s.neural)
      s.sr = 0;
    if (s.neural && msaa) {
      msaa = 0;
      changedAa = true;
    }
  } else if (row == rows[2])
    s.preset = row->value + 1;
  else if (row == rows[3])
    s.resolution = (row->value + 1) * 25;
  else if (row == rows[4])
    s.strength = row->value * 10;
  else if (row == rows[5])
    s.tone = row->value * 10;
  else if (row == rows[6])
    s.detail = row->value * 10;
  else if (row == rows[7]) {
    s.sr = row->value;
    if (s.sr) {
      s.neural = s.dlaa = 0;
      if (msaa) {
        msaa = 0;
        changedAa = true;
      }
    }
  } else if (row == rows[8]) {
    s.fg = row->value;
    if (s.fg && msaa) {
      msaa = 0;
      changedAa = true;
    }
  } else
    return;
  s.temporal = 1;
  s.identity = 0;
  if ((changedAa && !SaveMsaa(msaa)) || !SaveLiveSettings(s)) {
    Log("native_vr_settings_save_failed=1");
    Sync(row);
    return;
  }
  for (auto *p : rows)
    if (p)
      Sync(p);
  RefreshValues();
  Log("native_vr_settings_saved nr=%u dlaa=%u msaa_requested=%u msaa_actual=%u "
      "restart=%u",
      s.neural, s.dlaa, msaa, actualSamples.load(),
      unsigned(msaa != actualSamples.load()));
}
Option *Add(const char *name, unsigned count, const char **names) {
  auto *row =
      reinterpret_cast<Option *(__cdecl *)(size_t)>(0x401000)(sizeof(Option));
  if (!row)
    return nullptr;
  // The variadic engine constructor copies precisely count names into its own
  // heap.
  auto init = reinterpret_cast<Constructor>(0x7d6350);
  std::array<const char *, 11> values{};
  for (unsigned i = 0; i < count; ++i)
    values[i] = names[i];
  init(row, name, Selected, 0x40000000, 0, count, 170, values[0], values[1],
       values[2], values[3], values[4], values[5], values[6], values[7],
       values[8], values[9], values[10]);
  return row;
}
void Extend() {
  if (installed)
    return;
  const unsigned char ctor[] = {0x55, 0x8b, 0xec, 0x6a, 0xff, 0x68};
  const unsigned char append[] = {0x55, 0x8b, 0xec, 0x83, 0xec, 0x08};
  const unsigned char optimized[] = {
      0x51, 0xe8, 0x5a, 0x62, 0xf9, 0xff, 0x59, 0x8b, 0x49, 0x04, 0x8b,
      0x54, 0x24, 0x04, 0x8b, 0x12, 0x89, 0x14, 0x81, 0xc2, 0x04, 0x00};
  if (std::memcmp(reinterpret_cast<void *>(0x7d6350), ctor, sizeof(ctor)) ||
      (std::memcmp(reinterpret_cast<void *>(0x7cb2e0), append,
                   sizeof(append)) &&
       std::memcmp(reinterpret_cast<void *>(0x7cb2e0), optimized,
                   sizeof(optimized)))) {
    Log("native_vr_menu_refused engine_signature=1");
    return;
  }

  // Hide the unavailable built-in upscaler controls, retaining objects and
  // callback pointers owned by TTWVR so its cleanup remains valid.
  for (unsigned i = 0; i < layout->upscalerRows.size(); ++i) {
    auto *p = *reinterpret_cast<Option **>(ttwvr + layout->upscalerRows[i]);
    if (!p || !p->name || p->flags != 0x40000000) {
      Log("native_vr_menu_refused invalid_original_row=1");
      return;
    }
    // Resolution/Sharpness have dynamic percentage labels. Verify identity as
    // well as flags before hiding rows; adjacent stock options share flags.
    const bool expected = i == 0   ? !std::strcmp(p->name, "Upscaler")
                          : i == 1 ? !std::strcmp(p->name, "Upscaler Level")
                                   : !std::strncmp(p->name, "Sharpness", 9) &&
                                         (p->name[9] == 0 || p->name[9] == ' ');
    if (!expected) {
      Log("native_vr_menu_refused original_row_name=%s index=%u", p->name, i);
      return;
    }
  }
  rows = {Add("Anti-aliasing", 5, aaNames),
          Add("Neural Rendering", 2, toggleNames),
          Add("NR model", 2, modelNames),
          Add("NR processing scale", 4, scaleNames),
          Add("NR strength", 11, percentNames),
          Add("NR tone", 11, percentNames),
          Add("NR detail", 11, percentNames),
          Add("DLSS SR", 4, srNames),
          Add("Frame gen.", 2, toggleNames)};
  for (auto *p : rows)
    if (!p) {
      for (auto *allocated : rows)
        if (allocated) {
          // Constructor-owned value pointer arrays use the engine heap.
          reinterpret_cast<void(__cdecl *)(void *)>(0x401030)(
              const_cast<char **>(allocated->names));
          reinterpret_cast<void(__cdecl *)(void *)>(0x401030)(allocated);
        }
      rows.fill(nullptr);
      Log("native_vr_menu_failed allocation=1");
      return;
    }
  for (auto *p : rows)
    reinterpret_cast<Append>(0x7cb2e0)(reinterpret_cast<void *>(0x11dab50), &p);
  for (auto rva : layout->upscalerRows)
    (*reinterpret_cast<Option **>(ttwvr + rva))->flags = 0;
  auto **data = *reinterpret_cast<Option ***>(0x11dab54);
  auto count = *reinterpret_cast<unsigned *>(0x11dab58);
  auto *firstOriginal =
      *reinterpret_cast<Option **>(ttwvr + layout->upscalerRows[0]);
  for (unsigned i = 0; i + rows.size() < count; ++i)
    if (data[i] == firstOriginal) {
      std::rotate(data + i, data + count - rows.size(), data + count);
      break;
    }
  for (auto *p : rows)
    Sync(p);
  installed = true;
  Log("native_vr_menu_registered rows=9 controller_navigation=engine "
      "msaa_actual=%u",
      actualSamples.load());
}
void __cdecl HookBuild() {
  build();
  Extend();
}
} // namespace
bool NativeVRSettingsInstalled() { return installed; }
unsigned CurrentMsaaSamples() { return actualSamples; }
void ObserveMSAATarget(IDirect3DSurface9 *surface) {
  if (!surface)
    return;
  D3DSURFACE_DESC desc{};
  if (SUCCEEDED(surface->GetDesc(&desc)) && desc.Width == 2944 &&
      desc.Height == 2208 && desc.Format == D3DFMT_A16B16G16R16F &&
      unsigned(desc.MultiSampleType) > actualSamples) {
    actualSamples = unsigned(desc.MultiSampleType);
    Log("native_vr_msaa_scene samples=%u size=%ux%u", actualSamples.load(),
        desc.Width, desc.Height);
  }
}
bool InstallVRSettings(HMODULE module, IDirect3DDevice9 *device,
                       const TTWVRBuild &buildLayout) {
  layout = &buildLayout;
  if (!GetModuleHandleW(L"TTWVRRenderingAddon.dll"))
    return false;
  ttwvr = reinterpret_cast<unsigned char *>(module);
  (void)device; // Presentation buffers are single-sampled even with scene MSAA.
  auto *address = ttwvr + layout->menuBuild;
  if (MH_CreateHook(address, (void *)HookBuild, (void **)&build) != MH_OK ||
      MH_EnableHook(address) != MH_OK)
    return false;
  // OpenXR can initialize before or after the first menu construction.
  if (*reinterpret_cast<Option **>(ttwvr + layout->upscalerRows[0]))
    Extend();
  // This is the same verified settings object used by NativeGuides. Disable
  // the nonfunctional built-in upscaler for this session.
  *(ttwvr + layout->settings + 0x8c) = 0;
  Log("native_vr_settings_hook_ready=1 builtin_upscaler=off msaa_actual=%u",
      actualSamples.load());
  return true;
}
} // namespace ttwvrnr
