// SPDX-License-Identifier: GPL-3.0-only
// Stereo validation and pose interpolation adapted from TF2VRNeuralRendering.
#include "FrameDelivery.h"
#include "Support.h"
#include <algorithm>
#include <cmath>
#include <cstring>
namespace ttwvrnr {
namespace {
const XrCompositionLayerProjection *Stereo(const XrFrameEndInfo *info) {
  if (!info || info->next || info->layerCount != 1 || !info->layers ||
      !info->layers[0] ||
      info->environmentBlendMode != XR_ENVIRONMENT_BLEND_MODE_OPAQUE)
    return nullptr;
  auto *h = info->layers[0];
  if (h->type != XR_TYPE_COMPOSITION_LAYER_PROJECTION || h->next)
    return nullptr;
  auto *p = reinterpret_cast<const XrCompositionLayerProjection *>(h);
  if (p->viewCount != 2 || !p->views)
    return nullptr;
  for (unsigned e = 0; e < 2; ++e) {
    const auto &v = p->views[e];
    if (v.next || v.subImage.imageArrayIndex || v.subImage.imageRect.offset.x ||
        v.subImage.imageRect.offset.y ||
        v.subImage.imageRect.extent.width <= 0 ||
        v.subImage.imageRect.extent.height <= 0)
      return nullptr;
  }
  return p;
}
XrPosef Midpoint(const XrPosef &a, const XrPosef &b) {
  XrPosef r{};
  r.position = {(a.position.x + b.position.x) * .5f,
                (a.position.y + b.position.y) * .5f,
                (a.position.z + b.position.z) * .5f};
  const float dot =
      a.orientation.x * b.orientation.x + a.orientation.y * b.orientation.y +
      a.orientation.z * b.orientation.z + a.orientation.w * b.orientation.w;
  const float sign = dot < 0 ? -1.f : 1.f;
  auto &q = r.orientation;
  q = {a.orientation.x + sign * b.orientation.x,
       a.orientation.y + sign * b.orientation.y,
       a.orientation.z + sign * b.orientation.z,
       a.orientation.w + sign * b.orientation.w};
  const float n = std::sqrt(q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w);
  if (n > 1e-6f) {
    q.x /= n;
    q.y /= n;
    q.z /= n;
    q.w /= n;
  } else
    q = b.orientation;
  return r;
}
} // namespace
FrameDelivery::FrameDelivery(XrSession s, ID3D11Device *d,
                             const FrameDeliveryDispatch &a)
    : session_(s), device_(d), api_(a) {}
FrameDelivery::~FrameDelivery() {
  Stop();
  for (auto &c : chains_)
    if (c.handle)
      api_.destroy(c.handle);
}
void FrameDelivery::Stop() {
  {
    std::lock_guard lock(mutex_);
    stop_ = true;
    changed_.notify_all();
  }
  if (worker_.joinable())
    worker_.join();
  if (threading_) {
    // The CPU worker has exited; finish its queued copies before the runtime
    // can destroy imported swapchain images or its Vulkan device.
    Log("fg_presentation_drained success=%d", DrainPresentation(device_));
    bool ignored = false;
    SetPresentationThreading(device_, previousProtection_, ignored);
    threading_ = false;
    Log("fg_context_threading restored=%d", previousProtection_);
  }
  // End a presentation generation completely. Keeping imported swapchain
  // images across a menu/worker restart left the Proton/runtime path stalled
  // on reuse; a new worker gets fresh acquire/release state and images.
  for (auto &c : chains_) {
    if (c.handle)
      api_.destroy(c.handle);
    c = {};
  }
  std::lock_guard lock(mutex_);
  running_ = false;
  eligible_ = false;
  queued_.reset();
  prime_.reset();
  proxy_ = false;
  appBusy_ = false;
  changed_.notify_all();
}
unsigned FrameDelivery::GeneratedPairs() const {
  std::lock_guard lock(mutex_);
  return generated_;
}
bool FrameDelivery::WasGenerated(uint64_t frame) const {
  std::lock_guard lock(mutex_);
  return std::find(presented_.begin(), presented_.end(), frame) !=
         presented_.end();
}
void FrameDelivery::Complete() {
  std::lock_guard lock(mutex_);
  appBusy_ = false;
  changed_.notify_all();
}
std::shared_ptr<FrameDelivery::Packet>
FrameDelivery::CopyPacket(const XrFrameEndInfo *info,
                          const StereoFrameTextures &textures) {
  const auto *p = Stereo(info);
  if (!p || !textures)
    return {};
  auto copy = std::make_shared<Packet>();
  copy->textures = CopyFrameTextures(device_, textures);
  if (!copy->textures)
    return {};
  copy->projection = *p;
  copy->projection.views = nullptr;
  for (unsigned e = 0; e < 2; ++e)
    copy->views[e] = p->views[e];
  return copy;
}
XrResult FrameDelivery::Wait(const XrFrameWaitInfo *info, XrFrameState *state,
                             bool enabled) {
  if (!enabled)
    Stop();
  std::unique_lock lock(mutex_);
  changed_.wait(lock, [&] { return !appBusy_; });
  if (enabled && eligible_ && !fault_ && !running_) {
    if (!SetPresentationThreading(device_, true, previousProtection_)) {
      fault_ = true;
      lock.unlock();
      return api_.wait(session_, info, state);
    }
    threading_ = true;
    Log("fg_context_threading enabled=1 previous=%d", previousProtection_);
    stop_ = false;
    running_ = true;
    tick_ = issuedTick_ = 0;
    issuedTime_ = 0;
    state_ = {XR_TYPE_FRAME_STATE};
    workerResult_ = XR_SUCCESS;
    auto first = std::move(prime_);
    worker_ = std::thread([this, first] { Run(first); });
    Log("fg_delivery_started mode=queued owned_packets=bounded");
  }
  if (running_ && !fault_) {
    changed_.wait(lock, [&] {
      return fault_ || !running_ || (!queued_ && tick_ >= issuedTick_ + 2);
    });
    if (running_ && !fault_ && state) {
      void *callerNext = state->next;
      *state = state_;
      state->next = callerNext;
      state->predictedDisplayTime = std::max(
          state_.predictedDisplayTime + 2 * state_.predictedDisplayPeriod,
          issuedTime_ + state_.predictedDisplayPeriod);
      issuedTime_ = state->predictedDisplayTime;
      state->predictedDisplayPeriod *= 2;
      issuedTick_ = tick_;
      proxy_ = appBusy_ = true;
      begun_ = false;
      return workerResult_;
    }
  }
  lock.unlock();
  if (worker_.joinable())
    Stop();
  return api_.wait(session_, info, state);
}
XrResult FrameDelivery::Begin(const XrFrameBeginInfo *info) {
  {
    std::lock_guard lock(mutex_);
    if (proxy_) {
      if (begun_)
        return XR_ERROR_CALL_ORDER_INVALID;
      begun_ = true;
      return XR_SUCCESS;
    }
  }
  return api_.begin(session_, info);
}
XrResult FrameDelivery::OriginalEnd(const XrFrameEndInfo *info) {
  // Proxy frames have no application-owned runtime frame. Finish the worker's
  // frame, then establish an ordinary frame before forwarding arbitrary layers.
  Stop();
  XrFrameWaitInfo wait{XR_TYPE_FRAME_WAIT_INFO};
  XrFrameState state{XR_TYPE_FRAME_STATE};
  auto result = api_.wait(session_, &wait, &state);
  if (XR_FAILED(result))
    return result;
  XrFrameBeginInfo begin{XR_TYPE_FRAME_BEGIN_INFO};
  result = api_.begin(session_, &begin);
  if (XR_FAILED(result))
    return result;
  if (!info)
    return api_.end(session_, info);
  auto copy = *info;
  copy.displayTime = state.predictedDisplayTime;
  return api_.end(session_, &copy);
}
XrResult FrameDelivery::End(const XrFrameEndInfo *info,
                            const StereoFrameTextures &textures) {
  bool proxy;
  {
    std::lock_guard lock(mutex_);
    proxy = proxy_;
    if (proxy && !begun_)
      return XR_ERROR_CALL_ORDER_INVALID;
  }
  std::shared_ptr<Packet> packet;
  try {
    packet = CopyPacket(info, textures);
  } catch (...) {
    Log("fg_packet_copy_failed original_frame_retained=1");
  }
  if (proxy) {
    if (!packet)
      return OriginalEnd(info);
    std::unique_lock lock(mutex_);
    changed_.wait(lock, [&] { return !queued_ || fault_ || !running_; });
    if (fault_ || !running_) {
      lock.unlock();
      return OriginalEnd(info);
    }
    queued_ = std::move(packet);
    proxy_ = false;
    changed_.notify_all();
    return XR_SUCCESS;
  }
  const auto result = api_.end(session_, info);
  {
    std::lock_guard lock(mutex_);
    prime_ = std::move(packet);
    eligible_ = !!prime_ && XR_SUCCEEDED(result) && !fault_;
  }
  return result;
}
bool FrameDelivery::Ensure(const Packet &p) {
  for (unsigned e = 0; e < 2; ++e) {
    auto &c = chains_[e];
    const auto size = p.views[e].subImage.imageRect.extent;
    const auto format = p.textures.format[e];
    if (format != 28 && format != 29)
      return false;
    if (c.handle && (c.width != unsigned(size.width) ||
                     c.height != unsigned(size.height) || c.format != format)) {
      api_.destroy(c.handle);
      c = {};
    }
    if (c.handle)
      continue;
    XrSwapchainCreateInfo info{XR_TYPE_SWAPCHAIN_CREATE_INFO};
    info.usageFlags = XR_SWAPCHAIN_USAGE_TRANSFER_DST_BIT |
                      XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT |
                      XR_SWAPCHAIN_USAGE_SAMPLED_BIT;
    info.format = format;
    info.sampleCount = info.faceCount = info.arraySize = info.mipCount = 1;
    info.width = size.width;
    info.height = size.height;
    if (XR_FAILED(api_.create(session_, &info, &c.handle)))
      return false;
    uint32_t count = 0;
    if (XR_FAILED(api_.enumerate(c.handle, 0, &count, nullptr)) || !count ||
        count > 32)
      return false;
    std::vector<XrSwapchainImageD3D11KHR> images(
        count, {XR_TYPE_SWAPCHAIN_IMAGE_D3D11_KHR});
    if (XR_FAILED(api_.enumerate(
            c.handle, count, &count,
            reinterpret_cast<XrSwapchainImageBaseHeader *>(images.data()))))
      return false;
    for (auto &image : images) {
      if (!image.texture)
        return false;
      c.images.push_back(image.texture);
    }
    c.width = info.width;
    c.height = info.height;
    c.format = format;
    Log("fg_presentation_swapchain eye=%u size=%ux%u images=%u", e, c.width,
        c.height, count);
  }
  return true;
}
bool FrameDelivery::Fill(const Packet &p, bool generated) {
  if (!Ensure(p))
    return false;
  for (unsigned e = 0; e < 2; ++e) {
    auto &c = chains_[e];
    uint32_t index = 0;
    XrSwapchainImageAcquireInfo a{XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO};
    if (XR_FAILED(api_.acquire(c.handle, &a, &index)))
      return false;
    XrSwapchainImageWaitInfo w{XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO};
    w.timeout = 500000000;
    if (api_.waitImage(c.handle, &w) != XR_SUCCESS)
      return false;
    const bool copied =
        index < c.images.size() &&
        CopyPresentationImage(device_, c.images[index],
                              generated ? p.textures.generated[e]
                                        : p.textures.real[e]);
    XrSwapchainImageReleaseInfo r{XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};
    if (XR_FAILED(api_.release(c.handle, &r)) || !copied)
      return false;
  }
  return true;
}
XrResult FrameDelivery::Emit(const Packet &packet, const Packet *previous,
                             bool generated, XrTime time) {
  auto views = packet.views;
  for (unsigned e = 0; e < 2; ++e) {
    views[e].subImage.swapchain = chains_[e].handle;
    if (generated && previous)
      views[e].pose = Midpoint(previous->views[e].pose, views[e].pose);
  }
  auto p = packet.projection;
  p.views = views.data();
  const auto *pointer =
      reinterpret_cast<const XrCompositionLayerBaseHeader *>(&p);
  XrFrameEndInfo end{XR_TYPE_FRAME_END_INFO};
  end.displayTime = time;
  end.environmentBlendMode = XR_ENVIRONMENT_BLEND_MODE_OPAQUE;
  end.layerCount = 1;
  end.layers = &pointer;
  return api_.end(session_, &end);
}
void FrameDelivery::Run(std::shared_ptr<Packet> previous) {
  std::shared_ptr<Packet> pendingReal;
  XrResult result = XR_SUCCESS;
  while (true) {
    {
      std::lock_guard lock(mutex_);
      if (stop_)
        break;
    }
    XrFrameWaitInfo wait{XR_TYPE_FRAME_WAIT_INFO};
    XrFrameState state{XR_TYPE_FRAME_STATE};
    result = api_.wait(session_, &wait, &state);
    if (XR_FAILED(result))
      break;
    XrFrameBeginInfo begin{XR_TYPE_FRAME_BEGIN_INFO};
    result = api_.begin(session_, &begin);
    if (XR_FAILED(result))
      break;
    bool stop;
    {
      std::lock_guard lock(mutex_);
      state_ = state;
      ++tick_;
      workerResult_ = result;
      stop = stop_;
      changed_.notify_all();
    }
    std::shared_ptr<Packet> packet;
    bool generated = false, repeat = false;
    if (state.shouldRender && !stop) {
      if (pendingReal) {
        packet = std::move(pendingReal);
      } else {
        {
          std::lock_guard lock(mutex_);
          packet = std::move(queued_);
          changed_.notify_all();
        }
        if (packet) {
          generated = previous && packet->textures.interpolated &&
                      packet->textures.frame == previous->textures.frame + 1 &&
                      packet->projection.space == previous->projection.space;
          if (generated)
            for (unsigned e = 0; e < 2; ++e)
              generated = generated &&
                          !std::memcmp(&packet->views[e].fov,
                                       &previous->views[e].fov, sizeof(XrFovf));
          if (generated)
            pendingReal = packet;
        } else {
          packet = previous;
          repeat = !!packet;
        }
      }
    }
    bool copied = false;
    try {
      if (packet)
        copied = Fill(*packet, generated);
      if (copied)
        result = Emit(*packet, previous.get(), generated,
                      state.predictedDisplayTime);
      else {
        XrFrameEndInfo end{XR_TYPE_FRAME_END_INFO};
        end.displayTime = state.predictedDisplayTime;
        end.environmentBlendMode = XR_ENVIRONMENT_BLEND_MODE_OPAQUE;
        result = api_.end(session_, &end);
        if (packet && XR_SUCCEEDED(result))
          result = XR_ERROR_RUNTIME_FAILURE;
      }
    } catch (...) {
      XrFrameEndInfo end{XR_TYPE_FRAME_END_INFO};
      end.displayTime = state.predictedDisplayTime;
      end.environmentBlendMode = XR_ENVIRONMENT_BLEND_MODE_OPAQUE;
      api_.end(session_, &end);
      result = XR_ERROR_RUNTIME_FAILURE;
      Log("fg_delivery_exception original_frame_retained=1");
    }
    if (XR_FAILED(result))
      break;
    if (copied) {
      std::lock_guard lock(mutex_);
      if (generated) {
        ++generated_;
        presented_.push_back(packet->textures.frame);
        if (presented_.size() > 64)
          presented_.pop_front();
      } else if (repeat)
        ++repeated_;
      else
        ++real_;
      if (!generated)
        previous = packet;
      if (generated_ <= 3 || tick_ % 120 == 0 || packet->textures.capture)
        Log("fg_present mode=queued tick=%llu frame=%llu generated=%d "
            "repeat=%d result=%d generated_total=%u real_total=%u "
            "repeat_total=%u period_ns=%lld",
            (unsigned long long)tick_,
            (unsigned long long)packet->textures.frame, generated, repeat,
            result, generated_, real_, repeated_,
            (long long)state.predictedDisplayPeriod);
    }
    if (!state.shouldRender) {
      previous.reset();
      pendingReal.reset();
    }
  }
  {
    std::lock_guard lock(mutex_);
    running_ = false;
    if (XR_FAILED(result)) {
      fault_ = true;
      workerResult_ = result;
      Log("fg_delivery_failed result=%d bypass=1", result);
    }
    changed_.notify_all();
  }
}
} // namespace ttwvrnr
