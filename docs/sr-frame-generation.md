# DLSS SR and stereo frame generation — 12 November 2026

The addon now implements **DLSS Quality** and **experimental stereo frame generation**
as separate live controls in TTWVR's native VR Settings. Both default off.
This is a working integration with measured costs, not a claim that DLSS fixes
TTWVR's performance. SR alone was slower than native in the tested corridor.

[Implementation and installation](../neural/docs/SR-FRAME-GENERATION.md) ·
[Machine-readable evidence](evidence/sr-frame-generation-20261112.json)

## Matched 120 Hz measurement

TTWVR 1.1.3, Proton Experimental 11, RTX 5090, Ryzen 9 5950X, NVIDIA 615.78.08.
The isolated copied game loaded the same corridor save with a fixed simulated
head pose. The Monado null compositor ran at 120 Hz; the normal WiVRn runtime
and Titanfall were unchanged. Each mode had five seconds to settle followed by
30 seconds of measurement. Captures, builds and GPU probes were excluded from
these timing windows. NR used model B at 100% scale and strength.

| Mode | App FPS | Generated pairs/s | Repeated pairs/s | Runtime submits/s | GPU use | Power |
|---|---:|---:|---:|---:|---:|---:|
| Off | 58.0 | — | — | — | 51.8% | 104.6 W |
| DLAA | 48.5 | — | — | — | 58.8% | 153.5 W |
| SR Quality | 47.9 | — | — | — | 52.3% | 141.3 W |
| FG | 35.5 | 35.4 | 4.3 | 75.2 | 50.1% | 136.4 W |
| SR + FG | 37.7 | 37.7 | 16.6 | 92.0 | 57.3% | 164.9 W |
| DLAA + FG | 33.0 | 33.0 | 4.1 | 70.2 | 52.6% | 162.3 W |
| NR + DLAA | 33.7 | — | — | — | 67.8% | 275.5 W |
| NR + DLAA + FG | 23.9 | 24.0 | 0.6 | 48.6 | 56.1% | 246.1 W |
| Off (repeat) | 57.7 | — | — | — | 50.9% | 109.2 W |

FG columns count **stereo pairs**, not individual eyes. For SR + FG, about
37.7 real and 37.7 generated pairs/s were distinct presentations; about 16.6
more submissions/s repeated the last real pair. Calling that “92 generated FPS”
would be wrong. Interpolation adds buffering and cannot improve the underlying
simulation's responsiveness. A higher submission count is not proof of better
VR comfort.

The off run returned to 57.7 FPS after cycling all modes, compared with 58.0
before. Monado's adaptive prediction gave the ordinary app roughly a 16.67 ms
period at 120 Hz. Earlier 90 Hz runs were around 39–40 app FPS. Those observations
show why refresh and app FPS cannot be treated as interchangeable. They do not
establish a universal fixed half-refresh cap.

SR used about 141 W and 52% global GPU utilization versus DLAA's 154 W and 59%,
at similar app rates. Effects-off used about 105 W and 52% while producing more
real frames. SR therefore saved work relative to native DLAA here, but **did not
improve performance over native rendering with effects off**. These one-second
GPU samples include the desktop and compositor and are not process-specific
frame-time measurements. Free VRAM is not evidence that scheduling and resource
handoffs have no cost; original full-size scene allocations also remain present.

Median per-eye NGX GPU evaluation was approximately **0.685 ms SR**, **0.719 ms
DLAA**, **2.15 ms FG** and **3.55 ms NR** in their corresponding measured modes.
Preparation, transfers and presentation add cost beyond these timestamps. The
ordinary app CPU span grew from about 10.85 ms off to 15.19 ms with SR. With
SR + FG it was about 18.88 ms plus 6.47 ms waiting and 0.30 ms in the intercepted
end call. These wall-clock spans contain GPU dependencies; they do not identify
a pure CPU or GPU bottleneck. More overlap and presentation work remain to be
optimized.

## Actual output checks

- SR captures show 1963×1472 HDR/depth/motion inputs and 2944×2208 final output
  for both eyes on consecutive frames. Logs include `sr_evaluate` with
  `output_applied=1`. Same-frame HDR output is finite, changes RGB, and preserves
  bilinearly resampled alpha within the comparison's filtering/half-float bound.
- The FG probe runs moving foreground geometry through both eyes at native and
  SR guide sizes. Generated object positions match the expected halfway point;
  this checks TTWVR's motion direction and scaling instead of assuming Titanfall's.
- In-game captures retain original, real and generated stereo images. Converted
  real RGB is pixel-identical to the original final eye. Generated-pair
  presentation is checked for each captured source frame, not inferred from a
  loaded DLL, enabled setting or aggregate counter.
- Preview images were inspected side by side. The fixed-camera scene does not
  validate moving-head ghosting, flicker, HUD behavior during action or comfort.
  Raw game captures remain private; the repository records metadata and metrics.

Native tests cover image ownership, camera/projection conventions, live settings,
OpenXR lifecycle, queued stereo delivery, copied poses, adaptive cadence,
menu bypass, failure fallback and shutdown. ThreadSanitizer also exercises the
presentation worker. The layer enables D3D11 context protection while the worker
runs and restores the previous setting after a bounded GPU drain when it stops.
An active-FG quit initially stalled in Monado Vulkan teardown. Explicit GPU-copy
and helper shutdown before downstream teardown fixed direct quit. Resuming
from menus also required releasing owned presentation swapchains on worker stop;
each new worker now gets fresh images. The reproduced SR+FG menu/Back/resume,
second pause/resume and quit then passed. Normal rendering has no added CPU
completion wait.

## Use and rollback

Install the matching protocol-6 layer and helper while the game is closed.
Preserve your runtime INI; add the separately obtained `nvngx_dlssg.dll` only
if using FG. The existing `nvngx_dlss.dll` supplies DLAA and SR. Keep the Steam
command and Fluorine profile. NVIDIA dependencies are not distributed here.

Select **DLSS Super Resolution → Quality** and optionally **Frame gen. →
2x (experimental)**. SR excludes NR/DLAA; FG can run independently. Active MSAA
excludes all neural processing and requires a restart to disable. Off switches
restore native rendering and ordinary runtime pacing; initialization or invalid
input failures retain original-image bypass.

Only the verified 2944×2208 output/Quality adapter is currently supported. The
stock TTWVR upscaler remains disabled. Keep both new features off until you want
to test them; there is no recommendation to replace a working headset setup
with these experimental modes. Neither this work nor the earlier GPU handoff
establishes a fix for the reported distant DLAA haze.
