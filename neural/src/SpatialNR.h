// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <cstdint>
struct IDirect3DDevice9;
namespace ttwvrnr {
bool SpatialApplied(uint64_t frame);
bool DlaaApplied(uint64_t frame);
bool TemporalNRApplied(uint64_t frame);
bool DlaaJitterReady();
bool SceneProcessingEnabled();
void ProcessingJitter(uint64_t frame, float &x, float &y);
bool SpatialEnabled(uint64_t frame);
bool ApplySpatialNR(IDirect3DDevice9 *, uint64_t frame, unsigned eye,
                    bool capture = false);
} // namespace ttwvrnr
