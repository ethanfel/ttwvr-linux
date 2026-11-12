# Changes

## Native VR rendering settings and DLAA correction — 2026-11-12

- Package the rendering controls as a separately enableable addon in Fluorine/MO2.
- Add native Settings → VR rows for AA and NR tuning; hide the unavailable built-in upscaler controls.
- Enable scene MSAA 2×/4×/8× through a hash-verified xNVSE startup hook, with restart labels and a neural-processing bypass while MSAA is active.
- Correct the sign of the raster jitter supplied to DLAA and test the projection-to-NGX convention geometrically.
- Record isolated stereo captures, motion validation and remaining headset checks in [the evidence note](neural/docs/VR-SETTINGS.md).

## Native Fluorine launch integration — 2026-11-12

- Preserve Fluorine’s prepared game command and FUSE mod deployment while applying the existing VR patches.
- Support a Steam runtime in a secondary library and per-launch simulated runtime selection.
- Keep the desktop user ID through profile-save mounts so Steam uses the correct WiVRn socket directory.
- Add a native Steam entry point, migration/rollback documentation and save-mount integration checks.

## 2026-11-11

- **Connected the neural addon to regular Steam VR launches:** an opt-in manifest
  and `neural_rendering=true` instance setting enable the layer/NVIDIA access
  through the existing command. Added deployment and rollback instructions;
  flat launches stay outside the opt-in. [Setup](neural/docs/STEAM.md)

- **Added native in-game neural controls and live Apply:** separate NR/DLAA
  switches, model/scale/strength/tone/detail, controller navigation and keyboard
  fallback. Feature changes rebuild the helper on bypass between stereo frames,
  reset both histories, and preserve the original game/DLAA resolution. Failed
  generations can be retried; stale menu edits are rejected. No frame generation.
  [Controls and implementation](neural/README.md#in-game-controls)

- **Added experimental temporal NR and independent DLAA:** verified camera and
  object motion, per-eye history and native-resolution jitter; captured both eyes
  on consecutive gameplay frames with each effect and both combined. Added NR
  model A/B, resolution and tuning controls, plus separate GPU timing. Fixed
  inconsistent jitter in TTWVR's replacement lighting shaders, separated capture
  files per launch, and removed a redundant GPU completion wait after validating
  pending color/depth copies. All effects still default off; normal Steam/Titanfall/WiVRn setup remains unchanged.
  [Implementation and validation limits](neural/README.md)

- **Added an isolated spatial NR prototype:** full-resolution D3D9 scene inputs,
  64-bit NGX helper, GPU depth/HDR transport, per-eye NR features and confirmed
  in-game stereo captures on consecutive frames. History resets every frame;
  this original mode remains available alongside the temporal path. The working
  Steam setup is unchanged. [Source and evidence](neural/README.md)

- Added Amethyst as a native Linux alternative for base TTW setup, with its
  upstream guide. Amethyst integration with this project's VR launcher remains
  untested.
- **Documented setup:** short README, numbered installation steps, Linux patch
  commands, troubleshooting and a current compatibility summary.
- **Fixed save-load crash:** native D3DCompiler 47 replaces the failing Wine
  compiler for VR launches. [Details](docs/simulated-vr.md)
- **Fixed black headset gameplay:** private Proton OpenXR bridge patch corrects
  the observed mixed-layer allocation failure. [Details](docs/openxr-layers.md)
- **Improved full-resolution FPS:** DXVK reuses the large eye buffers. Simulated
  control: 18.8 → 44.1 FPS snapshots, with repeated mappings falling from 599 to
  2 in six seconds. [Patch and measurements](docs/dxvk-pools.md)
- **Verified Quest 3 at 120 Hz:** 59.8 FPS capture, original 2944×2208 eye targets,
  and user-confirmed clear, smooth gameplay. Long-session checks remain pending.
- **Fixed launch setup:** MO2's `TTW` identifier, Steam's OpenXR runtime import,
  and flat mode starting without a connected headset.
- **Kept experiments separate:** DLSS attempts remain unsuccessful and disabled;
  the reduced-resolution experiment is also disabled.

Original installation and patch records remain in the
[installation record](docs/installation-record.md),
[investigation history](docs/investigation.md), and
[earlier detailed changelog](https://github.com/ethanfel/ttwvr-linux/blob/d8ac75e/CHANGELOG.md).
