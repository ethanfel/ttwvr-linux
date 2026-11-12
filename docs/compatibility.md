# Tested configuration

[← README](../README.md) · [Installation](installation.md)

Primary manager: **native Fluorine 0.4.1, FUSE**, functionally confirmed on
**2026-11-12**. Initial physical headset measurements were recorded on
**2026-11-11** with the now-archived Windows MO2 setup. These are tested versions,
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
| Native manager | Fluorine 0.4.1, FUSE backend |
| Historical manager | Windows MO2 2.5.2 portable; [archived workflow](archive/mo2/README.md) |
| TTW / TTWVR | 3.4 / supplied 1.1.3 installed; verified 1.1.0 remains supported |
| xNVSE | 6.4.9 |
| DXVK source | `cf0a0d90f8965147741ad17dcd0b263817572ab3`, with the pool patch |

## Current status

| Check | Result |
|---|---|
| Native Fluorine launch and migrated save | Passed; confirmed by the user, without a new measured headset benchmark |
| TTWVR 1.1.3 upgrade | Installed with adapted addon/layer; 53 checked settings/save files unchanged. Revised menu, stereo NR/DLAA and MSAA 4× passed isolated tests; headset confirmation pending. |
| Flat TTW intro and utilities | Passed in the original setup; retained through migration |
| VR save loading | Passed with native D3DCompiler 47 |
| Headset gameplay and tracking | Passed with the OpenXR bridge patch |
| Earlier full-resolution performance | 59.8 FPS capture at 120 Hz with Windows MO2; user reported clear, very smooth gameplay. Not a Fluorine benchmark. |
| Capture resolution | 2944×2208 per eye, `Resolution=150%` in isolated tests; the upgrade preserves the regular profile's chosen resolution |
| TTWVR built-in DLSS / FSR upscaler | Disabled in this addon setup; original path not revalidated on 1.1.3 |
| Rendering Addon in Fluorine | Installed; native VR Settings rows, gamepad navigation, live NR/DLAA, restart for MSAA |
| Scene MSAA 2×/4×/8× | Matching full-size color/depth sample counts verified in isolated GPU runs |
| NR and native-resolution DLAA | Both eyes/consecutive frames verified in isolated gameplay captures; DLAA softness/haze at distance remains under investigation |
| Blood-effect depth copy | Warnings remain |
| Complete controls / calibration / Pip-Boy checks | Pending |
| Long sessions, outdoor benchmarks, other hardware | Pending |
| Physical 90 Hz test with the final patches | Pending |

TTWVR uses **OpenXR directly**. xrizer is used by Proton's earlier VR startup
probe. No global OpenXR/OpenVR runtime change is required by the launcher.

## Evidence

- [TTWVR 1.1.3 compatibility and menu update](ttwvr-1.1.3.md)
- [Native Fluorine configuration, save mount and migration validation](native-fluorine.md)
- [Rendering Addon controls, jitter correction and stereo tests](../neural/docs/VR-SETTINGS.md)
- [OpenXR patch and regression results](openxr-layers.md)
- [DXVK control comparison and physical headset result](dxvk-pools.md)
- [Compiler workaround and save-load test](simulated-vr.md)
- [Earlier investigation and supplied binary hashes](investigation.md)
- [Installed utility archive records](guide-dependencies.json)

FPS captures are scene snapshots, not average FPS benchmarks. The patched
90 Hz simulated control comparison was 18.8 → 44.1 FPS. The physical 120 Hz
result was a separate test; it should not be treated as the same comparison.
