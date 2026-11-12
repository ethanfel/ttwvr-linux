# FSR 3.1.5 through the Linux rendering addon

FSR Quality, Balanced and Performance are available in **VR Settings → FSR SR**.
The addon uses the AMD runtime supplied with TTWVR 1.1.3 through its existing
D3D11/D3D12 GPU transport. The original TTWVR DLL and ordinary Proton remain in
use, with the existing OpenXR bridge/DXVK pool overlays. The diagnostic Wine
D3D12 fence workaround from the bundled-renderer investigation is not used.

This is a functional first path, **not a demonstrated FPS improvement**. FSR
costs less GPU power than our DLSS Quality path in this test, but more than
native rendering. Physical headset motion and visual quality still need testing.

## Install and select

1. Close Fallout and its helper. Back up the runtime directory and INI.
2. Build/install the matching x86 layer and x64 helper using the existing
   [Fluorine instructions](../neural/docs/STEAM.md). Protocol **8 / 448 bytes**
   adds camera world-scale metadata; older helpers are incompatible.
3. Copy `NVSE/Plugins/TTWVR/amd_fidelityfx_upscaler_dx12.dll` from your own
   TTWVR 1.1.3 download into the addon's `runtime/` folder, alongside the optional
   NVIDIA DLLs. This binary is not distributed by this repository. The tested
   runtime reports **3.1.5**. No extra loader DLL or global DLL search-path change
   is required.
4. Select **FSR SR → Quality / Balanced / Performance**. Changes apply live at a
   stereo boundary. **Off** restores native scene rendering. Keep the same Steam
   command and WiVRn registration.

| Preset | Scene/depth/motion per eye | Reconstructed output | Jitter phases |
|---|---|---|---|
| Quality | 1962×1472 | 2944×2208 | 18 |
| Balanced | 1731×1298 | 2944×2208 | 23 |
| Performance | 1472×1104 | 2944×2208 | 32 |

Only the verified 2944×2208 output layout is supported. The SDK's resolution and
jitter queries are checked at initialization. Keep the tested TTWVR Resolution
setting of 150%. HUD and final eye dimensions stay full size. FSR owns temporal
AA and excludes NR, DLAA, DLSS and frame generation. Active MSAA requires a
restart before FSR can run. The menu marks those pending selections.

```ini
[super_resolution]
enabled=1
backend=FSR
mode=Quality
capture_probe=0
[diagnostics]
gpu_handoff=1
```

The CLI equivalent is `python3 neural/tools/control.py --config /absolute/path/to/TTWVRNeuralRendering.ini fsr quality`.
Use `balanced`, `performance` or `off` in place of `quality`. Repository defaults
keep effects off. A failed initialization retains original rendering; a missing
AMD runtime was explicitly tested, including full-resolution stereo captures.

## Implementation contract

- Source HDR, matching depth, current-to-previous pixel motion and exposure are
  captured at the verified TTWVR pre-tone-map point. Input color is clamped to
  nonnegative HDR; exposure is supplied separately with pre-exposure 1.
- One FSR context and history per eye. Inputs are shared D3D11/D3D12 textures;
  GPU fences and the existing opaque cross-process timelines retain ordering.
  Normal rendering performs no CPU image transfers.
- Finite forward depth, near/far and vertical FOV come from the verified TTWVR
  matrices. World-to-meter scale comes from TTWVR's effective units-per-meter
  setting. Motion uses pixel scale (1,1), without an imported Titanfall scale or
  sign assumption. Jitter matches the verified raster displacement.
- Reconstruction returns HDR before the game's tone mapping. Alpha is resampled
  from original HDR. Graphics state is restored and OpenXR image ownership is
  unchanged. History resets on discontinuities, camera cuts and long gaps.
- Sharpening is off. Reactive/transparency masks are not yet supplied, so fog,
  particles, disocclusion and text need headset review. This does not resolve
  the separately reported DLAA distance haze.

AMD API headers are pinned to SDK v2.0.0 commit
`f4c1da8e92f3fe563b5c28c44e6267ce6b6b8eb2`; their MIT notices are preserved in
`neural/third_party/fidelityfx`. Addon code remains GPL-3.0-only. See
[AMD's source](https://github.com/GPUOpen-LibrariesAndSDKs/FidelityFX-SDK/tree/f4c1da8e92f3fe563b5c28c44e6267ce6b6b8eb2/Kits/FidelityFX).

## Isolated validation and measurements

Both eyes logged `fsr_evaluate ... output_applied=1`, including consecutive
frames. All three presets created separate contexts at the expected dimensions.
Controller navigation changed Quality to Balanced and returned to gameplay.
Switching FSR/off/DLSS rebuilt helpers cleanly. Five native test suites and nine
CLI state/invalid-input checks passed.

On the final clear scene, a 0.6 m simulated head movement changed 99.46% of
left-eye pixels and 99.41% of right-eye pixels. This rejects the stale-output failure found in the author's
experimental DLSS path. Same-frame HDR checks found finite reconstructed output
and alpha error below the hardware bilinear/half-float tolerance. Initial
captures intersected the avatar (also with effects off); Full Body was disabled
only in the private test for the final clear-scene comparison and timings.

RTX 5090 / Ryzen 9 5950X; copied corridor save, stationary simulated Monado at
90 Hz. Each timed sample runs 30 seconds after settling. No capture, debugger,
image processing, build or GPU timestamp profiling runs during these samples.
Application CPU span excludes the OpenXR wait/end spans; it is not GPU time.

| Mode | App FPS | App CPU span (ms) | GPU load | GPU power |
|---|---:|---:|---:|---:|
| native | 38.50 | 11.50 | 34.7% | 86.9 W |
| FSR Quality | 39.45 | 14.90 | 43.8% | 120.4 W |
| FSR Balanced | 38.99 | 15.41 | 43.0% | 116.9 W |
| FSR Performance | 38.70 | 15.93 | 42.1% | 113.5 W |
| DLSS Quality | 39.24 | 15.60 | 44.5% | 131.3 W |
| native repeat | 39.37 | 11.17 | 36.1% | 94.5 W |

The native repeat brackets ordinary run variation. All modes remain around
39 app FPS: these data do not establish an FSR speedup. Quality uses about 8%
less GPU power than DLSS Quality in this scene; that is not a comparison against
native rendering, which consumes less. Lower FSR input resolution alone does
not remove the observed limit. Simulated-runtime timing does not predict WiVRn
streaming latency, headset comfort or performance in other scenes.

[Machine-readable evidence and runtime hashes](evidence/fsr-20261112.json).
Private captures and game/NVIDIA/AMD runtime binaries are not published.
