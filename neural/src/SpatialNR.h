// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <cstdint>
struct IDirect3DDevice9;
struct ID3D11Device;
struct ID3D11Texture2D;
namespace ttwvrnr {
struct StereoFrameTextures;
bool FrameGenerationRequested();
void StopSceneProcessing();
bool GenerateFrameEye(ID3D11Device*, ID3D11Texture2D*, uint64_t frame, unsigned eye);
StereoFrameTextures GeneratedFrame(uint64_t frame);

bool SpatialApplied(uint64_t frame);
bool SrApplied(uint64_t frame);
bool SuperResolutionReady();
bool DlaaApplied(uint64_t frame);
bool TemporalNRApplied(uint64_t frame);
bool DlaaJitterReady();
bool SceneProcessingEnabled();
void ProcessingJitter(uint64_t frame, float &x, float &y);
bool SpatialEnabled(uint64_t frame);
bool ApplySpatialNR(IDirect3DDevice9 *, uint64_t frame, unsigned eye,
                    bool capture = false);
} // namespace ttwvrnr
