# Proton neural rendering investigation — 12 November 2026

> This records the earlier GPU-handoff experiment. The later
> [SR/FG integration and 120 Hz measurements](sr-frame-generation.md) supersede
> its statements about absent SR/FG integration. Current layer/helper protocol
> is 6; protocol 4 below identifies the earlier tested build.


This investigation uses TTWVR 1.1.3 and the Fluorine-compatible rendering addon.
The private automated harness still uses an archived MO2 instance; it does not
change the production Fluorine instance, Titanfall, or WiVRn registration.

## Findings

The current 32-bit game passes GPU images to a separate 64-bit NGX helper.
Previously it blocked the game thread until the input copy completed, then
blocked again until the helper completed its output. This serializes work that
could otherwise overlap. It explains why free VRAM or low aggregate GPU use
alone did not translate into a faster frame.

An experimental handoff replaces those CPU completion waits with two monotonic
Vulkan timeline semaphores, exported as **OPAQUE_WIN32**, not D3D12 fences.
The CPU events still publish one immutable request at a time. The helper queues
an input dependency, evaluates the selected features, and queues an output
signal. It acknowledges submission before the game queues its output wait and
copies the processed HDR image. The original CPU path remains the default.

No rendering dimensions, model settings, motion-vector conventions, exposure,
or jitter are changed. NR still runs before the game's tone mapper and retains
separate temporal state for each eye. DLAA remains a distinct feature.

The opaque mechanism passed a standalone 32-to-64-bit probe with 32 GPU-signalled
round trips on the installed, unmodified Proton. It avoids the imported
cross-process metadata involved in the [earlier D3D12 fence crash](../../../docs/fence-experiment.md).
It does not fix arbitrary D3D12 fence imports or enable the stock TTWVR upscaler.

The texture stress test also exposed a pre-existing depth issue: a full-image
clear could remain deferred in DXVK while the raw Vulkan copy read zero depth.
The CPU-fenced control failed too. The copy now asks DXVK to transition the
depth resource and restore its tracked layout before issuing external commands.
With this change, 32 repeated HDR/depth round trips verified **393,216 pixels
with zero mismatches**, reading them back only after all submissions. This is
not evidence that the reported in-headset DLAA haze is fixed.

## Measurement method

RTX 5090, NVIDIA 615.78.08, Ryzen 9 5950X guest, Proton Experimental 11,
TTWVR 1.1.3, existing 32 MiB DXVK pool patch. Both eyes remain **2944×2208**.
MSAA is off. NR is at 100% processing dimensions, model B unless specified,
100% strength/tone/detail. DLAA uses the existing K preset.

Tests load the same copied corridor save, freeze the simulated head pose in the
isolated process, request no image captures while timing, and warm each live
configuration before recording approximately 25 seconds. FPS is derived from
OpenXR frame counters and log timestamps. Reported frame times are averages
over logging windows, **not per-frame latency percentiles**. NPCs and background
system load are not frozen. Baseline/off repeats help expose drift. Matching
means the same save and runtime pose/FOV, not a pixel-identical deterministic
replay across launches.

Initial simulated poses intersected the avatar, including with effects off.
For the final table and visual checks, move the stationary simulated HMD center
to `(0, 1.6, -0.5)` **after loading** the save. This clears the view without
changing renderer dimensions. Both runs use the same pose. Full Body was requested
off only in the private INI; that setting alone did not remove the intersection.
The original private INI is restored after testing. Production body/calibration
settings are unchanged.

The Monado null compositor runs at 90 Hz and has adaptive pacing. These numbers
are not predictions of Quest/WiVRn FPS, streaming latency, dropped frames or
comfort. Application-span timings include CPU work and waits performed inside
the application; NGX GPU timings cover evaluation only, excluding input/output
conversion and transport.

## Final matched results

These runs use the **same final layer/helper build**, switching only
`gpu_handoff` from 0 to 1 between launches. The CPU control already includes the
completion-query and deferred-depth-clear fixes. They isolate the handoff change,
rather than combining it with a different model, reduced resolution or a DLL swap.

| Mode | CPU handoff FPS | GPU handoff FPS | Mean frame interval, CPU → GPU |
|---|---:|---:|---:|
| Effects off, start | 39.71 | 38.71 | 25.18 → 25.83 ms |
| DLAA | 35.77 | 38.65 | 27.96 → 25.87 ms |
| NR model B | 23.52 | 38.61 | 42.52 → 25.90 ms |
| NR + DLAA | 16.02 | 39.19 | 62.41 → 25.52 ms |
| Effects off, end | 39.06 | 39.28 | 25.60 → 25.46 ms |

NR improved about **64%** and
combined processing about **145%**;
DLAA alone improved about **8%**.
These percentages describe this simulated scene, not guaranteed headset gains.
Small differences among the fastest modes are within the observed run drift.

Combined processing's median logged application CPU span fell from
46.40 to 16.32 ms.
Its median helper handoff duration fell from
6.58 to 1.16 ms per eye;
the new duration ends at **submission acknowledgment**, not GPU completion.
Median GPU utilization increased from
33% to 77%,
consistent with overlapping work. Sampled evaluation times remained roughly
0.72 ms/eye for DLAA and 3.2–3.6 ms/eye for model-B NR. The main gain is
scheduling, not a cheaper model. Mode changes completed without helper or bypass
failures. An earlier transport-only control reached 38.66 FPS.

[Aggregate samples, CPU spans, GPU medians, capture metadata and tested binary hashes](evidence/proton-neural-20261112.json)
are public. Full raw logs and captures remain in the private research workspace.
Image captures are run after timing and are excluded from these measurements.

## Completed image checks

- GPU handoff, off: frames **29520/29521**, both eyes.
- GPU handoff, nr: frames **30930/30931**, both eyes.
- GPU handoff, dlaa: frames **33750/33751**, both eyes.
- GPU handoff, both: frames **36750/36751**, both eyes.

The NR, DLAA and combined captures contain finite same-frame input/output HDR,
unchanged alpha and nonzero RGB changes for each eye on both consecutive frames.
Logs contain the appropriate `neural_evaluate`/`dlaa_evaluate` with
`output_applied=1`. A separate identity-transport capture produced only about
0.000008 mean absolute HDR RGB error, consistent with its conversion round trip.
Stereo previews and CPU-control references were inspected at matching poses and
FOVs. The visible scene has no avatar obstruction at the final test pose.

These checks establish actual output and matching stereo metadata. Stationary
pairs do not validate moving-head flicker, ghosting, HUD/text behavior, WiVRn
latency or comfort. The distant DLAA haze remains a headset validation item.

## Research checked against the installed implementation

- NVIDIA's current Remix dependency manifest still selects NR dependency 2.
  The installed `nvngx_dlssnr.dll` matches the reference dependency used by this
  addon. NVIDIA's October announcement describes a faster NR model, but there
  is no evidence here that swapping a stale DLL would recover the measured loss.
  [NVIDIA announcement](https://www.nvidia.com/en-au/geforce/news/dlss-5-faster-ai-model-hogwarts-legacy-rtx-spark-laptops/),
  [Remix dependency manifest](https://github.com/NVIDIAGameWorks/dxvk-remix/blob/main/packman-external.xml).
- Current Valve Wine source still contains the problematic D3D12-fence import
  handling examined in our earlier reproduction. This was checked directly;
  an unrelated Proton upgrade is not established as a fix.
  [Valve source](https://github.com/ValveSoftware/wine/blob/bleeding-edge/dlls/win32u/vulkan.c).
- Independent Linux NR projects report the same broad 32-bit/64-bit fence
  obstacle and successful NR on other paths. Their resolution, optical-flow,
  architecture-spoofing and presentation assumptions are not copied into TTWVR.
  [Cross-process investigation](https://github.com/kamalzakaria/dlss5-on-linux),
  [RTX 5090 experiments](https://github.com/pantsoftime/dlss5-linux).
- DXVK recommends limiting explicit flushes. The helper candidate also reuses
  its completion query, polls with `DONOTFLUSH` after its explicit flush, and
  batches the D3D11 output wait with the following resolve work. GPU ordering is
  retained; waits are never skipped by pretending unfinished work completed.
  [DXVK developer guidelines](https://github.com/doitsujin/dxvk/wiki/Developer-guidelines).

## Frame generation

A private copy of the Titanfall reference's standalone FG backend probe ran in
the TTWVR test prefix at 2944×2208. Both synthetic eyes produced interpolated
outputs across consecutive frames; the probe checked the expected moving-object
centroid and reported `fg_backend_probe_passed=1`. Its roughly 11.8 ms median
per-eye measurement includes conversions and CPU readback, so it is **not** a
pure FG GPU-time measurement or an application performance prediction.

This proves that the current Proton/NVIDIA stack can evaluate that FG backend.
It does not integrate FG into TTWVR. A correct port must capture final stereo
color before OpenXR image release, pair it with matching eye/frame depth and
motion, retain owned images, and submit generated/real frames with correct
OpenXR ownership and pacing. The earlier Titanfall gameplay experiment felt
less smooth, so DLL loading or doubled desktop FPS is insufficient acceptance.

NVIDIA documents Smooth Motion for Linux, but its presentation injection does
not establish generated stereo delivery to TTWVR's OpenXR swapchains. WiVRn's
current setting labelled Application SpaceWarp changes the FPS divider; the
label alone is not evidence that TTWVR submits per-eye depth and motion to the
headset. This addon currently submits neither OpenXR frame-synthesis metadata
nor generated VR frames. FG remains disabled.
[NVIDIA Linux guide](https://docs.nvidia.com/datacenter/tesla/driver-installation-guide/gaming.html),
[WiVRn setting implementation](https://github.com/WiVRn/WiVRn/blob/master/client/scenes/gui_settings.cpp),
[Meta's required depth/motion submission](https://developers.meta.com/vr/documentation/native/android/mobile-asw/).

## Rejected or limited experiments

A helper-local D3D11 fence/event replacing query polling did not establish an
FPS gain, and was dropped. Forcing the simulated runtime to return immediately
from `xrWaitFrame` moved substantial waiting into `xrEndFrame` and gave inconsistent
results, including slower effects-off runs. No such pacing override is retained
or recommended for WiVRn. The older reduced-resolution and monotonic-D3D12-fence
binary experiments remain disabled.

DLAA's reported distant haze/softness still needs headset assessment. This
performance work does not establish a visual fix for it, and it does not add
Super Resolution upscaling.

## Enabling and rollback

Build and install the matching x86 `TTWVRNeuralRenderingLayer.dll` and x64
`neural_host.exe` while the game and helper are closed. Preserve the existing
runtime INI, NVIDIA DLLs and feature preferences. This update uses protocol 4;
a new layer with an old helper is rejected and bypasses processing.

In the runtime's `TTWVRNeuralRendering.ini`:

```ini
[diagnostics]
gpu_handoff=1
```

Restart. Expect `gpu_handoff_ready mode=opaque_timeline` and
`gpu_handoff_host_ready mode=opaque_timeline`, followed by per-eye
`neural_evaluate`/`dlaa_evaluate` with `output_applied=1` for enabled features.
`host_submitted ... gpu_completion=queued` records submission, not GPU completion.
A stereo capture or the texture probe verifies actual completed output.
Initialization failure bypasses processing; a parent-side export failure falls
back to the CPU handoff and reports it. Setting `gpu_handoff=0` and restarting
restores the CPU handoff. Distributed defaults remain zero pending headset
assessment. The Steam command and native Fluorine setup stay the same.

`frame_timing=1` logs average OpenXR CPU spans without changing pacing.
`gpu_timing=1` samples NGX GPU execution without waiting for unfinished queries.
These are diagnostic settings; both default off. No driver, global Proton,
WiVRn refresh setting or runtime registration is changed by this patch.

## Reproducing the transport checks

Build both Windows architectures using [the addon instructions](../neural/README.md#build).
In an isolated Proton prefix, launch the x86 `timeline_texture_probe.exe` with
one argument: the absolute Windows path to its x64 counterpart. Expected final
lines from both processes contain `PASS`, `frames=32`, `mismatches=0` and
`deferred_readback=1`. The separate `tests/opaque_timeline_probe.cpp` uses only
Vulkan and the Win32 handle/event APIs; compile it for x86/x64 with Vulkan-Headers
and run its x86 executable with the x64 executable path. It verifies 32 alternating
GPU-signalled round trips ending at timeline value 64.

The actual texture probe uses DXVK D3D9 and D3D11 devices, the production depth
copy, alternating HDR/depth inputs, and a helper-written output. All staging
readback is deferred until the 32 exchanges finish. Thus readback cannot supply
the per-frame synchronization being tested. Raw captures, game shaders/saves
and NVIDIA runtime DLLs remain private and are not redistributed here.
