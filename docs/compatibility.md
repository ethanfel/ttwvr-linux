# Tested configuration

[← README](../README.md) · [Installation](installation.md)

Initial physical headset test: **2026-11-11**. These are the tested versions,
not a claim that every component must use exactly this version. The binary
patches do require the matching Proton files.

| Component | Tested version / hardware |
|---|---|
| Linux | EndeavourOS / Arch, KVM guest |
| GPU / driver | RTX 5090 / NVIDIA 615.78.08 |
| CPU | Ryzen 9 5950X |
| Headset | Quest 3, 120 Hz |
| Streaming runtime | WiVRn 26.9 |
| Proton | `experimental-11.0-20261105c-x86_64`, WoW64 enabled |
| Steam container | Steam Linux Runtime 4 |
| xrizer | `0.6.r5.g63fc7eb`, 64-bit |
| Mod Organizer 2 | 2.5.2, portable |
| TTW / TTWVR | 3.4 / 1.1.0 |
| xNVSE | 6.4.9 |
| DXVK source | `cf0a0d90f8965147741ad17dcd0b263817572ab3`, with the pool patch |

## Current status

| Check | Result |
|---|---|
| Flat TTW intro and utilities | Passed |
| VR save loading | Passed with native D3DCompiler 47 |
| Headset gameplay and tracking | Passed with the OpenXR bridge patch |
| Full-resolution performance | 59.8 FPS capture at 120 Hz; user reported clear, very smooth gameplay |
| Rendering settings | Original 2944×2208 eye targets, `Resolution=150%` |
| DLSS / FSR upscaler path | Not working; experimental patches disabled |
| Blood-effect depth copy | Warnings remain |
| Complete controls / calibration / Pip-Boy checks | Pending |
| Long sessions, outdoor benchmarks, other hardware | Pending |
| Physical 90 Hz test with the final patches | Pending |

TTWVR uses **OpenXR directly**. xrizer is used by Proton's earlier VR startup
probe. No global OpenXR/OpenVR runtime change is required by the launcher.

## Evidence

- [OpenXR patch and regression results](openxr-layers.md)
- [DXVK control comparison and physical headset result](dxvk-pools.md)
- [Compiler workaround and save-load test](simulated-vr.md)
- [Earlier investigation and supplied binary hashes](investigation.md)
- [Installed utility archive records](guide-dependencies.json)

FPS captures are scene snapshots, not average FPS benchmarks. The patched
90 Hz simulated control comparison was 18.8 → 44.1 FPS. The physical 120 Hz
result was a separate test; it should not be treated as the same comparison.
