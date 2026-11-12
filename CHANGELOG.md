# Changes

## Optional DLSS Quality and queued stereo frame generation — 2026-11-12

- Add independent live SR and FG rows inside native VR Settings; preserve existing feature preferences and default both new features off.
- Reconstruct verified 1963×1472 scene inputs to 2944×2208 per eye using per-eye NGX SR state, while retaining native-size tone mapping and HUD.
- Adapt the FG backend to TTWVR camera and pixel-motion conventions, share final eye textures through separate GPU timelines, and queue private generated/real stereo pairs with tracked OpenXR ownership.
- Protect the D3D11 context during queued delivery; drain queued GPU copies, renew presentation swapchains after menus, and stop the helper before runtime teardown. Preserve bypass and ordinary pacing when disabled. Layer/helper protocol is now 6.
- Add stereo delivery/camera tests and native/SR-guide moving-object probes. Record actual consecutive-eye captures and distinct/generated/repeated presentation counts.
- Publish [120 Hz measurements and limits](docs/sr-frame-generation.md): SR alone did not improve the measured corridor; generated frames add buffering and need headset validation. DLAA haze remains unresolved.

## Neural GPU handoff and Proton research — 2026-11-12

- Add an opt-in opaque Vulkan timeline handoff between the 32-bit game and 64-bit NGX helper, preserving GPU ordering without CPU completion waits. Matching protocol-4 layer/helper required; distributed default remains off.
- Materialize deferred DXVK depth clears before external copies; verify HDR/depth/output across 32 asynchronous round trips with deferred readback and zero pixel mismatches.
- Reuse the helper completion query, avoid implicit polling flushes, and batch output synchronization with the resolve.
- Add optional OpenXR CPU-span timing and document matched full-resolution tests, capture evidence, current Proton/NR findings and rollback in [the investigation](docs/proton-neural-performance.md).
- Verify the reference FG backend in the isolated Proton prefix; no TTWVR frame generation or Super Resolution integration is enabled. DLAA haze still needs headset assessment.

## TTWVR 1.1.3 deployment and resource guide — 2026-11-12

- Record the completed Fluorine upgrade, unchanged settings/saves and rollback backup.
- Publish a [copy-ready Discord resource post](docs/discord-resource-post.txt), with three messages below 2,000 UTF-16 units each.
- Clarify the new menu entry, paired addon/layer upgrade, isolated validation and pending headset checks.
- Mark the initial addon settings as historical and distinguish the disabled stock upscaler from its untested updated host.

## TTWVR 1.1.3 compatibility — 2026-11-12

- Support the supplied 1.1.3 DLL with a shared hash-selected hook table, retaining 1.1.0 support and rejection of unknown builds.
- Keep addon controls inside the new top-level VR Settings entry; verify stock option labels before hiding upscaler rows.
- Adapt the native D3D9 device, motion-guide and MSAA startup hooks to the new build. Rendering algorithms and final resolution are unchanged.
- Preserve the separate DLAA haze investigation; this compatibility update does not claim to fix it.
- Document [upgrade steps and validation](docs/ttwvr-1.1.3.md).

## Fluorine-first documentation and MO2 archive — 2026-11-12

- Make native Fluorine/FUSE the primary installation, Steam launch and addon workflow.
- Clarify instance configuration paths, private prefixes and the two launcher stages.
- Preserve the previous Windows MO2 installation, patch, troubleshooting and addon guides in [the legacy archive](docs/archive/mo2/README.md), with migration/rollback guidance.
- Distinguish the earlier MO2 performance captures from native migration validation.
- Retain shared launcher filenames and existing MO2 support; this documentation update changes no runtime behavior.

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
