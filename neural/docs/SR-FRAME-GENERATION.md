# DLSS Super Resolution and stereo frame generation

The rendering addon provides **DLSS Super Resolution → Quality** and
**Frame gen. → 2x (experimental)** inside TTWVR's native **VR Settings**.
They are separate features and can be switched live. Both default off.

This implementation has been exercised in the isolated TTWVR 1.1.3 installation
under Proton Experimental 11 with an RTX 5090. Headset streaming, latency,
moving-head comfort and behavior in other scenes still need physical testing.
Do not equate the compositor submission rate with the number of distinct images.

## Features and dependencies

| Feature | NVIDIA runtime | Rendering behavior |
|---|---|---|
| NR | `nvngx_dlssnr.dll` | Neural rendering before tone mapping |
| DLAA | `nvngx_dlss.dll` | Native-resolution temporal antialiasing |
| DLSS SR Quality | `nvngx_dlss.dll` | 1963×1472 scene reconstructed to 2944×2208 per eye |
| Stereo FG | `nvngx_dlssg.dll` | An interpolated stereo pair between consecutive real pairs |

The current SR adapter supports the verified **2944×2208 output**, Quality mode
and preset K. Its 1963×1472 input is NVIDIA's queried optimal size. It does not
change the final headset, HUD or tone-mapping output dimensions. Original
full-size scene resources remain allocated, so VRAM consumption is not a useful
measure of whether SR is active.

SR selects its own temporal reconstruction and currently excludes NR and DLAA.
Selecting NR or DLAA disables SR. FG can be enabled independently, including
with SR; it is not required for either SR or NR. Active MSAA excludes all neural
processing because the verified depth transport is single-sampled. Changing
MSAA still requires a restart.

Use separately obtained NVIDIA SDKs and runtime DLLs under NVIDIA's terms.
The repository distributes its GPL-3.0-only addon source, not NVIDIA runtime
binaries, game assets or private captures. The reference is
[TF2VRNeuralRendering](https://github.com/ethanfel/TF2VRNeuralRendering).

## Installation and controls

Follow [the Fluorine addon installation](STEAM.md). Close the game and helper
before replacing the matching x86 layer and x64 helper; protocol **6** is not
compatible with older helper binaries. Preserve the runtime INI and profile
settings rather than replacing them with development defaults. The Steam
command and WiVRn registration remain unchanged.

Add your separately obtained `nvngx_dlssg.dll` to the private `runtime/` directory
for FG. `nvngx_dlss.dll` already used for DLAA also supplies SR. The x64 build now
requires `NGX_FG_INCLUDE` pointing at the external frame-generation SDK headers,
in addition to the common and NR headers.

```ini
[super_resolution]
enabled=0
capture_probe=0
[frame_generation]
enabled=0
[diagnostics]
gpu_handoff=1
```

Select the desired features in VR Settings. FG automatically uses opaque GPU
timelines while enabled; it cannot use the older CPU-only transport. GPU handoff
is also recommended for SR. Keep `capture_probe=0` for normal use: that diagnostic
forces reduced scene rendering without neural reconstruction.

Explicit CLI controls are available for an identified runtime INI:

```sh
python3 neural/tools/control.py --config /path/to/runtime/TTWVRNeuralRendering.ini sr on
python3 neural/tools/control.py --config /path/to/runtime/TTWVRNeuralRendering.ini fg on
python3 neural/tools/control.py --config /path/to/runtime/TTWVRNeuralRendering.ini all-off
```

Turning features off restores native rendering and ordinary runtime pacing.
Live changes briefly bypass processing while the previous helper exits. No
second helper starts until its predecessor has released shared resources.

## TTWVR input and output contract

TTWVR renders the world in D3D9 and supplies final D3D11 eye textures to OpenXR.
The addon identifies the hash-verified scene and tone-map draw, rather than
copying Titanfall's texture signatures or shader bindings.

1. Capture scene HDR, matching D24 depth, current-to-previous pixel motion and
   exposure before tone mapping. Verify their frame, eye and dimensions.
2. SR substitutes lower-resolution world color/depth targets and matching motion
   buffers and viewport. It retains the game's bloom/adaptation output sizing.
3. The helper reconstructs each eye independently through D3D12 NGX. Its full-size
   HDR output is bound only for the tone-map draw; the previous binding is restored.
   Alpha is bilinearly resampled from the original input. SR uses 18 Halton phases,
   with the verified raster-jitter sign; motion excludes jitter.
4. FG retains decoded guides for that exact frame and eye. It resamples them onto
   the final eye grid, removes the sampling displacement and scales pixel motion
   by output/input dimensions. There is no Titanfall-specific sign or `/1024`
   conversion. The camera is decoded from TTWVR's verified unjittered matrices.
5. Before OpenXR image release, copy the final BGRA sRGB eye into private GPU
   storage. The helper converts channel order while preserving encoded color,
   evaluates per-eye FG and returns private RGBA real/generated textures. Final
   eye images contain the HUD; they are not falsely labelled HUD-less inputs.
6. Queue private copies of both real and generated eyes. A presentation worker
   owns the downstream wait/begin/end calls and its own swapchains. It presents
   generated then real pairs, or repeats a real pair when the producer is late.
   Generated poses are midpoints of the two real poses. Repeats are logged
   separately and are never counted as generated images.

The D3D9 and D3D11 producer queues have **separate opaque Vulkan timelines**.
A counter shared between independently executing queues could be signalled out
of order. The helper acknowledges submission before the producer enqueues its
output wait; no unconfirmed future signal is waited on. Normal rendering uses
GPU textures and GPU dependencies, without CPU image transfers.

The queued path enables D3D11 immediate-context multithread protection while its
worker is active and restores the previous setting after joining the worker.
Explicit DXVK `Enter`/`Leave` calls alone are ineffective when protection is off.
Stopping the worker drains its queued D3D11 copies before restoring protection.
Session/instance teardown also stops and waits for the neural helper before
destroying downstream resources; waits are bounded and absent from normal rendering.
Owned presentation swapchains are released when the worker stops. A subsequent
worker creates fresh images rather than reusing imported state across menus.
No compositor-owned image is read or written after release. Queued packets hold
value copies of view metadata and owned textures. The worker is drained before
application spaces or sessions are destroyed.

Unsupported layer compositions, menus, missing guides and failed initialization
retain original-image bypass. The initial supported presentation case is one
opaque stereo projection with separate full-size eye swapchains and no extension
chains. Resolution/camera changes reset temporal history. Generated presentation
adds buffering and cannot improve the responsiveness of the underlying simulation.

## Evidence and interpretation

Capture-first validation on 1.1.3 established 1963×1472 color, depth and motion
for both eyes on consecutive frames, with 2944×2208 tone-map/OpenXR output.
Actual SR then logged `sr_evaluate ... output_applied=1`; its HDR output was finite,
changed RGB and preserved resampled alpha within the capture comparison's bound.

The adapted FG backend probe uses BGRA inputs and TTWVR's pixel-motion convention
at both native and SR guide sizes. Both eyes place a moving object's generated
image at the expected halfway position. The in-game queued captures include
consecutive real/generated stereo pairs, actual generated submission records and
pixel-identical real RGB after the channel-format round trip.

Runtime logs distinguish:

- `sr_evaluate ... output_applied=1`: reconstructed HDR produced for a scene eye.
- `fg_output_ready ... output_applied=1`: helper output is available, not proof of presentation.
- `fg_present mode=queued ... generated=1`: a generated stereo pair was submitted.
- `repeat=1`: a repeated real pair, not a new generated image.
- `fg_gpu_timing`: NGX evaluation only, excluding preparation, copies and presentation.

Capture requests save original, `-fg-real.dds` and `-fg-generated.dds` images for
each eye. `fg_interpolated` identifies actual interpolation and `fg_presented`
records whether that source frame's generated pair was submitted by readback time.
The generated format is RGBA sRGB (29); the original is BGRA sRGB (91).
Export a selected stereo capture and verify its real-image color round trip:

```sh
python3 neural/tools/frame_generation_preview.py /path/to/capture-run FRAME /path/to/output
```

The exporter preserves each eye's `fg_presented` metadata; inspect it separately
from `fg_interpolated`. A generated texture alone is not proof of presentation.

The initial synchronous reference-style delivery loop produced valid images but
lost throughput in Monado's adaptive wait scheduling. The queued implementation
replaces it. A development crash exposed disabled D3D11 multithread protection;
that issue was corrected before the repeated capture validation. A later active-FG
quit stalled inside the simulated runtime's Vulkan device destruction. Explicit
GPU-copy and helper teardown drains fixed direct quit. A menu/resume case also
needed fresh presentation swapchains after stopping the worker. SR+FG then
continued generating after menus, passed another pause/resume and quit cleanly.
This local result does not diagnose the proprietary driver or validate
all runtime teardown paths. Native ownership,
menu/failure/shutdown tests and a ThreadSanitizer run also exercise the worker.

See [the current measurement record](../../docs/sr-frame-generation.md) for
application FPS, distinct generated frames, repeats, GPU use and limitations.
The older [GPU handoff measurements](../../docs/proton-neural-performance.md)
remain a separate investigation. Neither update establishes a fix for the
reported distant DLAA softness/haze.

Primary API references:
[NVIDIA DLSS SR integration](https://github.com/NVIDIA-RTX/Streamline/blob/main/docs/ProgrammingGuideDLSS.md),
[NVIDIA FG integration](https://github.com/NVIDIA-RTX/Streamline/blob/main/docs/ProgrammingGuideDLSS_G.md),
[OpenXR specification](https://registry.khronos.org/OpenXR/specs/1.1/html/xrspec.html).
