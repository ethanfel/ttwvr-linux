# Changes

## 2026-11-11

- **Added an isolated spatial NR prototype:** full-resolution D3D9 scene inputs,
  64-bit NGX helper, GPU depth/HDR transport, per-eye NR features and confirmed
  in-game stereo captures on consecutive frames. History resets every frame;
  temporal NR and DLAA remain unvalidated and disabled. The working Steam setup
  is unchanged. [Source and evidence](neural/README.md)

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
