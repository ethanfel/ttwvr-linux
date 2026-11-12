// SPDX-License-Identifier: GPL-3.0-only
#include "../src/FrameDelivery.h"
#include "../src/backend/FrameCamera.h"
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <thread>
using namespace ttwvrnr;
namespace {
std::atomic<unsigned> checks{0}, copiedPackets{0};
std::atomic<bool> failCopy{false}, protectedContext{false};
std::mutex dataMutex;
void Check(bool ok, const char *why) {
  ++checks;
  if (!ok) {
    std::fprintf(stderr, "FAIL: %s\n", why);
    std::abort();
  }
}
template <class T> T H(uintptr_t n) { return reinterpret_cast<T>(n); }
constexpr XrDuration Period = 8333333;
std::atomic<XrDuration> runtimePeriod{Period};
XrTime time = 1000000000;
bool open = false;
uintptr_t next = 1000;
struct Image {
  unsigned owned = 0;
  uintptr_t source = 0;
};
std::map<XrSwapchain, Image> images;
struct Submission {
  uint64_t frame;
  bool generated;
  float x;
};
std::vector<Submission> submissions;
XrResult XRAPI_CALL Wait(XrSession, const XrFrameWaitInfo *, XrFrameState *s) {
  std::this_thread::sleep_for(std::chrono::milliseconds(2));
  std::lock_guard lock(dataMutex);
  Check(!open, "prior runtime frame ended");
  const auto period = runtimePeriod.load();
  time += period;
  s->predictedDisplayTime = time;
  s->predictedDisplayPeriod = period;
  s->shouldRender = XR_TRUE;
  return XR_SUCCESS;
}
XrResult XRAPI_CALL Begin(XrSession, const XrFrameBeginInfo *) {
  std::lock_guard lock(dataMutex);
  Check(!open, "single runtime begin");
  open = true;
  return XR_SUCCESS;
}
XrResult XRAPI_CALL End(XrSession, const XrFrameEndInfo *e) {
  std::lock_guard lock(dataMutex);
  Check(open, "end follows runtime begin");
  open = false;
  Check(e && e->displayTime == time, "runtime display time used");
  if (e->layerCount) {
    auto *p =
        reinterpret_cast<const XrCompositionLayerProjection *>(e->layers[0]);
    if (images.count(p->views[0].subImage.swapchain)) {
      auto a = images.at(p->views[0].subImage.swapchain),
           b = images.at(p->views[1].subImage.swapchain);
      Check(!a.owned && !b.owned, "both submitted eyes released");
      Check(a.source / 100 == b.source / 100, "stereo source frame matches");
      Check(a.source % 100 + 1 == b.source % 100, "stereo source kind matches");
      submissions.push_back(
          {a.source / 100, a.source % 100 >= 10, p->views[0].pose.position.x});
    }
  }
  return XR_SUCCESS;
}
XrResult XRAPI_CALL Create(XrSession, const XrSwapchainCreateInfo *i,
                           XrSwapchain *s) {
  std::lock_guard lock(dataMutex);
  Check(i->usageFlags & XR_SWAPCHAIN_USAGE_TRANSFER_DST_BIT,
        "owned chains writable");
  *s = H<XrSwapchain>(next++);
  images[*s] = {};
  return XR_SUCCESS;
}
XrResult XRAPI_CALL Destroy(XrSwapchain s) {
  std::lock_guard lock(dataMutex);
  images.erase(s);
  return XR_SUCCESS;
}
XrResult XRAPI_CALL Enumerate(XrSwapchain s, uint32_t capacity, uint32_t *count,
                              XrSwapchainImageBaseHeader *p) {
  *count = 1;
  if (capacity)
    reinterpret_cast<XrSwapchainImageD3D11KHR *>(p)[0].texture =
        H<ID3D11Texture2D *>(reinterpret_cast<uintptr_t>(s));
  return XR_SUCCESS;
}
XrResult XRAPI_CALL Acquire(XrSwapchain s, const XrSwapchainImageAcquireInfo *,
                            uint32_t *index) {
  std::lock_guard lock(dataMutex);
  Check(images.at(s).owned == 0, "acquire released image");
  images[s].owned = 1;
  *index = 0;
  return XR_SUCCESS;
}
XrResult XRAPI_CALL WaitImage(XrSwapchain s, const XrSwapchainImageWaitInfo *) {
  std::lock_guard lock(dataMutex);
  Check(images.at(s).owned == 1, "wait after acquire");
  images[s].owned = 2;
  return XR_SUCCESS;
}
XrResult XRAPI_CALL Release(XrSwapchain s,
                            const XrSwapchainImageReleaseInfo *) {
  std::lock_guard lock(dataMutex);
  Check(images.at(s).owned == 2, "release after wait");
  images[s].owned = 0;
  return XR_SUCCESS;
}
FrameDeliveryDispatch Api() {
  return {Wait,      Begin,   End,       Create, Destroy,
          Enumerate, Acquire, WaitImage, Release};
}
StereoFrameTextures Textures(uint64_t f) {
  StereoFrameTextures t;
  t.owner = std::make_shared<int>(1);
  t.frame = f;
  t.interpolated = true;
  for (unsigned e = 0; e < 2; ++e) {
    t.real[e] = H<ID3D11Texture2D *>(f * 100 + e);
    t.generated[e] = H<ID3D11Texture2D *>(f * 100 + 10 + e);
    t.format[e] = 29;
  }
  return t;
}
} // namespace
namespace ttwvrnr {
void Log(const char *, ...) {}
bool DrainPresentation(ID3D11Device *) {
  Check(protectedContext, "GPU drain before context protection is restored");
  std::lock_guard lock(dataMutex);
  Check(!open, "worker frame ended before GPU drain");
  return true;
}
bool SetPresentationThreading(ID3D11Device *, bool enabled, bool &previous) {
  previous = protectedContext.exchange(enabled);
  return true;
}
StereoFrameTextures CopyFrameTextures(ID3D11Device *,
                                      const StereoFrameTextures &t) {
  ++copiedPackets;
  auto copy = t;
  copy.owner = std::make_shared<int>(2);
  return copy;
}
bool CopyPresentationImage(ID3D11Device *, ID3D11Texture2D *to,
                           ID3D11Texture2D *from) {
  Check(protectedContext, "worker context protected");
  std::lock_guard lock(dataMutex);
  auto &im = images.at(H<XrSwapchain>(reinterpret_cast<uintptr_t>(to)));
  Check(im.owned == 2, "copy only waited owned image");
  im.source = reinterpret_cast<uintptr_t>(from);
  return !failCopy;
}
} // namespace ttwvrnr
int main() {
  const float view[] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, -10, -20, -30, 1};
  const float projection[] = {
      .760018468f, 0,           0, 0, 0, 1.013358f,    0, 0, 0,
      0,           1.00075817f, 1, 0, 0, -5.00379086f, 0};
  auto camera = DecodeTTWVRCamera(view, projection);
  Check(camera.valid && std::abs(camera.nearPlane - 5) < .001f &&
            std::abs(camera.fov - 2 * .778764f) < .0001f,
        "verified TTWVR projection decoded");
  Check(camera.position[0] == 10 && camera.position[1] == 20 &&
            camera.position[2] == 30,
        "absolute camera position");
  Matrix4 inv;
  Check(Inverse4(camera.worldToClip, inv), "invertible camera");
  auto id = Multiply4(camera.worldToClip, inv);
  for (unsigned k = 0; k < 16; ++k)
    Check(std::abs(id[k] - (k % 5 == 0)) < 1e-4, "camera inverse roundtrip");
  float invalid[16]{};
  Check(!DecodeTTWVRCamera(view, invalid).valid, "invalid projection bypass");
  XrCompositionLayerProjectionView views[2]{};
  for (unsigned e = 0; e < 2; ++e) {
    views[e].type = XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW;
    views[e].pose.orientation.w = 1;
    views[e].subImage.swapchain = H<XrSwapchain>(e + 1);
    views[e].subImage.imageRect.extent = {2944, 2208};
  }
  XrCompositionLayerProjection p{XR_TYPE_COMPOSITION_LAYER_PROJECTION};
  p.space = H<XrSpace>(5);
  p.viewCount = 2;
  p.views = views;
  auto *layer = reinterpret_cast<const XrCompositionLayerBaseHeader *>(&p);
  XrFrameEndInfo end{XR_TYPE_FRAME_END_INFO};
  end.environmentBlendMode = XR_ENVIRONMENT_BLEND_MODE_OPAQUE;
  end.layerCount = 1;
  end.layers = &layer;
  XrFrameWaitInfo wait{XR_TYPE_FRAME_WAIT_INFO};
  XrFrameBeginInfo begin{XR_TYPE_FRAME_BEGIN_INFO};
  XrFrameState state{XR_TYPE_FRAME_STATE};
  {
    FrameDelivery d(H<XrSession>(1), H<ID3D11Device *>(1), Api());
    Check(d.Wait(&wait, &state, false) == XR_SUCCESS &&
              state.predictedDisplayPeriod == Period,
          "off timing unchanged");
    d.Begin(&begin);
    end.displayTime = state.predictedDisplayTime;
    d.End(&end, {});
    d.Complete();
    Check(!copiedPackets, "off no packet copies");
    d.Wait(&wait, &state, true);
    d.Begin(&begin);
    end.displayTime = state.predictedDisplayTime;
    views[0].pose.position.x = 1;
    d.End(&end, Textures(1));
    d.Complete();
    XrTime previousPrediction = state.predictedDisplayTime;
    for (unsigned f = 2; f <= 7; ++f) {
      runtimePeriod = f == 4 ? 2 * Period : Period;
      Check(d.Wait(&wait, &state, true) == XR_SUCCESS &&
                state.predictedDisplayPeriod >= 2 * Period,
            "application predicts at least two runtime slots");
      Check(state.predictedDisplayTime > previousPrediction,
            "prediction stays monotonic when runtime cadence changes");
      previousPrediction = state.predictedDisplayTime;
      Check(d.Begin(&begin) == XR_SUCCESS, "proxied begin succeeds");
      end.displayTime = state.predictedDisplayTime;
      views[0].pose.position.x = float(f);
      Check(d.End(&end, Textures(f)) == XR_SUCCESS, "packet accepted");
      views[0].pose.position.x = 999;
      d.Complete();
    }
    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (!d.WasGenerated(7) && std::chrono::steady_clock::now() < deadline)
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    Check(d.WasGenerated(7) && d.GeneratedPairs() >= 6,
          "both consecutive eyes actually interpolated");
    // An unsupported menu must drain the worker and forward an ordinary frame.
    d.Wait(&wait, &state, true);
    d.Begin(&begin);
    end.displayTime = state.predictedDisplayTime;
    end.layerCount = 0;
    Check(d.End(&end, {}) == XR_SUCCESS,
          "menu bypass obtains a real runtime frame");
    d.Complete();
    end.layerCount = 1;
    {
      std::lock_guard lock(dataMutex);
      Check(images.empty(), "menu stop releases old presentation chains");
    }
    d.Wait(&wait, &state, true);
    d.Begin(&begin);
    end.displayTime = state.predictedDisplayTime;
    d.End(&end, Textures(8));
    d.Complete();
    d.Wait(&wait, &state, true);
    d.Begin(&begin);
    end.displayTime = state.predictedDisplayTime;
    d.End(&end, Textures(9));
    d.Complete();
    deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (!d.WasGenerated(9) && std::chrono::steady_clock::now() < deadline)
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    Check(d.WasGenerated(9), "stereo generation resumes after menu bypass");
    Check(d.Wait(&wait, &state, false) == XR_SUCCESS &&
              state.predictedDisplayPeriod == Period,
          "off restores runtime pacing");
    d.Begin(&begin);
    end.displayTime = state.predictedDisplayTime;
    d.End(&end, {});
    d.Complete();
    {
      std::lock_guard lock(dataMutex);
      bool gen = false, real = false;
      for (auto &s : submissions) {
        if (s.frame == 7 && s.generated) {
          Check(s.x == 6.5f, "generated midpoint uses copied poses");
          gen = true;
        }
        if (s.frame == 7 && !s.generated) {
          Check(s.x == 7, "real pose retained after caller mutation");
          real = true;
        }
      }
      Check(gen && real, "generated then real source frame submitted");
    }
  }
  Check(images.empty(), "owned chains destroyed after worker exits");
  Check(!protectedContext, "original context threading restored");
  {
    FrameDelivery d(H<XrSession>(2), H<ID3D11Device *>(1), Api());
    d.Wait(&wait, &state, true);
    d.Begin(&begin);
    end.displayTime = state.predictedDisplayTime;
    d.End(&end, Textures(20));
    d.Complete();
    failCopy = true;
    Check(d.Wait(&wait, &state, true) == XR_SUCCESS,
          "worker failure recovers through original wait");
    failCopy = false;
    d.Begin(&begin);
    end.displayTime = state.predictedDisplayTime;
    Check(d.End(&end, {}) == XR_SUCCESS, "original frame follows failed copy");
    d.Complete();
    Check(d.GeneratedPairs() == 0, "failed output never counted");
  }
  Check(images.empty(), "failure teardown releases owned chains");
  std::printf("Queued stereo delivery: %u checks passed\n", checks.load());
}
