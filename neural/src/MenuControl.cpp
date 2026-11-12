// SPDX-License-Identifier: GPL-3.0-only
#include "MenuControl.h"
#include "Support.h"
#include "VRSettings.h"
#include <algorithm>
#include <cstdio>
namespace ttwvrnr {
namespace {
const uint32_t session = (GetTickCount() & 0x7fffff) + 1;
uint32_t consumed = 0;
std::string notice;
std::wstring Ini() { return Directory() + L"\\TTWVRNeuralRendering.ini"; }
std::wstring MenuPath(const wchar_t *file) {
  wchar_t exe[32768]{};
  GetModuleFileNameW(nullptr, exe, 32768);
  std::wstring dir(exe);
  dir.resize(dir.find_last_of(L"\\/"));
  dir += L"\\Data\\config";
  CreateDirectoryW(dir.c_str(), nullptr);
  return dir + L"\\" + file;
}
uint32_t Read(const std::wstring &p, const wchar_t *sec, const wchar_t *key,
              uint32_t fallback) {
  return GetPrivateProfileIntW(sec, key, fallback, p.c_str());
}
void Flush(const std::wstring &path) {
  WritePrivateProfileStringW(nullptr, nullptr, nullptr, path.c_str());
}
bool Save(const LiveSettings &s) {
  auto ini = Ini(), tmp = ini + L".menu.tmp";
  if (!CopyFileW(ini.c_str(), tmp.c_str(), FALSE))
    return false;
  bool ok = true;
  auto put = [&](const wchar_t *section, const wchar_t *key, uint32_t value) {
    ok = WritePrivateProfileStringW(
             section, key, std::to_wstring(value).c_str(), tmp.c_str()) &&
         ok;
  };
  put(L"neural_rendering", L"enabled", s.neural);
  put(L"neural_rendering", L"experimental_temporal", 1);
  put(L"neural_rendering", L"experimental_spatial", 0);
  put(L"neural_rendering", L"transport_identity", 0);
  put(L"neural_rendering", L"resolution_percent", s.resolution);
  put(L"neural_rendering", L"intensity_percent", s.strength);
  put(L"neural_rendering", L"tone_percent", s.tone);
  put(L"neural_rendering", L"detail_percent", s.detail);
  ok = WritePrivateProfileStringW(L"neural_rendering", L"model",
                                  s.preset == 1 ? L"A" : L"B", tmp.c_str()) &&
       ok;
  put(L"dlaa", L"enabled", s.dlaa);
  put(L"super_resolution", L"enabled", s.sr != 0);
  if (s.sr)
    ok = WritePrivateProfileStringW(L"super_resolution", L"mode",
        s.sr == 3 ? L"Performance" : s.sr == 2 ? L"Balanced" : L"Quality",
        tmp.c_str()) && ok;
  put(L"frame_generation", L"enabled", s.fg);
  Flush(tmp);
  if (ok)
    ok = MoveFileExW(tmp.c_str(), ini.c_str(),
                     MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH);
  if (!ok)
    DeleteFileW(tmp.c_str());
  Flush(ini);
  return ok;
}
} // namespace
bool SaveLiveSettings(const LiveSettings &s) { return s.Valid() && Save(s); }
LiveSettings ReadLiveSettings() {
  auto p = Ini();
  Flush(p);
  LiveSettings s;
  auto get = [&](const wchar_t *sec, const wchar_t *key, uint32_t fallback) {
    return Read(p, sec, key, fallback);
  };
  s.dlaa = get(L"dlaa", L"enabled", 0) != 0;
  s.temporal = s.dlaa || get(L"neural_rendering", L"experimental_temporal", 0);
  s.neural =
      get(L"neural_rendering", L"enabled", 0) &&
      (s.temporal || get(L"neural_rendering", L"experimental_spatial", 0));
  s.identity = get(L"neural_rendering", L"transport_identity", 1) != 0;
  s.profile = get(L"diagnostics", L"gpu_timing", 0) != 0;
  auto percent = [&](const wchar_t *key, uint32_t fallback, int minimum) {
    return uint32_t(
        std::clamp(int(get(L"neural_rendering", key, fallback)), minimum, 100));
  };
  s.resolution = percent(L"resolution_percent", 100, 25);
  s.strength = percent(L"intensity_percent", 50, 0);
  s.tone = percent(L"tone_percent", 100, 0);
  s.detail = percent(L"detail_percent", 100, 0);
  wchar_t model[16]{};
  GetPrivateProfileStringW(L"neural_rendering", L"model", L"B", model, 16,
                           p.c_str());
  s.preset = !_wcsicmp(model, L"A") ? 1 : 2;
  wchar_t srMode[32]{};
  GetPrivateProfileStringW(L"super_resolution", L"mode", L"Quality", srMode, 32, p.c_str());
  if (get(L"super_resolution", L"enabled", 0))
    s.sr = !_wcsicmp(srMode, L"Performance") ? 3
         : !_wcsicmp(srMode, L"Balanced") ? 2 : 1;
  s.fg = get(L"frame_generation", L"enabled", 0) != 0;
  if (s.fg) { s.temporal = 1; s.identity = 0; }
  if (s.sr) {
    s.neural = s.dlaa = s.identity = 0;
    s.temporal = 1;
  }
  return s;
}
bool ReadMenuRequest(uint32_t revision, LiveSettings &s) {
  auto p = MenuPath(L"TTWVRNR_request.ini");
  Flush(p);
  auto get = [&](const wchar_t *key) { return Read(p, L"request", key, 0); };
  uint32_t seq = get(L"sequence");
  if (!seq || seq == consumed)
    return false;
  LiveSettings next = s;
  next.sr = get(L"sr");
  next.fg = Read(p, L"request", L"fg", s.fg);
  next.neural = get(L"neural");
  next.dlaa = get(L"dlaa");
  next.temporal = 1;
  next.identity = 0;
  next.preset = get(L"preset");
  next.resolution = get(L"resolution");
  next.strength = get(L"strength");
  next.tone = get(L"tone");
  next.detail = get(L"detail");
  const auto requestSession = get(L"session"), base = get(L"revision");
  Flush(p);
  if (get(L"sequence") != seq)
    return false;
  consumed = seq;
  if (requestSession != session)
    return false; // previous launch, never replay
  if (base != revision || !next.Valid()) {
    notice = "Settings changed. Close and reopen this menu.";
    Log("menu_request_rejected sequence=%u stale_or_invalid=1", seq);
    return false;
  }
  if (!Save(next)) {
    notice = "Could not save settings. Previous settings retained.";
    Log("menu_request_rejected sequence=%u save_failed=1", seq);
    return false;
  }
  notice.clear();
  s = next;
  Log("menu_apply sequence=%u base_revision=%u nr=%u dlaa=%u sr=%u model=%u "
      "scale=%u",
      seq, revision, s.neural, s.dlaa, s.sr, s.preset, s.resolution);
  return true;
}
void PublishMenuState(const ReloadState &r, bool applied) {
  const char *status = "Off - original image";
  if (r.fault)
    status = "Error - original image. Apply to retry.";
  else if (r.phase == ReloadState::Stopping)
    status = "Rebuilding - original image";
  else if (r.phase == ReloadState::Starting)
    status = "Initializing - original image";
  else if (r.requested.Active()) {
    if (r.requested.identity)
      status = "Diagnostic bypass - no effects applied";
    else if (!applied)
      status = "Ready - waiting for a scene";
    else if (r.requested.sr)
      status = r.requested.sr == 3 ? "Active: DLSS Performance"
             : r.requested.sr == 2 ? "Active: DLSS Balanced" : "Active: DLSS Quality";
    else if (r.requested.neural && r.requested.dlaa)
      status = "Active: NR + DLAA";
    else if(r.requested.fg && !r.requested.neural && !r.requested.dlaa)
      status="Active: frame generation (experimental)";
    else
      status = r.requested.neural ? "Active: NR" : "Active: DLAA";
  }
  if (CurrentMsaaSamples()) status = "Active: MSAA - AA changes require restart";
  const auto &s = r.requested;
  char data[1024]{};
  std::snprintf(
      data, sizeof(data),
      "[state]\nnative_menu=%u\nmsaa_samples=%u\nsession=%u\nrevision=%u\nsequence=%u\nneural=%u\ndlaa=%"
      "u\nsr=%u\nfg=%u\npreset=%u\n"
      "resolution=%u\nstrength=%u\ntone=%u\ndetail=%u\nstatus=%s\nnotice=%s\n",
      unsigned(NativeVRSettingsInstalled()), CurrentMsaaSamples(), session, r.revision, consumed, s.neural, s.dlaa, s.sr, s.fg, s.preset, s.resolution,
      s.strength, s.tone, s.detail, status, notice.c_str());
  static std::string previous;
  if (data == previous)
    return;
  auto path = MenuPath(L"TTWVRNR_state.ini"), tmp = path + L".tmp";
  auto *f = _wfopen(tmp.c_str(), L"wb");
  if (!f)
    return;
  bool ok = std::fputs(data, f) >= 0;
  ok = std::fclose(f) == 0 && ok;
  if (ok && MoveFileExW(tmp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING)) {
    Flush(path);
    previous = data;
  } else
    DeleteFileW(tmp.c_str());
}
} // namespace ttwvrnr
