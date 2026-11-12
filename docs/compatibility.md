# Tested configuration and feature coverage

[← README](../README.md) · [Installation](installation.md)

| Component | Tested version / hardware |
|---|---|
| Linux | EndeavourOS / Arch, KVM guest |
| GPU / driver | RTX 5090 / NVIDIA 615.78.08 |
| CPU / headset | Ryzen 9 5950X / Quest 3 |
| Streaming runtime | WiVRn 26.9 |
| Proton | Exact `experimental-11.0-20261105c-x86_64`, WoW64 |
| Container / startup probe | Steam Linux Runtime 4 / xrizer `0.6.r5.g63fc7eb`, 64-bit |
| Native manager | Fluorine 0.4.1, FUSE |
| TTW / xNVSE | 3.4 / 6.4.9 |
| TTWVR | Supplied 1.1.3 and 1.1.4 archives, exact hashes |
| DXVK patch source | `cf0a0d90f8965147741ad17dcd0b263817572ab3`, mapped-pool and opaque-fence changes |

## Current status

| Feature / check | Evidence |
|---|---|
| Native Fluorine, migrated saves, VR gameplay | Confirmed by the user |
| Flat TTW intro and utilities | Passed before VR setup |
| Author DLSS Quality / Balanced / Performance | 1.1.3: fresh consecutive frames in both eyes, translated-head test, successful per-eye NGX evaluations |
| Author native Vulkan FSR | 1.1.3: Quality/Balanced, consecutive stereo/movement, no native-FSR failure; 1.1.4: Quality at 100% |
| Resolution slider | 1.1.3: 100/150/200%; outputs 2944×2208 / 4480×3360 / 5888×4416 in this runtime configuration |
| Physical author-path image quality | User confirmed good 200% DLSS Quality output with 1.1.3; no controlled sustained headset benchmark |
| 1.1.4 port | DLSS Quality 200% and FSR Quality 100% passed isolated stereo/movement checks; successful NGX calls for DLSS; physical confirmation pending |
| Original menu/settings | Preserved; 1.1.4 adds Turn Speed and Loot Menu keys. Every setting's behavior has not been exhaustively tested. |
| Linux setup tool | Fresh dependency build, apply/rollback on disposable real files, Qt argument parsing, generated launch dry-run and real namespace overlays; see [tool validation](setup-validation.md) |
| Voice, multiplayer, all calibration/control combinations | Not comprehensively validated |
| Long sessions, outdoor frame times, other GPUs/distros | Pending |
| Custom NR/DLAA/SR/FG | Archived; not part of the current patch workflow |

The author renderer uses Vulkan, DLSS evaluation uses its D3D11 helper, and FSR
runs directly in Vulkan. No custom D3D12 processing helper is needed.
TTWVR uses OpenXR directly; xrizer serves Proton's earlier VR startup probe.
The launcher does not replace global runtime registration.

## Evidence

- [1.1.4 patch hashes, stereo captures and settings preservation](ttwvr-1.1.4.md)
- [Author DLSS output diagnosis and measurements](author-dlss.md)
- [Native Vulkan FSR and shared-fence repair](native-vulkan-fsr.md)
- [Native save-mount integration](native-fluorine.md#save-mount-compatibility-fix)
- [OpenXR bridge](openxr-layers.md), [shader compiler](simulated-vr.md)
- [Earlier mapped-pool A/B and physical headset result](dxvk-pools.md)
- [Archived custom rendering experiments](../archive/rendering-addon/README.md)

The earlier 59.8 FPS physical capture at 120 Hz used Windows MO2 and the
pool-only path. It is not a benchmark of the current author path or Fluorine.
Monado's simulated pacing differs from WiVRn; do not treat simulated application
FPS or an output-resolution change as a controlled headset performance result.
