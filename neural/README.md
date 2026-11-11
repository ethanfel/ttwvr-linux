# TTWVR neural rendering prototype

**Spatial NR works in the isolated Linux test game. Temporal NR and DLAA are not
ready.** Both eyes were captured in the loaded Vault 101 scene on consecutive
frames, at the original **2944 × 2208 per eye**. No frame generation or Super
Resolution upscaling is implemented. The normal Steam installation is unchanged.

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

**There is no verified motion-vector source yet.** The only live mode is an
explicit spatial experiment: zero motion and a history reset on every eye/frame.
It cannot establish temporal quality or acceptable headset comfort. The backend
rejects DLAA activation until motion and projection jitter are verified.

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
- your separately obtained `runtime/nvngx_dlssnr.dll`.

The helper requires the driver NGX core, working D3D12, NVIDIA GPU visibility and
native D3DCompiler 47. Register the manifest **only in the copied prefix's 32-bit
OpenXR implicit-layer registry view**. Do not apply that manifest to the native
Linux OpenXR loader. The existing private Wine OpenXR bridge is still required.
See the repository's [simulated runtime notes](../docs/simulated-vr.md).

Defaults leave NR off. In the isolated INI, set both `enabled=1` and
`experimental_spatial=1` under `[neural_rendering]` to opt in. First use
`transport_identity=1`; restart with `transport_identity=0` to evaluate real NR.
The identity option is fixed when the helper starts. Turning `enabled=0` is
polled every 30 submitted frames and restores bypass. A helper failure disables
processing for that process; the original HDR is not overwritten before a
successful helper response. `DISABLE_TTWVR_NEURAL_RENDERING=1` bypasses the layer
entirely for that launch. Never replace a DLL/executable while it is loaded.

The scene adapter is deliberately restricted to the verified TTWVR 1.1.0 binary,
shader hash, formats and dimensions. Unknown layouts fail closed. Prototype GPU
resources are retained for process lifetime; restart between test configurations.
Failure after one eye has completed can produce one mixed pair before bypass;
this requires further hardening before normal headset use.

## Capture and inspection

Create `capture.request` next to the layer. With `burst_frames=2`, the layer
copies owned OpenXR images before release, holds both consecutive stereo frames,
then reads back and writes private DDS/JSON files. Confirm that the game has
passed its menu and loaded the scene. Metadata says NR only if both eye writes
succeeded for that exact frame. NR captures use `mode=spatial_nr_reset_every_frame`.

```sh
python neural/tools/capture_preview.py /private/captures/capture-...-eye0.dds /private/previews
```

Python previews require NumPy and Pillow. `prepare_replay.py` also verifies eye
correspondence, D24 range and uniform exposure before preparing an explicitly
spatial replay. Raw captures, game shader bytecode, saves and runtime binaries
stay outside the repository. Capture stalls are diagnostic costs and must not be
counted as normal rendering performance.

## Further work

Verify camera/object motion and projection conventions in TTWVR's own stereo
rendering path, then temporal NR, then independently switchable native DLAA.
Measure jitter alignment, flicker, ghosting, eye disagreement, HUD/text, GPU and
application frame time. Real WiVRn/Quest testing remains necessary. This prototype
is a visual processing feature and does not improve the game's frame rate.

Additional references:

- [FNV-DLSS5 Nexus setup](https://www.nexusmods.com/newvegas/mods/99411).
- [chasmlol/fnv-dlss5](https://github.com/chasmlol/fnv-dlss5): raster-preserving
  Remix/DLAA host, camera metadata and jitter references. Its flat-screen camera
  and motion rules are not assumed valid for TTWVR.
- [DLSS5-Feeder](https://github.com/jlrouzies-fr/DLSS5-Feeder): cross-bitness
  architecture reference. No third-party injector binaries were installed from it.
