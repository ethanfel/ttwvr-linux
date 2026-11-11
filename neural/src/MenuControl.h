// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "LiveSettings.h"
#include <string>
namespace ttwvrnr {
LiveSettings ReadLiveSettings();
// Called only at a stereo frame boundary. Menu writes a draft followed by its
// sequence number. The renderer acknowledges and persists a complete snapshot.
bool ReadMenuRequest(uint32_t revision, LiveSettings &s);
void PublishMenuState(const ReloadState &state, bool applied);
} // namespace ttwvrnr
