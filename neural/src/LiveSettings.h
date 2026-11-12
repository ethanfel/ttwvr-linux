// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <cstdint>
#include "SRMode.h"
#include <tuple>
namespace ttwvrnr {
struct LiveSettings {
  uint32_t sr = 0, fg = 0; // SR: 0 off, 1 Quality, 2 Balanced, 3 Performance.
  uint32_t neural = 0, dlaa = 0, temporal = 0, identity = 1, profile = 0;
  uint32_t resolution = 100, preset = 2, strength = 50, tone = 100,
           detail = 100;
  bool Active() const { return neural || dlaa || sr || fg; }
  bool Valid() const {
    return fg <= 1 && (!fg || (temporal && !identity)) && ValidSRMode(sr) && (!sr || (temporal && !neural && !dlaa && !identity)) &&
           neural <= 1 && dlaa <= 1 && temporal <= 1 && identity <= 1 &&
           profile <= 1 && resolution >= 25 && resolution <= 100 &&
           preset >= 1 && preset <= 2 && strength <= 100 && tone <= 100 &&
           detail <= 100 && (!dlaa || temporal);
  }
  auto Values() const {
    return std::tie(sr, fg, neural, dlaa, temporal, identity, profile, resolution,
                    preset, strength, tone, detail);
  }
  bool operator==(const LiveSettings &s) const {
    return Values() == s.Values();
  }
  bool operator!=(const LiveSettings &s) const { return !(*this == s); }
};
// Driven exclusively at the stereo frame boundary, before the next camera
// jitter or either eye.
// An old helper retains its immutable settings until it has exited. Requests
// arriving during shutdown coalesce; there can never be two live helpers.
struct ReloadState {
  enum Phase { Off, Starting, Ready, Stopping, Failed } phase = Off;
  LiveSettings requested;
  uint32_t revision = 0, generation = 0;
  bool process = false, fault = false;
  bool Request(const LiveSettings &s, bool retry = false) {
    if (!s.Valid() || (!retry && s == requested))
      return false;
    requested = s;
    ++revision;
    fault = false;
    phase = process ? Stopping : Off;
    return true;
  }
  bool NeedStart() const { return !process && !fault && requested.Active(); }
  bool NeedStop() const { return process && (phase == Stopping || fault); }
  bool CanApply() const {
    return phase == Ready && !fault && generation == revision;
  }
  void Started() {
    process = true;
    generation = revision;
    phase = Starting;
  }
  void Initialized() {
    if (phase == Starting && generation == revision)
      phase = Ready;
  }
  void Fail() {
    fault = true;
    phase = Failed;
  }
  void Exited() {
    if (phase != Stopping)
      fault = true;
    process = false;
    phase = fault ? Failed : Off;
  }
};
} // namespace ttwvrnr
