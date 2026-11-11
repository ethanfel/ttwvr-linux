// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <cstdint>
namespace ttwvrnr {
bool InstallD3D9Trace();
void ArmD3D9Trace(bool enabled, uint64_t frame);
void FinishD3D9Trace(uint64_t frame);
}
