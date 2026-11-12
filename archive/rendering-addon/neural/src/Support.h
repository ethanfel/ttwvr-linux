// SPDX-License-Identifier: GPL-3.0-only
// Derived from Ethanfel TF2VRNeuralRendering; see THIRD-PARTY-NOTICES.md.
#pragma once
#include <windows.h>
#include <string>
#include <functional>
namespace ttwvrnr {
struct Config {
    bool enabled = true;
    bool capture = true;
    bool captureFirst = false;
    bool inspectBindings = true;
    bool captureCandidates = true;
    bool frameTiming = false;
    unsigned burstFrames = 1;
    unsigned logEvery = 600;
};
std::wstring Directory();
Config ReadConfig();
bool IsTarget();
void Log(const char* fmt, ...);
bool ConsumeCaptureRequest();
std::wstring CaptureDirectory();
bool CaptureWriteBusy();
bool QueueCaptureWrite(std::function<void()> work);
}
