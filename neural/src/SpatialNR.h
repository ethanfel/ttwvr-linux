// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <cstdint>
struct IDirect3DDevice9;
namespace ttwvrnr {
bool SpatialApplied(uint64_t frame);
bool SpatialEnabled(uint64_t frame);
bool ApplySpatialNR(IDirect3DDevice9 *, uint64_t frame, unsigned eye);
} // namespace ttwvrnr
