// SPDX-License-Identifier: GPL-3.0-only
// Derived from Ethanfel TF2VRNeuralRendering; see THIRD-PARTY-NOTICES.md.
#include "Support.h"
#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <mutex>
namespace ttwvrnr {
std::wstring Directory() {
    HMODULE module = nullptr;
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
        reinterpret_cast<LPCWSTR>(&Directory), &module);
    wchar_t path[32768]{};
    const auto n = GetModuleFileNameW(module, path, 32768);
    if (!n || n >= 32768) return {};
    std::wstring result(path, n);
    return result.substr(0, result.find_last_of(L"\\/"));
}
Config ReadConfig() {
    Config c;
    const auto path = Directory() + L"\\TTWVRNeuralRendering.ini";
    c.enabled = GetPrivateProfileIntW(L"layer", L"enabled", 1, path.c_str()) != 0;
    c.capture = GetPrivateProfileIntW(L"capture", L"enabled", 1, path.c_str()) != 0;
    c.captureFirst = GetPrivateProfileIntW(L"capture", L"first_stereo_frame", 0, path.c_str()) != 0;
    c.inspectBindings = GetPrivateProfileIntW(L"capture", L"inspect_bindings", 1, path.c_str()) != 0;
    c.captureCandidates = GetPrivateProfileIntW(L"capture", L"capture_candidates", 1, path.c_str()) != 0;
    c.burstFrames = std::clamp(GetPrivateProfileIntW(L"capture", L"burst_frames", 1, path.c_str()), 1u, 2u);
    c.logEvery = std::clamp(GetPrivateProfileIntW(L"diagnostics", L"log_every_frames", 600, path.c_str()), 60u, 36000u);
    return c;
}
bool IsTarget() {
    wchar_t path[32768]{};
    const auto n = GetModuleFileNameW(nullptr, path, 32768);
    if (!n || n >= 32768) return false;
    std::wstring name(path, n);
    name = name.substr(name.find_last_of(L"\\/") + 1);
    return !_wcsicmp(name.c_str(), L"FalloutNV.exe") || !_wcsicmp(name.c_str(), L"TTWVRProbe.exe");
}
void Log(const char* fmt, ...) {
    static std::mutex mutex;
    std::lock_guard lock(mutex);
    // Open per entry, so neither process termination nor DLL unload loses logs.
    FILE* file = _wfopen((Directory() + L"\\TTWVRNeuralRendering.log").c_str(), L"ab");
    if (!file) return;
    SYSTEMTIME t{}; GetLocalTime(&t);
    std::fprintf(file, "%04u-%02u-%02u %02u:%02u:%02u.%03u pid=%lu ", t.wYear, t.wMonth, t.wDay,
        t.wHour, t.wMinute, t.wSecond, t.wMilliseconds, GetCurrentProcessId());
    va_list args; va_start(args, fmt); std::vfprintf(file, fmt, args); va_end(args);
    std::fputc('\n', file); std::fclose(file);
}
bool ConsumeCaptureRequest() {
    return DeleteFileW((Directory() + L"\\capture.request").c_str()) != FALSE;
}
std::wstring CaptureDirectory() {
    const auto path = Directory() + L"\\captures";
    if (!CreateDirectoryW(path.c_str(), nullptr) && GetLastError() != ERROR_ALREADY_EXISTS) {
        Log("capture_directory_failed error=%lu", GetLastError()); return {};
    }
    return path;
}
}
