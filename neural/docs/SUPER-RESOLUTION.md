# Experimental DLSS Super Resolution — 2026-11-12

DLSS Quality now evaluates both eyes in the loaded TTWVR test scene, with actual
**1963×1472 scene rendering → 2944×2208 output per eye**. This is a separate
implementation from TTWVR's built-in upscaler, which remains unavailable in the
tested Linux setup. Frame generation is absent. NR and native DLAA are disabled
while SR is selected in this first implementation.

**This is not yet a performance recommendation.** The first simulated-headset
measurements showed a substantial application-frame-time regression despite
about 0.69 ms of NGX work per eye. CPU/GPU handoff waits and engine overhead must
be considered alongside the neural kernel time. A loaded DLL, successful NGX
call, or low GPU kernel time does not establish an FPS gain.

## Rendering contract

The existing hash-verified TTWVR 1.1.0 adapter identifies the scene's RGBA16F HDR
texture at the verified tone-map draw. It redirects subsequent world rendering
to a private lower-resolution HDR target, with matching D24S8 depth, viewport,
and object-motion grid. Bloom/adaptation continue at their original sizes.
The original full-size resources remain allocated. Final tone mapping, HUD and
OpenXR swapchains keep their original 2944×2208 dimensions, poses and FOVs.

The x64 helper queries NGX's optimal-settings callback for the native output
size and MaxQuality. It returned **1963×1472**; an initial 1962-pixel probe was
rejected and native rendering remained active. No estimated resolution is
silently accepted. The supported first path is intentionally fixed to this
verified output size and game/mod version.

The helper creates separate per-eye SuperSampling features, Quality preset K,
with lower-resolution HDR/depth/motion inputs and native-resolution outputs.
Motion stays in input-pixel units and excludes jitter. Eighteen shared-eye
Halton phases provide scene jitter on the smaller grid; camera validation and
per-eye histories reset after a size change. Alpha is bilinearly reconstructed
from the original input rather than taken from the NGX output.

After successful completion, only the tone-map draw samples the reconstructed
HDR texture. The prior texture binding is restored immediately afterward.
The normal path uses GPU textures; CPU readback is restricted to explicit
captures. Protocol version 4 carries a distinct output handle and dimensions.
Deploy the x86 layer and x64 helper together, only after the game exits.

## Live control and bypass

Use **Neural Rendering → DLSS Quality → Apply** in the addon menu, or:

```sh
python3 neural/tools/control.py --config /path/to/TTWVRNeuralRendering.ini sr on
python3 neural/tools/control.py --config /path/to/TTWVRNeuralRendering.ini sr off
python3 neural/tools/control.py --config /path/to/TTWVRNeuralRendering.ini all-off
```

SR starts off in the distributed configuration. A switch waits for the old
helper to exit before replacing shared transport images. Lower scene rendering
starts only at a stereo frame boundary after SR initialization succeeds.
Turning SR off or failing initialization restores native scene rendering.
If an evaluation fails after a smaller scene has already been rendered, that
frame can fall back to ordinary sampling of the smaller image; subsequent
frames return to native rendering. This does not promise perfect same-frame
recovery from a mid-stereo failure.

`super_resolution.capture_probe=1` is a startup-only diagnostic that renders
the smaller scene without NGX. It is not SR and must remain zero for normal
use. The addon menu setting is distinct from TTWVR's built-in DLSS/FSR menu.
TTWVR's Sharpness setting is passed to its own upscaler; this addon does not
consume it and adds no separate sharpening pass.

## Capture evidence

Private isolated run `134389122576671542`, frames **810/811**:

- Both eyes: `mode=super_resolution`, input HDR **1963×1472**, processed HDR and
  final OpenXR image **2944×2208**. NGX logged `sr_evaluate output_applied=1`
  for both eyes on consecutive frames with temporal history continuing.
- All captured feature inputs/outputs were finite. The feature output differed
  from a bilinear input enlargement (mean absolute HDR RGB difference
  0.005172/0.005616). This establishes processing, not a quality score.
- Alpha was compared against bilinear input reconstruction. Maximum error was
  0.004916/0.004727, within the two-axis 8-bit filtering-weight bound plus
  half-float rounding. Unlike same-size DLAA, SR alpha is resampled and is not
  bit-identical to the input.
- Low-resolution camera matrices were independently verified against actual
  scene draws: 104/102 verified draws, zero rejected camera fits.
- Consecutive low-grid HDR proxy reprojection reduced mean pixel error from
  5.750/5.709 to **0.792/0.823**; reversing motion made it worse at 7.711/7.645.
  This checks direction, pixel scale, depth and eye matching. It does not validate
  all disocclusions or all game materials.
- Stereo gameplay previews were inspected. No gross eye disagreement or black
  output was visible in these captures. Moving-scene flicker, fine ghosting,
  HUD/text at other settings and physical-headset comfort require further tests.

The capture-first 1962-pixel probe separately showed over 2,200 indexed world
draws per stereo frame at the smaller size, with tone-map/output draws still native.
The SR trace for frame 810 confirms **2,266** indexed world draws at
**1963×1472**, while its tone-map/output target stays **2944×2208**. Captures include the
loaded scene after the menu, not merely a successful menu screenshot.

## Performance and validation scope

The test uses an isolated game, MO2 instance and Proton prefix with the reusable
Monado simulated headset. It does not measure WiVRn streaming or headset latency.
The Titanfall installation and WiVRn's runtime registration remain untouched.
Normal Steam runtime binaries have not been replaced by this experiment.

The initial polling handoff showed roughly 43 ms application intervals versus
roughly 23 ms during native bypass in the same test session. These are exploratory
samples, not a controlled headset benchmark; menu/transition intervals and
capture stalls must be excluded from a formal comparison. GPU utilization was
about 27% on the RTX 5090, so spare VRAM or shader capacity does not resolve the
observed waits by itself.

A reusable same-process D3D11 completion fence replaces per-eye event-query
allocation/polling. It preserves completion ordering and avoids Wine's broken
cross-process D3D12-fence import. Early typical helper handoff fell to about
2.6 ms per eye, but intermittent >12 ms waits remained and overall frame time
did not materially improve. A further private-fence/CPU-wait experiment
also failed to improve application frame time and was removed. Sampled `host_gpu_span`, `host_completed`,
`handoff_timing`, `scene_jitter_cpu` and `neural_gpu_timing sr_ms` separate those
costs instead of labelling the whole delay as DLSS evaluation time.

Source builds and native tests cover the existing OpenXR ownership/history
rules and SR/NR/DLAA helper-reload state transitions. Runtime scene validation
is still specific to the captured TTWVR 1.1.0 scene and fixed output dimensions.
Do not deploy this as a general game-resolution or device-reset solution.

NVIDIA SDK/runtime DLLs, game assets, captured shaders and saves are not included.
The addon remains GPL-3.0-only; NVIDIA dependencies retain NVIDIA's terms.

## Live-resize regression checks

Run `134389132851769997` completed SR → DLAA → off → NR → SR → off without
restarting the game. Captured consecutive stereo frames were SR 3030/3031,
DLAA 3240/3241, native bypass 3390/3391, NR 3660/3661, and SR 3870/3871.
This exercised both transport sizes and restoration of native rendering.
The final shared-fence build also started with all effects and motion probes
off, then enabled SR live and captured both eyes on frames 3712/3713 in run
`134389136594451175`.

The final build subsequently applied both eyes after live changes at DLAA
frame 12360, NR frame 12720 and SR frame 13200, then returned to native bypass.
