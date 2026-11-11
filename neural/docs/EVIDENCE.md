# Isolated validation, 2026-11-11

This records **spatial NR, temporal NR, native-resolution DLAA and combined
NR/DLAA prototypes** in an isolated simulated headset. It is not a physical
headset comfort/performance validation. All screenshots below were checked
after the game loaded Vault 101, beyond the main menu. Raw evidence is retained
locally rather than published with game assets or identifying save filenames.

## Scene discovery

- TTWVR 1.1.0 SHA-256:
  `8a3a71b48d362984fe346c73b68dfbd476d6e138d6831f26f10c6f506cd03880`.
- Verified borrowed D3D9 device getter at module RVA `0x4f5770`, accessing
  `module+0x19710e8`. SHA-256, relocated instruction bytes and owning window PID
  are checked before installing hooks. No on-disk TTWVR patch.
- A previous temporary-device discovery attempt replaced TTWVR's cached device
  pointer and crashed. Removed; no second D3D9 device is created in the game.
- Baseline OpenXR frames 2010/2011: both eyes 2944×2208 BGRA8 sRGB; 2454/2439
  D3D9 events, no overflow. No submitted OpenXR depth/motion extension.
- Scene-input frame 570: full-size RGBA16F HDR and D24S8 depth for each pass,
  quarter-size RGBA16F bloom and 1×1 adapted RGB luminance.
- Final tone-map PS bytecode FNV-1a hash `18030291181409727554`: bloom s0, HDR s1.
  Bound s2 is not sampled by that shader. Adapted luminance RGB sum travels in
  bloom alpha. `c1.x=1.3`; measured bloom alpha 0.828125/0.8271484375; effective
  exposure multiplier 1.0 for each eye. Sampler sRGB reads and writes were off.
- Pass-0 LDR mean byte error against XR left/right: 1.852/21.408. Pass-1:
  21.516/1.787. This and image inspection support pass 0 → left, pass 1 → right;
  they are not pixel-identical because later presentation processing remains.
- D24 capture values fit the low 24 bits, normalize by 16777215, and visually
  match scene geometry in both eyes. Observed depth ranges approximately
  0.87984–0.99604 and 0.88943–1.0; not reversed depth.
- Unused shader constants can contain NaN. The initial diagnostic wrote invalid
  JSON; fixed to serialize nonfinite values as null. Used exposure constants were
  finite. The reader normalizes only the old diagnostic's NaN literals.

## GPU transport and replay

- 32→64→32 D3D11 keyed shared HDR probe: 4096 pixels in each direction,
  zero mismatches on stock Proton. No cross-bitness shared fence imported.
- D3D9↔D3D11 HDR: 100 alternating round trips, 819200 pixels verified,
  zero mismatches.
- Raw INTZ depth export failed `OpenSharedResource` with `0x80070057` because
  DXVK does not publish compatible depth metadata. Replaced by GPU-only D24
  image→device-local buffer→RGBA8 bytes and shader decoding. Standalone packed
  depth probe passed 4096 pixels with zero mismatches. A later version verified
  four depth quadrants (0.1234567, 0.375, 0.9876543, 1.0) with zero mismatches.
- One standalone probe launch reported a DXVK submission timeout/device loss
  during device initialization. A fresh isolated-prefix run of the same binary
  passed all 100 HDR round trips and the four-quadrant depth check. Its cause is
  not established; initialization reliability remains an open test item.
- Native-size captured-HDR identity replay, both eyes: no nonfinite output,
  zero alpha changes, every value within two FP16 ULPs of its original. Small
  rounding changes are expected through encode/decode. Signed HDR is retained.
- Real NR captured-HDR replay, both eyes: `neural_evaluate output_applied=1`,
  approximately 19.3 million changed components per eye, no nonfinite values,
  zero alpha changes. NR preset B, intensity 0.5, native 2944×2208 model input.
  Motion zero and reset=true on every evaluation; no temporal quality claim.

## Live in-game output

- Identity-only control ran for thousands of frames; frames 2400/2401 captured
  both eyes. Identity logs use `conversion_evaluate`, not `neural_evaluate`.
- Actual NR host created independent features for eyes 0 and 1 and evaluated
  consecutive frames with `output_applied=1`. The scene hook then copied the
  processed HDR back before the game's original tone mapping.
- Actual NR OpenXR capture frames **630 and 631**, both eyes, confirmed
  `neural_rendering=true`, `mode=spatial_nr_reset_every_frame`, `dlaa=false`,
  rect `[0,0,2944,2208]`. Stereo previews show loaded Vault 101 gameplay.
- Native-resolution NR added cost. Median wall interval per application frame,
  measured over 120-frame log intervals: identity 26.82 ms (27 intervals), NR
  43.05 ms (30 intervals), roughly 37 vs 23 FPS in this simulated test. These
  include synchronization/CPU work and are not isolated neural GPU timings.
  Capture intervals can stall; these are diagnostic observations, not a controlled
  headset benchmark. Full NR handoff samples were about 5–6 ms per eye.
- The offline off/on comparison uses exactly the same scene input. Live control
  and NR runs share the save/location but NPC motion differs. Do not present them
  as identical-frame comparisons. Consecutive screenshots do not establish
  flicker/ghosting/comfort; those remain open.

## Temporal guide validation

- Reused the hash-verified TTWVR 1.1.0 object-motion recorder with its upscaler
  inactive. Captured RGBA16F vectors at 2944×2208: XY current-to-previous pixels,
  B validity, A zero. Observed moving character silhouettes match the scene.
- Verified camera view/projection/world matrices against the common scene shader's
  actual MVP constants (typical normalized error around 1e-7; threshold 2e-4).
  World rendering is camera-relative: the separate camera origin must be restored
  before reconstructing background motion. Applying only the rotation matrices
  produced incorrect translation and was rejected.
- Independent pixel reprojection for consecutive frames **1020/1021**:

  | Byte RGB MAE | Left | Right |
  |---|---:|---:|
  | No reprojection | 12.430 | 12.457 |
  | Verified reprojection | 1.051 | 1.164 |
  | Reversed direction | 17.528 | 17.613 |
  | Background only, verified | 1.018 | 1.132 |
  | Object-motion pixels, verified | 1.411 | 1.580 |

  Native object motion covered approximately 8.7%/7.7%; camera reprojection fills
  the remainder. This verifies the captured scene, not all game geometry/materials.
- Eight Halton phases, same phase for both eyes, with D3D9 integer pixel centers.
  Motion excludes jitter; `MVJittered` remains off. Projection uses per-eye state.
  Jitter-only reprojection confirmed the sampling/sign convention. The diagnostic
  images from that stage are not accepted final visuals, for the lighting issue below.
- Camera matrices are verified for each matching draw. Missing guides skip
  processing; gaps, pauses over 250 ms and large camera changes reset history.
  Both eyes logged consecutive processing with `reset=0` after warmup.

Fresh final motion-only capture **1080/1081**, with both effects and jitter off,
was rechecked in a unique run directory after the filename-collision fix.
Reprojection reduced full-image byte MAE from **9.030 to 1.214** (left) and
**8.809 to 1.192** (right); reversing direction worsened it to 12.973/12.633.
Background error was 1.174/1.160; object error 1.575/1.507. Native object-motion
coverage was 9.97%/9.30%, with 107/105 verified camera draws and relative MVP
error about 6.98e-8. The matching raw guides, images, report and preview remain
local and separate from all later launches.

## DLAA and temporal NR captures

- Early DLAA frames **1200/1201** processed but had bright/white surfaces. The
  jitter parser handled engine `ModelViewProj` matrices but missed TTWVR lighting
  shaders' `MVP` four-vector arrays. Matching depth and lighting passes therefore
  disagreed. Added the locally verified array layout, with register save/restore.
  Those early images are rejected as quality evidence.
- Corrected DLAA-only frames **1590/1591**: both eyes, native 2944×2208,
  `mode=dlaa`, NR false, DLAA true, and actual `dlaa_evaluate output_applied=1`.
  The loaded room and lighting look normal in both sampled eye images.
- Exact same-frame pre/post-feature HDR for frame 1590: finite output and identical
  alpha in both eyes. Mean RGB change 0.006050/0.006316. DLAA input preparation
  clamps negative RGB, unlike the signed-HDR NR identity path. This is disclosed
  rather than presented as an unchanged HDR range.
- Turning both effects off produced frames **6570/6571** with `capture_only`,
  both feature flags false and zero jitter. Motion recording now also stops in
  bypass unless the explicit diagnostic `native_motion_probe` was selected.
- Temporal NR-only frames **1740/1741**: both eyes, `mode=temporal_nr`, zero
  jitter, DLAA false, sustained `neural_evaluate reset=0 output_applied=1`.
  Exact-frame RGB changes 0.011786/0.011712; alpha unchanged and all values finite.
- Combined frames **5610/5611**: both evaluations and writes confirmed on both
  eyes. A nearby character intersects the simulated view; the same geometry is
  already visible in the captured pre-feature HDR. These frames demonstrate
  processing but are not a clean composition for comparing quality. A separate
  earlier-scene pair **1410/1411** was captured in the timed 100%-B run; visual
  inspection showed a clear view of the loaded room. Exact-frame RGB changes
  0.013086/0.012353, alpha unchanged, finite output.
- Model B at 50%: frames **1020/1021** (separate launch from the earlier motion
  diagnostic), both eyes with both features applied. Model A at 50%: frames
  **3000/3001**, also both eyes and both features applied. Creation logs confirm
  1472×1104 NR resources and 2944×2208 DLAA resources for both models.
- Frame-number-only scene filenames could collide between launches; the 50%-B
  capture exposed that issue. Captures now use a unique per-launch directory.
  The earlier motion-validation report and preview were preserved; subsequent
  guide verification uses the new run directories.
- Pre/post-feature comparisons are the exact same HDR frame. Their previews use
  a labelled HDR display proxy, not the game's tone mapping. OpenXR previews use
  the final in-game eye image. Both outputs are retained locally with metadata.
- Two-frame still captures do not establish absence of flicker, ghosting, stereo
  disagreement during movement, or HUD/text distortion. Longer sequences and
  physical-headset assessment remain necessary.

## Performance and controls

Initial whole-application median intervals were about 42.97 ms with DLAA alone
(41 intervals) and 46.43 ms with temporal NR alone (21 intervals). They include
guide rendering, CPU work and synchronous cross-process handoffs; they do not
isolate the NGX evaluation. Captures stall and are excluded from performance data.

Optional D3D12 timestamp queries now surround the actual NGX calls. Results are
read only after the existing host GPU completion wait; no extra wait or CPU image
transfer is added. These measurements exclude conversions, guide rendering, GPU
copies, synchronization and the base game. All tests use the same saved room with
moving NPCs and a moving simulated headset, not an identical-frame benchmark.

RTX 5090, warmed samples after frame 900, excluding captures and the next 120
frames for GPU timings. Application samples are adjacent 120-frame intervals
without captures. Both effects were active; DLAA stayed at full resolution:

| NR model/grid | DLAA GPU ms/eye | NR GPU ms/eye | GPU samples | App ms/frame | App intervals |
|---|---:|---:|---:|---:|---:|
| B / 100% | 0.719 | 3.449 | 66 | 61.49 | 7 |
| B / 50% | 0.718 | 1.531 | 106 | 49.26 | 9 |
| A / 50% | 0.717 | 2.731 | 200 | 57.96 | 22 |

NR is the more expensive NGX stage in these measurements. Model A was slower
than B at 50%; no quality preference is inferred from speed. Whole-frame costs
are much larger than the sum of the two eyes' NGX times, so blaming either neural
feature alone for the full FPS reduction would be incorrect. These are simulated
runtime observations, not predictions of WiVRn headset FPS.

The NR controls mirror the Titanfall addon: models A/B, model resolution, strength,
tone and detail. At 50%, color/depth/motion on the NR grid are 1472×1104; the game,
DLAA and final output remain 2944×2208. Motion magnitude is scaled to model pixels.
There is no independent depth-only downscale. The model result is converted to
bounded gain and applied to the original full-resolution HDR; it is not DLSS
Super Resolution. The defaults remain B/100% with both effects off.

## Handoff investigation

- Sampled camera verification was roughly 0.1 ms per eye and native motion draw
  submission roughly 0.5 ms per eye; these are CPU times, not the GPU time of
  motion rendering. Camera sampling was kept intact rather than weakened for a
  negligible saving.
- In the 50%-B combined run, median sampled depth transfer/completion was about
  0.838 ms per eye and helper wait 5.041 ms per eye (27 samples). The latter includes NGX,
  conversions, transfers and queue synchronization. It cannot be described as
  pure inter-process overhead.
- An additional D3D9 event-query flush/wait after the depth transfer cost about
  0.090 ms per eye in that run. Removed it: the depth transfer already flushes the D3D9 command
  and submission threads, queues its Vulkan copy on the same rendering queue,
  and waits for that copy's fence. Thus earlier color, motion and bloom copies
  have also completed before the helper receives its request.
- Extended the independent GPU transport probe with 32 alternating color/depth
  sequences, verifying 262144 pixels with **zero mismatches**, without another
  D3D9 wait. Its existing 100 HDR round trips also passed: 819200 pixels, zero
  mismatches. The retained Vulkan completion wait is bounded and still required.
- The revised live path processed combined model-B/50% NR and native DLAA
  continuously with `reset=0` on both eyes. Captures **1620/1621**, in a unique
  run directory, confirm both outputs applied at 2944×2208. Same-frame HDR alpha
  is unchanged, output finite; RGB changes 0.021307/0.021194. Stereo preview was
  visually checked in the loaded room.
- Live all-effects-off then produced captures **2700/2701** in the same run:
  `capture_only`, both effect flags false, jitter `[0,0]` for both eyes. The idle
  helper retained resources but issued no further neural evaluations in that
  earlier build. The live-Apply build below now shuts it down.
- The removed query is a small cleanup, not a solution to the large application
  frame-time increase. Synchronous input/output handoffs still prevent the game
  thread from overlapping work. The failed cross-bitness Wine shared-fence
  experiment remains disabled; the helper's internal D3D11/D3D12 fences remain.
- An early CPU profiling attempt converted absolute uptime to floating point
  under the game's x87 single-precision mode and quantized samples to 32 ms.
  Rejected those samples; the corrected profiler subtracts integer time points
  before converting the interval. GPU timestamp results were unaffected.

## Preservation and limits

The test used copied game executables, MO2, prefix, profile and saves, with the
original game/MO2/prefix and Titanfall paths mounted read-only. All eight original
save files matched their preparation hashes afterward. The isolated game was
closed after final validation, with NR, DLAA and diagnostic probes disabled. WiVRn's default
registration and the working full-resolution Steam path were unchanged.

Only the loaded Vault 101 scene, this TTWVR binary and this resolution were
validated. Native settings-menu navigation and live feature changes are now
covered below; wider scene transitions, device recreation, resizing and mixed-eye
failure recovery still require additional work. Shader identities are verified
locally; raw proprietary shader code is not distributed. Feature combinations
and tuning apply live; installing new binaries/scripts requires restart.
All effects default off.

## Native menu and live Apply (2026-11-11)

The menu uses original XML and xNVSE/JIP scripts with native selection dialogs.
F10/Escape bridge missing keyboard actions; Fallout handles arrows/Enter itself.
The user confirmed menu operation and then confirmed the double-step arrow fix.
Native controller injection through a private JIP test harness verified pause →
X opens the dialog, A changes a draft, three D-pad steps reach Apply, and B returns
from tuning or cancels. The input reader logged its verified JIP 57.30 wrapper and
left the controller state unchanged. All eight production UDFs precompiled.

In the 21:57:48 layer-log run (Wine game PID 552), menu sequence 7/revision 4
started NR and sequence 8/revision 5 replaced it with NR+DLAA without restarting
Fallout. The old helper exited before the replacement started. Both eyes logged
`output_applied=1` on consecutive frames: NR **16378–16380**, then both effects
**18880–18882**, with `reset=0` after the first frame. Captured NR frames
**16710/16711** have matching stereo metadata at **2944×2208 per eye**. Same-frame
HDR input/output was finite with unchanged alpha; mean absolute RGB changes were
0.008669/0.008102. The stereo gameplay preview was visually inspected.

These menu tests used model B, NR scale 25%, strength 20%, tone/detail 100%, as
selected during the interactive test. That scales the NR guide grid together to
736×552; game rendering and DLAA remain full resolution. It is not a new quality
recommendation or performance benchmark. The public defaults remain B/100%,
strength 50%, with both effects off.

The final build was also tested in one restarted isolated game (Wine PID 548,
22:08:36 layer-log run). Live menu/CLI changes produced these stereo captures:

| Mode | Consecutive frames | HDR RGB mean absolute change, left/right |
|---|---|---|
| NR + DLAA | 660/661 | 0.012466 / 0.012137 |
| DLAA alone | 7020/7021 | 0.006106 / 0.006507 |
| Both off | 3630/3631 | Not processed; both effect flags false |

Active captures had finite HDR output, unchanged alpha and both-eye applied
metadata. Stereo gameplay previews were inspected. An earlier DLAA capture at
3090/3091 passed processing checks but was obstructed by an NPC intersecting the
simulated camera; it was not accepted as a quality preview. Reloading the copied
save produced the unobstructed 7020/7021 pair. The off captures recorded `[0,0]`
projection jitter for each eye and the helper exited. Head tracking was unchanged.
These are scene checks, not identical-frame comparisons between separate runs.

Temporarily withholding the stopped private helper caused revision 9 to report
launch failure and retain the original HDR. Restoring it and explicitly retrying
created revision 10, with successful NR on both eyes at frames 4981–4983 without
restarting Fallout. A deliberately stale menu request was rejected without
changing a byte of the INI; a current all-off request then succeeded. These tests
do not cover an evaluation failure after only one eye has already been written.

Both Windows targets built, and all four native tests passed, including reload
coalescing, failure/retry and OpenXR ownership/capture lifecycle checks. Original
save hashes were rechecked: all eight unchanged. Test automation and capture
probes were disabled afterward; the isolated game was closed with both effects
off and the menu installed. No binaries were replaced while loaded. Source and
documentation are published; game captures, saves and NVIDIA dependencies remain
local. WiVRn streaming, physical-headset latency and longer-motion visual comfort
were not validated by these simulated-headset checks.
