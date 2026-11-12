// SPDX-License-Identifier: GPL-3.0-only
// Derived from Ethanfel TF2VRNeuralRendering; see THIRD-PARTY-NOTICES.md.
#include "Graphics.h"
#include "D3D9Trace.h"
#include "SpatialNR.h"
#include "Support.h"
#define XR_USE_GRAPHICS_API_D3D11
#define XR_USE_PLATFORM_WIN32
#include <openxr.h>
#include <openxr_platform.h>
#include <openxr_loader_negotiation.h>
#include <algorithm>
#include <array>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <map>
#include <mutex>
#include <set>
#include <vector>
#include <type_traits>

namespace ttwvrnr {
namespace {
template<class T> uint64_t HandleId(T h) {
    if constexpr(std::is_pointer_v<T>) return reinterpret_cast<uintptr_t>(h);
    else return h;
}
struct Instance {
    XrInstance handle = XR_NULL_HANDLE;
    Config config;
    bool active = false;
    PFN_xrGetInstanceProcAddr getProc = nullptr;
    PFN_xrDestroyInstance destroyInstance = nullptr;
    PFN_xrCreateSession createSession = nullptr;
    PFN_xrDestroySession destroySession = nullptr;
    PFN_xrCreateSwapchain createSwapchain = nullptr;
    PFN_xrDestroySwapchain destroySwapchain = nullptr;
    PFN_xrEnumerateSwapchainImages enumerateImages = nullptr;
    PFN_xrAcquireSwapchainImage acquireImage = nullptr;
    PFN_xrWaitSwapchainImage waitImage = nullptr;
    PFN_xrReleaseSwapchainImage releaseImage = nullptr;
    PFN_xrWaitFrame waitFrame = nullptr;
    PFN_xrEndFrame endFrame = nullptr;
    PFN_xrLocateViews locateViews = nullptr;
};
struct CapturedView {
    std::shared_ptr<CapturedImage> snapshot;
    ViewLayout layout;
    XrPosef pose{};
    XrFovf fov{};
    uint32_t imageIndex = 0;
    int64_t format = 0;
    uint64_t generation = 0;
    bool depth = false, motion = false, synthesis = false;
};
struct CapturedFrame {
    bool neural = false;
    bool dlaa = false;
    bool temporal = false;
    struct { float x=0,y=0; } jitter;
    uint64_t frame = 0;
    XrTime displayTime = 0;
    uint32_t layer = 0;
    XrSpace space = XR_NULL_HANDLE;
    std::vector<CapturedView> views;
};
struct Session {
    std::shared_ptr<Instance> instance;
    std::mutex mutex;
    // Borrowed from the session graphics binding; never used after session destruction.
    ID3D11Device* device = nullptr;
    uint64_t frames = 0;
    std::chrono::steady_clock::time_point waitReturned{};
    unsigned long waitThread = 0;
    double waitMs = 0, appMs = 0, endMs = 0;
    unsigned waitSamples = 0, appSamples = 0, endSamples = 0;
    XrDuration displayPeriod = 0;
    bool capturePending = false;
    unsigned captureAttempts = 0;
    // At most two stereo frames. Only owned copies and value metadata survive
    // xrEndFrame; no caller-owned structures or runtime textures are retained.
    std::vector<CapturedFrame> captureFrames;
    std::set<XrSwapchain> captureTargets;
    std::map<uint64_t, ViewHistory> histories;
};
struct Swapchain {
    std::shared_ptr<Session> session;
    std::mutex mutex;
    XrSwapchainCreateInfo info{};
    std::vector<ID3D11Texture2D*> images;
    ImageQueue queue;
    std::shared_ptr<CapturedImage> snapshot;
    uint64_t snapshotFrame = 0;
    uint32_t releasedIndex = 0;
    bool hasReleased = false;
    uint64_t acquireCalls = 0, waitCalls = 0, releaseCalls = 0;
    XrResult lastAcquire = XR_SUCCESS, lastWait = XR_SUCCESS;
};
std::mutex registryMutex;
std::map<XrInstance, std::shared_ptr<Instance>> instances;
std::map<XrSession, std::shared_ptr<Session>> sessions;
std::map<XrSwapchain, std::shared_ptr<Swapchain>> swapchains;

template<class Map, class Key> auto Lookup(Map& map, Key key) -> typename Map::mapped_type {
    std::lock_guard lock(registryMutex);
    auto it = map.find(key);
    return it == map.end() ? nullptr : it->second;
}
template<class T> bool Resolve(Instance& i, const char* name, T& fn) {
    PFN_xrVoidFunction result = nullptr;
    if (i.getProc(i.handle, name, &result) != XR_SUCCESS || !result) return false;
    fn = reinterpret_cast<T>(result); return true;
}
const XrBaseInStructure* Find(const void* chain, XrStructureType type) {
    auto p = static_cast<const XrBaseInStructure*>(chain);
    for (unsigned n = 0; p && n < 64; ++n, p = p->next) if (p->type == type) return p;
    return nullptr;
}

XrResult XRAPI_CALL DestroyInstance(XrInstance handle) {
    auto i = Lookup(instances, handle);
    if (!i || !i->destroyInstance) return XR_ERROR_HANDLE_INVALID;
    {
        std::lock_guard lock(registryMutex);
        for (auto& entry : sessions) if (entry.second->instance == i) {
        }
    }
    const auto r = i->destroyInstance(handle);
    if (r == XR_SUCCESS) {
        std::lock_guard lock(registryMutex);
        for (auto it = swapchains.begin(); it != swapchains.end();)
            if (it->second->session->instance == i) it = swapchains.erase(it); else ++it;
        for (auto it = sessions.begin(); it != sessions.end();)
            if (it->second->instance == i) it = sessions.erase(it); else ++it;
        instances.erase(handle);
    }
    return r;
}
XrResult XRAPI_CALL CreateSession(XrInstance instance, const XrSessionCreateInfo* info, XrSession* handle) {
    auto i = Lookup(instances, instance);
    if (!i || !i->createSession) return XR_ERROR_HANDLE_INVALID;
    const auto r = i->createSession(instance, info, handle);
    if (r != XR_SUCCESS || !info || !handle) return r;
    auto s = std::make_shared<Session>(); s->instance = i;
    const auto binding = reinterpret_cast<const XrGraphicsBindingD3D11KHR*>(Find(info->next, XR_TYPE_GRAPHICS_BINDING_D3D11_KHR));
    if (binding) s->device = binding->device;
    s->capturePending = i->config.capture && i->config.captureFirst;
    {
        std::lock_guard lock(registryMutex); sessions[*handle] = s;
    }
    Log("session=%p graphics=%s spatial_nr_opt_in=separate_control",
        reinterpret_cast<void*>(*handle), s->device ? "D3D11" : "unsupported_passthrough");
    return r;
}
XrResult XRAPI_CALL DestroySession(XrSession handle) {
    auto s = Lookup(sessions, handle);
    if (!s) return XR_ERROR_HANDLE_INVALID;
    const auto r = s->instance->destroySession(handle);
    if (r == XR_SUCCESS) {
        std::lock_guard lock(registryMutex);
        for (auto it = swapchains.begin(); it != swapchains.end();)
            if (it->second->session == s) it = swapchains.erase(it); else ++it;
        sessions.erase(handle);
    }
    return r;
}
XrResult XRAPI_CALL CreateSwapchain(XrSession session, const XrSwapchainCreateInfo* info, XrSwapchain* handle) {
    auto s = Lookup(sessions, session);
    if (!s) return XR_ERROR_HANDLE_INVALID;
    XrSwapchainCreateInfo modified{};
    const auto* used = info;
    if (info && s->device && s->instance->config.capture &&
        (info->usageFlags & XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT) &&
        !(info->createFlags & XR_SWAPCHAIN_CREATE_PROTECTED_CONTENT_BIT) &&
        !(info->usageFlags & XR_SWAPCHAIN_USAGE_TRANSFER_SRC_BIT)) {
        modified = *info; modified.usageFlags |= XR_SWAPCHAIN_USAGE_TRANSFER_SRC_BIT; used = &modified;
    }
    auto r = s->instance->createSwapchain(session, used, handle);
    if (used != info && (r == XR_ERROR_FEATURE_UNSUPPORTED || r == XR_ERROR_SWAPCHAIN_FORMAT_UNSUPPORTED || r == XR_ERROR_VALIDATION_FAILURE)) {
        Log("capture_transfer_src_unavailable result=%d; retrying original swapchain flags", r);
        used = info; r = s->instance->createSwapchain(session, info, handle);
    }
    if (r != XR_SUCCESS || !used || !handle) return r;
    auto sc = std::make_shared<Swapchain>(); sc->session = s; sc->info = *used; sc->info.next = nullptr;
    {
        std::lock_guard lock(registryMutex); swapchains[*handle] = sc;
    }
    Log("swapchain=%p size=%ux%u array=%u samples=%u format=%lld usage=0x%llx capture_copy_allowed=%d",
        reinterpret_cast<void*>(*handle), used->width, used->height, used->arraySize, used->sampleCount,
        static_cast<long long>(used->format), static_cast<unsigned long long>(used->usageFlags),
        !!(used->usageFlags & XR_SWAPCHAIN_USAGE_TRANSFER_SRC_BIT));
    return r;
}
XrResult XRAPI_CALL DestroySwapchain(XrSwapchain handle) {
    auto sc = Lookup(swapchains, handle);
    if (!sc) return XR_ERROR_HANDLE_INVALID;
    const auto r = sc->session->instance->destroySwapchain(handle);
    if (r == XR_SUCCESS) { std::lock_guard lock(registryMutex); swapchains.erase(handle); }
    return r;
}
XrResult XRAPI_CALL EnumerateImages(XrSwapchain handle, uint32_t capacity, uint32_t* count, XrSwapchainImageBaseHeader* images) {
    auto sc = Lookup(swapchains, handle);
    if (!sc) return XR_ERROR_HANDLE_INVALID;
    const auto inputType = capacity && images ? images[0].type : XR_TYPE_UNKNOWN;
    const auto r = sc->session->instance->enumerateImages(handle, capacity, count, images);
    // The caller chooses the image structure before enumeration. Proton 11's
    // wineopenxr D3D11 bridge memcpy()s its internal image array, including
    // uninitialized type/next fields, over the caller's initialized headers.
    // Use the saved input type and read only texture; never follow output next.
    // ValveSoftware/Proton wineopenxr/openxr_loader.c, xrEnumerateSwapchainImages.
    if (r == XR_SUCCESS && sc->session->device && capacity && count && *count <= capacity && images &&
        inputType == XR_TYPE_SWAPCHAIN_IMAGE_D3D11_KHR) {
        const auto d3d = reinterpret_cast<const XrSwapchainImageD3D11KHR*>(images);
        std::lock_guard lock(sc->mutex);
        sc->images.clear();
        for (uint32_t n = 0; n < *count; ++n) sc->images.push_back(d3d[n].texture);
    }
    {
        std::lock_guard lock(sc->mutex);
        Log("enumerate_images swapchain=%p result=%d capacity=%u count=%u input_type=%d output_type=%d tracked=%zu first_texture=%p",
            reinterpret_cast<void*>(handle), r, capacity, count && XR_SUCCEEDED(r) ? *count : 0,
            inputType, capacity && images ? images[0].type : XR_TYPE_UNKNOWN, sc->images.size(),
            sc->images.empty() ? nullptr : static_cast<void*>(sc->images[0]));
    }
    return r;
}
XrResult XRAPI_CALL AcquireImage(XrSwapchain handle, const XrSwapchainImageAcquireInfo* info, uint32_t* index) {
    auto sc = Lookup(swapchains, handle);
    if (!sc) return XR_ERROR_HANDLE_INVALID;
    const auto r = sc->session->instance->acquireImage(handle, info, index);
    {
        std::lock_guard lock(sc->mutex);
        ++sc->acquireCalls; sc->lastAcquire = r;
        if (r == XR_SUCCESS && index) sc->queue.Acquired(*index);
    }
    return r;
}
XrResult XRAPI_CALL WaitImage(XrSwapchain handle, const XrSwapchainImageWaitInfo* info) {
    auto sc = Lookup(swapchains, handle);
    if (!sc) return XR_ERROR_HANDLE_INVALID;
    const auto r = sc->session->instance->waitImage(handle, info);
    {
        std::lock_guard lock(sc->mutex);
        ++sc->waitCalls; sc->lastWait = r;
        if (r == XR_SUCCESS) sc->queue.Waited();
    }
    return r;
}
XrResult XRAPI_CALL ReleaseImage(XrSwapchain handle, const XrSwapchainImageReleaseInfo* info) {
    auto sc = Lookup(swapchains, handle);
    if (!sc) return XR_ERROR_HANDLE_INVALID;
    auto s = sc->session;
    bool capture = false, diagnose = false; uint64_t frame = 0;
    {
        std::lock_guard lock(s->mutex);
        capture = s->capturePending && (s->captureTargets.empty() || s->captureTargets.count(handle));
        diagnose = s->capturePending && s->captureAttempts < 2;
        frame = s->frames;
    }
    std::shared_ptr<CapturedImage> snapshot;
    std::optional<uint32_t> index;
    {
        std::lock_guard lock(sc->mutex);
        ++sc->releaseCalls;
        index = sc->queue.Writable();
        if (capture && index && *index < sc->images.size() &&
            (sc->info.usageFlags & XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT) &&
            (sc->info.usageFlags & XR_SWAPCHAIN_USAGE_TRANSFER_SRC_BIT) &&
            !(sc->info.createFlags & XR_SWAPCHAIN_CREATE_PROTECTED_CONTENT_BIT)) {
            try { snapshot = CaptureImage(s->device, sc->images[*index], sc->info.format,
                s->instance->config.inspectBindings, s->instance->config.captureCandidates); }
            catch (...) { Log("capture_copy_failed exception; original frame continues"); }
        }
        if (diagnose)
            Log("capture_release frame=%llu swapchain=%p selected=%d writable=%d index=%u images=%zu acquires=%llu waits=%llu releases=%llu last_acquire=%d last_wait=%d copied=%d",
                static_cast<unsigned long long>(frame), reinterpret_cast<void*>(handle), capture,
                index.has_value(), index.value_or(0), sc->images.size(),
                static_cast<unsigned long long>(sc->acquireCalls), static_cast<unsigned long long>(sc->waitCalls),
                static_cast<unsigned long long>(sc->releaseCalls), sc->lastAcquire, sc->lastWait, !!snapshot);
    }
    // Never inspect/copy a runtime texture below this point.
    const auto r = s->instance->releaseImage(handle, info);
    if (diagnose) Log("capture_release_result swapchain=%p result=%d", reinterpret_cast<void*>(handle), r);
    if (r == XR_SUCCESS) {
        std::lock_guard lock(sc->mutex);
        sc->snapshot = std::move(snapshot); sc->snapshotFrame = frame;
        sc->hasReleased = index.has_value();
        if (index) sc->releasedIndex = *index;
        sc->queue.Released();
    }
    return r;
}

ViewLayout Layout(const XrSwapchainSubImage& image) {
    return {HandleId(image.swapchain), image.imageArrayIndex,
        image.imageRect.offset.x, image.imageRect.offset.y, image.imageRect.extent.width, image.imageRect.extent.height};
}
bool WriteCapturedFrame(const CapturedFrame& frame, const std::wstring& burstBase, unsigned burstIndex, unsigned burstCount) {
    const auto base = burstBase + L"-frame" + std::to_wstring(frame.frame) + L"-layer" + std::to_wstring(frame.layer);
    unsigned captured = 0;
    for (unsigned eye = 0; eye < frame.views.size(); ++eye) {
        const auto& view = frame.views[eye];
        const auto& layout = view.layout;
        const auto name = base + L"-eye" + std::to_wstring(eye);
        if (!view.snapshot->WriteView(layout, name + L".dds")) continue;
        ++captured;
        try { view.snapshot->WriteCandidates(layout, name); }
        catch (...) { Log("capture_candidates_write_failed exception; color saved"); }
        const auto& bindings = view.snapshot->BindingsJson();
        if (!bindings.empty()) {
            FILE* file = _wfopen((name + L"-bindings.json").c_str(), L"wb");
            bool saved = false;
            if (file) {
                saved = std::fwrite(bindings.data(), 1, bindings.size(), file) == bindings.size();
                if (std::fclose(file)) saved = false;
            }
            Log("capture_bindings_saved frame=%llu eye=%u ok=%d validated_neural_guides=false",
                static_cast<unsigned long long>(frame.frame), eye, saved);
        }
        FILE* meta = _wfopen((name + L".json").c_str(), L"wb");
        if (meta) {
            std::fprintf(meta,
                "{\n  \"mode\": \"%s\", \"neural_rendering\": %s,\n"
                "  \"dlaa\": %s, \"jitter_pixels\": [%g, %g],\n"
                "  \"frame\": %llu, \"display_time\": %lld, \"layer\": %u, \"eye\": %u,\n"
                "  \"burst_index\": %u, \"burst_frames\": %u, \"readback_after_burst\": true, \"file_write_async\": true,\n"
                "  \"image_index\": %u, \"array_slice\": %u, \"format\": %lld,\n"
                "  \"rect\": [%d, %d, %d, %d],\n"
                "  \"position\": [%g, %g, %g], \"orientation\": [%g, %g, %g, %g],\n"
                "  \"fov\": [%g, %g, %g, %g],\n"
                "  \"depth_submitted\": %s, \"motion_submitted\": %s, \"synthesis_submitted\": %s\n}\n",
                frame.dlaa ? (frame.neural ? "temporal_nr_dlaa" : "dlaa") : frame.neural ? (frame.temporal ? "temporal_nr" : "spatial_nr_reset_every_frame") : "capture_only", frame.neural ? "true" : "false",
                frame.dlaa ? "true" : "false",frame.jitter.x,frame.jitter.y,
                static_cast<unsigned long long>(frame.frame), static_cast<long long>(frame.displayTime), frame.layer, eye,
                burstIndex, burstCount, view.imageIndex, layout.slice, static_cast<long long>(view.format),
                layout.x, layout.y, layout.width, layout.height,
                view.pose.position.x, view.pose.position.y, view.pose.position.z,
                view.pose.orientation.x, view.pose.orientation.y, view.pose.orientation.z, view.pose.orientation.w,
                view.fov.angleLeft, view.fov.angleRight, view.fov.angleUp, view.fov.angleDown,
                view.depth ? "true" : "false", view.motion ? "true" : "false", view.synthesis ? "true" : "false");
            std::fclose(meta);
        }
    }
    if (captured == 2)
        Log("stereo_capture_saved frame=%llu layer=%u eyes=2 burst_index=%u burst_frames=%u neural_rendering=%d",
            static_cast<unsigned long long>(frame.frame), frame.layer, burstIndex, burstCount, frame.neural);
    return captured == 2;
}
bool Consecutive(const CapturedFrame& previous, const CapturedFrame& next) {
    if (next.frame != previous.frame + 1 || next.layer != previous.layer || next.space != previous.space ||
        next.displayTime <= previous.displayTime || next.displayTime - previous.displayTime > 250000000 ||
        previous.views.size() != 2 || next.views.size() != 2) return false;
    for (unsigned eye = 0; eye < 2; ++eye) {
        const auto& a = previous.views[eye]; const auto& b = next.views[eye];
        if (!(a.layout == b.layout) || a.format != b.format || a.generation != b.generation ||
            a.fov.angleLeft != b.fov.angleLeft || a.fov.angleRight != b.fov.angleRight ||
            a.fov.angleUp != b.fov.angleUp || a.fov.angleDown != b.fov.angleDown) return false;
    }
    return true;
}
void InspectFrame(Session& s, const XrFrameEndInfo* info) {
    if (!info || (info->layerCount && !info->layers)) { s.captureFrames.clear(); return; }
    bool selectedTargets = false;
    CapturedFrame capturedFrame;
    capturedFrame.neural = SpatialApplied(s.frames);
    capturedFrame.dlaa = DlaaApplied(s.frames);
    capturedFrame.temporal = TemporalNRApplied(s.frames);
    ProcessingJitter(s.frames,capturedFrame.jitter.x,capturedFrame.jitter.y);
    capturedFrame.frame = s.frames; capturedFrame.displayTime = info->displayTime;
    std::map<uint64_t, bool> seen;
    for (uint32_t layerIndex = 0; layerIndex < std::min(info->layerCount, 64u); ++layerIndex) {
        auto header = info->layers[layerIndex];
        if (!header || header->type != XR_TYPE_COMPOSITION_LAYER_PROJECTION) continue;
        const auto* p = reinterpret_cast<const XrCompositionLayerProjection*>(header);
        if (!p->views || p->viewCount > 16) continue;
        const bool selected = !selectedTargets && p->viewCount == 2;
        if (selected) {
            selectedTargets = true;
            s.captureTargets = {p->views[0].subImage.swapchain, p->views[1].subImage.swapchain};
            capturedFrame.layer = layerIndex; capturedFrame.space = p->space;
        }
        for (uint32_t eye = 0; eye < p->viewCount; ++eye) {
            const auto& view = p->views[eye];
            const auto layout = Layout(view.subImage);
            const uint64_t key = (static_cast<uint64_t>(layerIndex) << 32) | eye;
            seen[key] = true;
            auto& history = s.histories[key];
            const bool reset = history.Observe(layout, info->displayTime);
            const bool depth = Find(view.next, XR_TYPE_COMPOSITION_LAYER_DEPTH_INFO_KHR) != nullptr;
            const bool motion = Find(view.next, XR_TYPE_COMPOSITION_LAYER_SPACE_WARP_INFO_FB) != nullptr;
            const bool synthesis = Find(view.next, XR_TYPE_FRAME_SYNTHESIS_INFO_EXT) != nullptr;
            if (reset || s.frames % s.instance->config.logEvery == 0)
                Log("frame=%llu layer=%u eye=%u swapchain=%p slice=%u rect=%d,%d,%d,%d depth_submitted=%d motion_submitted=%d synthesis_submitted=%d history_generation=%llu",
                    static_cast<unsigned long long>(s.frames), layerIndex, eye, reinterpret_cast<void*>(view.subImage.swapchain),
                    layout.slice, layout.x, layout.y, layout.width, layout.height, depth, motion, synthesis,
                    static_cast<unsigned long long>(history.Generation()));
            if (!s.capturePending || !selected) continue;
            auto sc = Lookup(swapchains, view.subImage.swapchain);
            if (!sc || sc->session.get() != &s) continue;
            std::lock_guard lock(sc->mutex);
            if (s.captureAttempts < 2)
                Log("capture_view frame=%llu eye=%u snapshot=%d snapshot_frame=%llu bounds_valid=%d releases=%llu images=%zu",
                    static_cast<unsigned long long>(s.frames), eye, !!sc->snapshot,
                    static_cast<unsigned long long>(sc->snapshotFrame),
                    Fits(sc->info.width, sc->info.height, sc->info.arraySize, layout),
                    static_cast<unsigned long long>(sc->releaseCalls), sc->images.size());
            if (!sc->snapshot || sc->snapshotFrame != s.frames || !Fits(sc->info.width, sc->info.height, sc->info.arraySize, layout)) continue;
            CapturedView copy;
            copy.snapshot = sc->snapshot; copy.layout = layout; copy.pose = view.pose; copy.fov = view.fov;
            copy.imageIndex = sc->releasedIndex; copy.format = sc->info.format; copy.generation = history.Generation();
            copy.depth = depth; copy.motion = motion; copy.synthesis = synthesis;
            capturedFrame.views.push_back(std::move(copy));
        }
    }
    for (auto& [key, history] : s.histories) if (!seen.count(key)) history.Invalidate();
    if (!s.capturePending) return;
    bool saved = false, unsuccessful = false;
    if (capturedFrame.views.size() == 2) {
        if (!s.captureFrames.empty() && !Consecutive(s.captureFrames.back(), capturedFrame)) {
            s.captureFrames.clear(); unsuccessful = true;
            Log("capture_burst_reset reason=frame_layout_or_time_discontinuity");
        }
        s.captureFrames.push_back(std::move(capturedFrame));
        const unsigned goal = std::clamp(s.instance->config.burstFrames, 1u, 2u);
        if (s.captureFrames.size() == goal) {
            // Read back on the graphics thread, then release all D3D references
            // before sending CPU-only data to the single bounded IO worker.
            const auto readbackStart = GetTickCount64();
            bool ready = true;
            for (auto& frame : s.captureFrames) for (auto& view : frame.views)
                if (!view.snapshot->PrepareForAsyncWrite()) ready = false;
            Log("capture_cpu_readback frames=%u ready=%d elapsed_ms=%llu", goal, ready,
                static_cast<unsigned long long>(GetTickCount64() - readbackStart));
            const auto dir = CaptureDirectory();
            if (ready && !dir.empty()) {
                const auto base = dir + L"\\capture-" + std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(GetTickCount64());
                saved = QueueCaptureWrite([frames = std::move(s.captureFrames), base, goal]() {
                    bool complete = true;
                    for (unsigned n = 0; n < goal; ++n)
                        if (!WriteCapturedFrame(frames[n], base, n, goal)) complete = false;
                    const bool neural=std::all_of(frames.begin(),frames.end(),[](const auto& frame){return frame.neural;});
                    Log("capture_burst_saved frames=%u complete=%d async=1 neural_rendering=%d", goal, complete, neural);
                });
                Log("capture_async_queued frames=%u accepted=%d", goal, saved);
            }
            s.captureFrames.clear();
            unsuccessful = !saved;
        }
    } else {
        s.captureFrames.clear();
        unsuccessful = selectedTargets;
    }
    if (unsuccessful) ++s.captureAttempts;
    if (saved || s.captureAttempts >= 60) {
        if (!saved) Log("capture_failed: no complete stereo burst after 60 unsuccessful stereo submissions");
        s.capturePending = false; s.captureAttempts = 0; s.captureFrames.clear();
    }
}
// Optional CPU wall-time diagnostics. They do not change runtime pacing or
// imply GPU execution time. App-span samples require serial same-thread calls.
XrResult XRAPI_CALL WaitFrame(XrSession handle, const XrFrameWaitInfo* info, XrFrameState* state) {
    auto s = Lookup(sessions, handle);
    if (!s) return XR_ERROR_HANDLE_INVALID;
    if (!s->instance->config.frameTiming) return s->instance->waitFrame(handle, info, state);
    const auto start = std::chrono::steady_clock::now();
    const auto r = s->instance->waitFrame(handle, info, state);
    const auto returned = std::chrono::steady_clock::now();
    std::lock_guard lock(s->mutex);
    s->waitMs += std::chrono::duration<double, std::milli>(returned - start).count();
    ++s->waitSamples;
    s->waitReturned = returned;
    s->waitThread = r == XR_SUCCESS ? GetCurrentThreadId() : 0;
    if (r == XR_SUCCESS && state) s->displayPeriod = state->predictedDisplayPeriod;
    return r;
}
XrResult XRAPI_CALL EndFrame(XrSession handle, const XrFrameEndInfo* info) {
    auto s = Lookup(sessions, handle);
    if (!s) return XR_ERROR_HANDLE_INVALID;
    // Forward every projection and extension chain unchanged.
    // File IO only touches owned copies and happens after the original submission.
    const bool timing = s->instance->config.frameTiming;
    const auto before = timing ? std::chrono::steady_clock::now() : std::chrono::steady_clock::time_point{};
    const auto r = s->instance->endFrame(handle, info);
    const auto after = timing ? std::chrono::steady_clock::now() : std::chrono::steady_clock::time_point{};
    std::lock_guard lock(s->mutex);
    if (timing) {
        s->endMs += std::chrono::duration<double, std::milli>(after - before).count();
        ++s->endSamples;
        if (s->waitThread == GetCurrentThreadId() && before >= s->waitReturned) {
            s->appMs += std::chrono::duration<double, std::milli>(before - s->waitReturned).count();
            ++s->appSamples;
        }
        s->waitThread = 0;
        if (s->endSamples >= s->instance->config.logEvery) {
            Log("frame_timing frame=%llu wait_samples=%u app_samples=%u end_samples=%u wait_ms=%.3f app_ms=%.3f end_ms=%.3f display_period_ms=%.3f",
                (unsigned long long)s->frames, s->waitSamples, s->appSamples, s->endSamples,
                s->waitSamples ? s->waitMs / s->waitSamples : 0.,
                s->appSamples ? s->appMs / s->appSamples : 0., s->endMs / s->endSamples,
                s->displayPeriod / 1000000.);
            s->waitMs = s->appMs = s->endMs = 0;
            s->waitSamples = s->appSamples = s->endSamples = 0;
        }
    }
    if (r == XR_SUCCESS) {
        try { InspectFrame(*s, info); }
        catch (...) { Log("frame_inspection_failed exception; original frame already submitted"); s->capturePending = false; s->captureFrames.clear(); }
    } else {
        s->captureFrames.clear();
        for (auto& entry : s->histories) entry.second.Invalidate();
        Log("end_frame_result=%d; temporal history invalidated", r);
    }
    FinishD3D9Trace(s->frames, !s->capturePending || r != XR_SUCCESS);
    ++s->frames;
    if (s->instance->config.capture && !s->capturePending && !CaptureWriteBusy() && s->frames % 30 == 0 && ConsumeCaptureRequest()) {
        s->instance->config.burstFrames=ReadConfig().burstFrames;
        s->capturePending = true; s->captureAttempts = 0; s->captureFrames.clear();
        Log("capture_requested burst_frames=%u; copying before release and saving after burst", s->instance->config.burstFrames);
    }
    ArmD3D9Trace(s->capturePending, s->frames);
    // An unfinished burst retains only owned copies; discard release metadata.
    {
        std::lock_guard registryLock(registryMutex);
        for (auto& entry : swapchains) if (entry.second->session == s) {
            std::lock_guard imageLock(entry.second->mutex);
            entry.second->snapshot.reset();
        }
    }
    return r;
}

XrResult XRAPI_CALL GetProc(XrInstance instance, const char* name, PFN_xrVoidFunction* fn) {
    if (!name || !fn) return XR_ERROR_VALIDATION_FAILURE;
    *fn = nullptr;
    auto i = Lookup(instances, instance);
    if (!i) return XR_ERROR_HANDLE_INVALID;
#define HOOK(api, function) if (!std::strcmp(name, api)) { *fn = reinterpret_cast<PFN_xrVoidFunction>(&function); return XR_SUCCESS; }
    HOOK("xrGetInstanceProcAddr", GetProc)
    if (i->destroyInstance) { HOOK("xrDestroyInstance", DestroyInstance) }
    if (i->active) {
        HOOK("xrCreateSession", CreateSession)
        HOOK("xrDestroySession", DestroySession)
        HOOK("xrCreateSwapchain", CreateSwapchain)
        HOOK("xrDestroySwapchain", DestroySwapchain)
        HOOK("xrEnumerateSwapchainImages", EnumerateImages)
        HOOK("xrAcquireSwapchainImage", AcquireImage)
        HOOK("xrWaitSwapchainImage", WaitImage)
        HOOK("xrReleaseSwapchainImage", ReleaseImage)
        HOOK("xrWaitFrame", WaitFrame)
        HOOK("xrEndFrame", EndFrame)
    }
#undef HOOK
    return i->getProc(instance, name, fn);
}
XrResult XRAPI_CALL CreateInstance(const XrInstanceCreateInfo* info, const XrApiLayerCreateInfo* layer, XrInstance* instance) {
    if (!layer || !layer->nextInfo || !layer->nextInfo->nextCreateApiLayerInstance ||
        !layer->nextInfo->nextGetInstanceProcAddr || !instance) return XR_ERROR_INITIALIZATION_FAILED;
    auto next = *layer; next.nextInfo = layer->nextInfo->next;
    const auto r = layer->nextInfo->nextCreateApiLayerInstance(info, &next, instance);
    if (r != XR_SUCCESS) return r;
    auto i = std::make_shared<Instance>(); i->handle = *instance; i->getProc = layer->nextInfo->nextGetInstanceProcAddr;
    Resolve(*i, "xrDestroyInstance", i->destroyInstance);
    if (IsTarget()) {
        i->config = ReadConfig();
        i->active = i->config.enabled &&
            Resolve(*i, "xrCreateSession", i->createSession) && Resolve(*i, "xrDestroySession", i->destroySession) &&
            Resolve(*i, "xrCreateSwapchain", i->createSwapchain) && Resolve(*i, "xrDestroySwapchain", i->destroySwapchain) &&
            Resolve(*i, "xrEnumerateSwapchainImages", i->enumerateImages) && Resolve(*i, "xrAcquireSwapchainImage", i->acquireImage) &&
            Resolve(*i, "xrWaitSwapchainImage", i->waitImage) && Resolve(*i, "xrReleaseSwapchainImage", i->releaseImage) &&
            Resolve(*i, "xrWaitFrame", i->waitFrame) && Resolve(*i, "xrEndFrame", i->endFrame);
        Resolve(*i,"xrLocateViews",i->locateViews);
        if(i->active) InstallD3D9Trace();
        Log("TTWVRNeuralRendering 0.1.0 build=ttwvr-capture-1 active=%d mode=graphics capture=%d runtime_selection=unchanged",
            i->active, i->config.capture);
        PFN_xrGetInstanceProperties properties = nullptr;
        if (Resolve(*i, "xrGetInstanceProperties", properties)) {
            XrInstanceProperties p{XR_TYPE_INSTANCE_PROPERTIES};
            if (properties(*instance, &p) == XR_SUCCESS) Log("runtime=%s", p.runtimeName);
        }
    }
    { std::lock_guard lock(registryMutex); instances[*instance] = i; }
    return r;
}
}
}
extern "C" __declspec(dllexport) XrResult XRAPI_CALL xrNegotiateLoaderApiLayerInterface(
    const XrNegotiateLoaderInfo* loader, const char*, XrNegotiateApiLayerRequest* request) {
    if (!loader || !request || loader->structType != XR_LOADER_INTERFACE_STRUCT_LOADER_INFO ||
        loader->structVersion != XR_LOADER_INFO_STRUCT_VERSION || loader->structSize != sizeof(*loader) ||
        request->structType != XR_LOADER_INTERFACE_STRUCT_API_LAYER_REQUEST ||
        request->structVersion != XR_API_LAYER_INFO_STRUCT_VERSION || request->structSize != sizeof(*request) ||
        loader->minInterfaceVersion > XR_CURRENT_LOADER_API_LAYER_VERSION ||
        loader->maxInterfaceVersion < XR_CURRENT_LOADER_API_LAYER_VERSION) return XR_ERROR_INITIALIZATION_FAILED;
    const auto version = std::min<XrVersion>(XR_CURRENT_API_VERSION, loader->maxApiVersion);
    if (version < loader->minApiVersion) return XR_ERROR_INITIALIZATION_FAILED;
    request->layerInterfaceVersion = XR_CURRENT_LOADER_API_LAYER_VERSION;
    request->layerApiVersion = version;
    request->getInstanceProcAddr = ttwvrnr::GetProc;
    request->createApiLayerInstance = ttwvrnr::CreateInstance;
    return XR_SUCCESS;
}
