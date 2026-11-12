// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "Graphics.h"
#define XR_USE_GRAPHICS_API_D3D11
#define XR_USE_PLATFORM_WIN32
#include <array>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <openxr.h>
#include <openxr_platform.h>
#include <thread>
#include <vector>
namespace ttwvrnr {
struct StereoFrameTextures {
  std::shared_ptr<void> owner;
  ID3D11Texture2D *real[2]{};
  ID3D11Texture2D *generated[2]{};
  int64_t format[2]{};
  uint64_t frame = 0;
  bool interpolated = false, capture = false;
  explicit operator bool() const {
    return owner && real[0] && real[1] && generated[0] && generated[1];
  }
};
bool CopyPresentationImage(ID3D11Device *, ID3D11Texture2D *,
                           ID3D11Texture2D *);
// Copies all four images into private textures before the producer can reuse
// them. No runtime-owned texture or caller-owned OpenXR chain is retained.
bool DrainPresentation(ID3D11Device *);
bool SetPresentationThreading(ID3D11Device *, bool enabled, bool &previous);
StereoFrameTextures CopyFrameTextures(ID3D11Device *,
                                      const StereoFrameTextures &);
struct FrameDeliveryDispatch {
  PFN_xrWaitFrame wait = nullptr;
  PFN_xrBeginFrame begin = nullptr;
  PFN_xrEndFrame end = nullptr;
  PFN_xrCreateSwapchain create = nullptr;
  PFN_xrDestroySwapchain destroy = nullptr;
  PFN_xrEnumerateSwapchainImages enumerate = nullptr;
  PFN_xrAcquireSwapchainImage acquire = nullptr;
  PFN_xrWaitSwapchainImage waitImage = nullptr;
  PFN_xrReleaseSwapchainImage release = nullptr;
};
class FrameDelivery {
  struct Packet {
    StereoFrameTextures textures;
    XrCompositionLayerProjection projection{
        XR_TYPE_COMPOSITION_LAYER_PROJECTION};
    std::array<XrCompositionLayerProjectionView, 2> views;
  };
  struct Chain {
    XrSwapchain handle = XR_NULL_HANDLE;
    unsigned width = 0, height = 0;
    int64_t format = 0;
    std::vector<ID3D11Texture2D *> images;
  };
  XrSession session_;
  ID3D11Device *device_;
  FrameDeliveryDispatch api_;
  std::array<Chain, 2> chains_;
  mutable std::mutex mutex_;
  std::condition_variable changed_;
  std::thread worker_;
  bool stop_ = false, running_ = false, fault_ = false, eligible_ = false;
  bool threading_ = false, previousProtection_ = false;
  bool proxy_ = false, appBusy_ = false, begun_ = false;
  uint64_t tick_ = 0, issuedTick_ = 0;
  XrTime issuedTime_ = 0;
  unsigned generated_ = 0, real_ = 0, repeated_ = 0;
  std::deque<uint64_t> presented_;
  XrFrameState state_{XR_TYPE_FRAME_STATE};
  XrResult workerResult_ = XR_SUCCESS;
  std::shared_ptr<Packet> queued_, prime_;
  std::shared_ptr<Packet> CopyPacket(const XrFrameEndInfo *,
                                     const StereoFrameTextures &);
  bool Ensure(const Packet &);
  bool Fill(const Packet &, bool);
  XrResult Emit(const Packet &, const Packet *previous, bool, XrTime);
  void Run(std::shared_ptr<Packet>);
  XrResult OriginalEnd(const XrFrameEndInfo *);

public:
  FrameDelivery(XrSession, ID3D11Device *, const FrameDeliveryDispatch &);
  ~FrameDelivery();
  XrResult Wait(const XrFrameWaitInfo *, XrFrameState *, bool enabled);
  XrResult Begin(const XrFrameBeginInfo *);
  XrResult End(const XrFrameEndInfo *, const StereoFrameTextures &);
  void Complete();
  // Drain before destroying an application space or ending its session.
  void Stop();
  unsigned GeneratedPairs() const;
  bool WasGenerated(uint64_t frame) const;
};
} // namespace ttwvrnr
