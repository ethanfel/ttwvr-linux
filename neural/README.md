# TTWVR neural rendering prototype

**Native-resolution DLAA, spatial NR and temporal NR run in the isolated Linux test game.**
Both eyes were captured with DLAA alone, temporal NR alone and both combined
in the loaded Vault 101 scene on consecutive frames,
at the original **2944 × 2208 per eye**. The temporal motion path is experimental;
see the exact tested modes in [the evidence record](docs/EVIDENCE.md).
No frame generation or Super Resolution upscaling is implemented. The normal
Steam installation is unchanged.

This GPL-3.0-only port uses
[TF2VRNeuralRendering](https://github.com/ethanfel/TF2VRNeuralRendering) as its
backend reference. NVIDIA headers and runtime DLLs are separate dependencies
under NVIDIA's terms; they are not included here. See
[notices](THIRD-PARTY-NOTICES.md) and [test evidence](docs/EVIDENCE.md).

## What runs

New Vegas is a **32-bit D3D9** application on DXVK. TTWVR 1.1.0 submits its finished
eye images through D3D11 OpenXR. The port therefore uses a 32-bit capture/scene
hook and a separate 64-bit D3D11/D3D12 NGX helper.

The verified interception point is immediately before the final HDR tone-mapping
draw. It exposes the scene HDR, matching D24 depth, and bloom with adapted
luminance in alpha. NR modifies the original scene HDR there; the game then runs
its existing tone mapping. Bloom has already been computed from the original
image. This is a late HDR insertion point, not an interception before every
post-processing effect.

The normal processing path shares GPU textures. Depth is copied through a
device-local Vulkan buffer into packed RGBA8 bytes, preserving all 24 bits, then
decoded by a compute shader in the helper. The helper derives exposure from
`HDRParam.x / max(bloom.a, HDRParam.x)`, applies exposure, a highlight shoulder and
sRGB encoding, evaluates NR, and applies bounded gain to the original HDR while
preserving signed values and alpha. Each eye has its own NGX feature.

The motion adapter reuses the hash-verified TTWVR object-motion recorder while
leaving its unavailable upscaler off. Native vectors are current-to-previous
pixel displacements, with validity in the third channel. Background motion is
reconstructed from matching D24 depth, the renderer's view/projection matrices,
and the separate camera origin used by New Vegas's camera-relative rendering.
Camera matrices are checked against actual scene vertex constants. Image
reprojection independently verified the direction, translation and eye pairing.

DLAA uses eight Halton phases at native resolution, with independent NGX state
for each eye. Scene jitter changes only the current draw's verified MVP registers
and restores them afterward. Both engine matrix declarations and TTWVR's
replacement lighting shaders' four-vector declarations must receive the same
jitter. Missing the latter caused bright surfaces in an early test and was fixed.
OpenXR poses, FOVs and image ownership remain unchanged.

Motion excludes jitter, so NGX's MVJittered flag is off. DLAA resolves first; if
NR is also selected, it runs on that HDR output with the guides aligned to the
resolved image. Each feature has separate per-eye state. Gaps, long stalls and
large camera changes reset history. The older spatial NR experiment remains
available with zero motion and a reset on every evaluation. None of these tests
establish headset comfort or quality across untested scenes.

Shared-texture handoffs currently wait for GPU completion on the CPU. Images do
not travel through CPU memory during processing, but these waits add latency.
D3D11/D3D12 synchronization inside the 64-bit helper uses GPU fences. The broken
cross-bitness Wine shared-fence experiment is not used.

## Build

Linux tools: CMake, Ninja, MinGW x86/x64, clang-cl, OpenXR headers and external
Vulkan headers. The x64 backend additionally needs NVIDIA common and NR headers.

```sh
cmake -S neural -B /path/to/build32 -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE=cmake/mingw32.cmake -DCMAKE_BUILD_TYPE=Release \
  -DOPENXR_INCLUDE_DIR=/usr/include/openxr \
  -DVULKAN_INCLUDE_DIR=/path/to/Vulkan-Headers/include
cmake --build /path/to/build32

cmake -S neural -B /path/to/build64 -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE=cmake/mingw64.cmake -DCMAKE_BUILD_TYPE=Release \
  -DNGX_COMMON_INCLUDE=/path/to/nvidia-common/include \
  -DNGX_NR_INCLUDE=/path/to/nvidia-nr/include
cmake --build /path/to/build64

cmake -S neural -B /path/to/build-native -G Ninja
cmake --build /path/to/build-native
ctest --test-dir /path/to/build-native --output-on-failure
```

The actual OpenXR entry points are tested for ownership, failed waits/releases,
array slices, burst capture, asynchronous writes and bypass behavior. The
standalone `shared_texture_probe.exe` and `d3d9_texture_probe.exe` verify GPU
transport under Proton; they deliberately read pixels back for verification.
`replay_probe.exe` processes captured stereo HDR outside the game. Its identity
mode verifies the conversion route; it does not claim an NR evaluation.

## Isolated installation only

Use a copied Proton prefix, game executables, MO2 configuration, profile and
saves. Large immutable assets may be read-only links. ESM/ESP files must be copies:
MO2 changes their timestamps. The test mount namespace must protect the originals.
Keep WiVRn's default registration and Titanfall files unchanged.

Place these together in the private layer directory:

- `TTWVRNeuralRenderingLayer.dll` from the x86 build;
- `neural_host.exe` from the x64 build;
- the manifest and INI in this directory;
- your separately obtained `runtime/nvngx_dlssnr.dll` for NR;
- your separately obtained `runtime/nvngx_dlss.dll` for DLAA.

The helper requires the driver NGX core, working D3D12, NVIDIA GPU visibility and
native D3DCompiler 47. Register the manifest **only in the copied prefix's 32-bit
OpenXR implicit-layer registry view**. Do not apply that manifest to the native
Linux OpenXR loader. The existing private Wine OpenXR bridge is still required.
See the repository's [simulated runtime notes](../docs/simulated-vr.md).

Defaults leave both effects off. Configure the **private** INI and restart when
changing the feature combination:

| Mode | `[neural_rendering] enabled` | `experimental_spatial` | `experimental_temporal` | `[dlaa] enabled` |
|---|---:|---:|---:|---:|
| Bypass | 0 | 0 | 0 | 0 |
| Spatial NR | 1 | 1 | 0 | 0 |
| Temporal NR | 1 | 0 | 1 | 0 |
| DLAA alone | 0 | 0 | 0 | 1 |
| DLAA + temporal NR | 1 | 0 | 1 | 1 |

Use `transport_identity=1` first to exercise GPU transport; real evaluation
requires `transport_identity=0`. Identity mode is not evidence of an NGX effect.
The diagnostic `jitter_probe` should stay zero. Turning both `enabled` settings
to zero is polled every 30 submitted frames and restores bypass. Feature selection
is fixed when the helper starts; use a restart to change from one active
combination to another.

NR tuning mirrors the Titanfall reference: `model=A` or `B`,
`resolution_percent=25/50/75/100`, `intensity_percent`, `tone_percent` and
`detail_percent` (0–100). Defaults retain model B at 100% for the original
validation. NR's color, depth and motion inputs scale together; motion is converted
to pixels on that model grid. At 50%, they are 1472×1104, one quarter of the
pixel count. The game and DLAA still render at 2944×2208, and NR applies its
interpolated gain to the original full-resolution HDR. There is no separate
depth-only scale control. The original full-size depth transport and object-motion
rendering cost remain. These are NR presets, separate from DLAA's fixed preset K;
no untested preset is labelled faster or better.

Use an explicit private INI path for terminal controls (no in-game menu port yet):

```sh
python neural/tools/control.py --config /private/layer/TTWVRNeuralRendering.ini status
python neural/tools/control.py --config /private/layer/TTWVRNeuralRendering.ini quality 50
python neural/tools/control.py --config /private/layer/TTWVRNeuralRendering.ini model A
python neural/tools/control.py --config /private/layer/TTWVRNeuralRendering.ini dlaa on
python neural/tools/control.py --config /private/layer/TTWVRNeuralRendering.ini on
python neural/tools/control.py --config /private/layer/TTWVRNeuralRendering.ini all-off
```

Restart after tuning changes. Configured values are not proof of successful
processing; check the feature-creation sizes/preset and both eyes' evaluation logs.
Optional `[diagnostics] gpu_timing=1` records sampled `neural_gpu_timing` entries.
These use D3D12 GPU timestamps immediately around each NGX evaluation, collected
after the helper's existing completion wait, with no extra wait or image readback.
They exclude conversion, guide rendering, transport, synchronization and the game.
The same option samples camera/motion submission CPU time and the input/helper
handoff stages. The input path keeps one Vulkan completion wait; a redundant
D3D9 event-query wait was removed after pixel-level transport verification.
See [Microsoft's timestamp semantics](https://learn.microsoft.com/en-us/windows/win32/direct3d12/timing)
and [our measurements](docs/EVIDENCE.md). Leave this diagnostic off for normal use.

Initialization runs asynchronously with a bounded 120-second limit, keeping the
game on bypass while the driver builds features. A first DLAA initialization took
35 seconds. Initialization/evaluation failures disable processing for that
process; the original HDR is not overwritten before a successful helper response.
Jitter stops when processing is disabled or fails, and requires consecutive
verified camera data. Diagnostic motion recording only stays active during bypass
if explicitly requested with `native_motion_probe=1`.
`DISABLE_TTWVR_NEURAL_RENDERING=1` bypasses the layer entirely for that launch.
Never replace a DLL/executable while it is loaded.

The scene adapter is deliberately restricted to the verified TTWVR 1.1.0 binary,
shader hash, formats and dimensions. Unknown layouts fail closed. Prototype GPU
resources are retained for process lifetime; restart between test configurations.
Failure after one eye has completed can produce one mixed pair before bypass;
the current frame may already contain subpixel jitter. This requires further
hardening before normal headset use. DLAA preserves alpha and HDR highlights,
but its preparation clamps negative RGB; the signed-HDR identity guarantee of
the spatial NR transport test does not apply to DLAA.

## Capture and inspection

Create `capture.request` next to the layer. With `burst_frames=2`, the layer
copies owned OpenXR images before release, holds both consecutive stereo frames,
then reads back and writes private DDS/JSON files in a unique `captures/run-*`
directory per launch, preventing reused frame numbers from overwriting evidence.
Pass that run directory to the inspection tools. Scene and guide readback is
also deferred until the burst finishes. Confirm that the game has passed its
menu and loaded the scene. Feature metadata is set only if both eye writes
succeeded for that exact frame. Modes distinguish spatial NR, temporal NR,
DLAA, combined processing and bypass.

```sh
python neural/tools/capture_preview.py /private/captures/capture-...-eye0.dds /private/previews
python neural/tools/feature_comparison.py /private/captures FRAME /private/comparison
python neural/tools/validate_motion.py /private/captures FRAME /private/motion-check
```

Python previews require NumPy and Pillow; motion validation additionally uses
SciPy. `scene_inputs=1` captures motion, camera matrices and exact pre/post-feature
HDR. `feature_comparison.py` labels its HDR display proxy; it is not the game's
tone mapper. `prepare_replay.py` also verifies eye
correspondence, D24 range and uniform exposure before preparing an explicitly
spatial replay. Raw captures, game shader bytecode, saves and runtime binaries
stay outside the repository. Capture stalls are diagnostic costs and must not be
counted as normal rendering performance.

## Further work

Expand beyond the validated Vault 101 scene and fixed render dimensions. Check
scene transitions, weapons, transparencies, HUD/text, moving thin geometry,
flicker, ghosting and eye disagreement over longer sequences. Measure GPU and
application time under real WiVRn/Quest use; the simulated runtime cannot validate
streaming latency or comfort. This prototype adds rendering cost and is not a
frame-rate improvement.

Additional references:

- [FNV-DLSS5 Nexus setup](https://www.nexusmods.com/newvegas/mods/99411).
- [chasmlol/fnv-dlss5](https://github.com/chasmlol/fnv-dlss5): raster-preserving
  Remix/DLAA host, camera metadata and jitter references. Its flat-screen camera
  and motion rules are not assumed valid for TTWVR.
- [DLSS5-Feeder](https://github.com/jlrouzies-fr/DLSS5-Feeder): cross-bitness
  architecture reference. No third-party injector binaries were installed from it.
