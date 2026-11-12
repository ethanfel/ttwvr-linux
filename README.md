# TTWVR on Linux: Fluorine + WiVRn

Play **Tale of Two Wastelands** in VR with KUPOkinz's **Tale of Two Realities**,
**native Fluorine Manager**, Proton and WiVRn. This repository supplies the
Linux launch integration, compatibility patches and an optional rendering addon.
Obtain the games, TTW and TTWVR from their authors; they are not included here.

**Fluorine is the primary setup.** Its FUSE backend manages mods on Linux, while
Proton runs Fallout. The native setup has loaded existing TTW saves and been
confirmed working in normal use. Windows MO2 is no longer part of the main guide.

## Start here

1. Install English **New Vegas Ultimate Edition** and **Fallout 3 GOTY**.
2. Create a native Fluorine TTW instance, or migrate your existing mod collection.
3. Verify flat TTW, then create a separate **TTW - VR** profile with TTWVR.
4. Apply the Linux fixes and configure Fluorine's wrapper and Steam entry point.
5. Connect your headset to WiVRn and launch New Vegas from Steam.

**[Install with Fluorine →](docs/installation.md)**

[Move an existing MO2 setup →](docs/native-fluorine.md#migrate-an-existing-mo2-installation)
· [Fluorine configuration and launch commands](docs/native-fluorine.md)
· [Troubleshooting](docs/troubleshooting.md)
· [Copy-ready Discord resource post](docs/discord-resource-post.txt)

The tested manager is **Fluorine 0.4.1 with FUSE**. The binary patches require
**Proton Experimental `experimental-11.0-20261105c-x86_64`** and reject unknown
hashes. A newer Proton Experimental build is not automatically compatible.

The supplied **TTWVR 1.1.3** build is now installed in the tested Fluorine instance
with the adapted addon. The update preserved settings and saves; isolated GPU
checks passed for its revised menu, stereo NR/DLAA and MSAA 4×. Physical-headset
confirmation of 1.1.3 remains pending. [Upgrade steps and evidence](docs/ttwvr-1.1.3.md).

## What this project adds

| Component | Purpose |
|---|---|
| [Native Fluorine launcher](docs/native-fluorine.md) | Steam launch, flat/VR profiles, private save mounts and the correct WiVRn socket |
| [Native D3DCompiler 47](docs/linux-fixes.md#1-native-shader-compiler) | Fix the observed save-loading crash |
| [Private OpenXR bridge patch](docs/openxr-layers.md) | Keep gameplay visible after the VR menu |
| [DXVK eye-buffer pool patch](docs/dxvk-pools.md) | Improve performance at the original eye resolution |
| [TTWVR Rendering Addon](neural/docs/STEAM.md) | Add AA and neural rendering controls directly to **VR Settings** |

The OpenXR/DXVK overlays apply to the VR launch without replacing Steam's
installed Proton files. Runtime selection is per launch; WiVRn's default
registration stays intact.

The [bundled DXVK / DLSS 1.1.3 retest](docs/bundled-renderer-113.md) reaches NGX
evaluation but produces stale eye images under the diagnostic fence workaround.
Its higher submission rate is not a validated performance gain. The separate
[author's native Vulkan FSR experiment](docs/native-vulkan-fsr.md) now produces
complete, moving stereo images after startup and 64-bit-handle repairs, using
a scoped DXVK fence change with stock Wine. It remains outside normal launches.

## In-game rendering controls

Install the optional **TTWVR Rendering Addon** as a mod in Fluorine, with its
matching OpenXR layer and helper. TTWVR 1.1.3 places **VR Settings** on the
main/pause menu; 1.1.0 uses **Settings → VR**. The addon rows follow Resolution:

- **Anti-aliasing:** Off, native-resolution DLAA, MSAA 2×/4×/8×.
- **Neural Rendering:** enable, model A/B, processing scale, strength, tone and detail.
- **DLSS SR:** Off / Quality / Balanced / Performance, with native-size output.
- **FSR SR:** Off / Quality / Balanced / Performance, using FSR 3.1.5.
- **Frame generation:** Off / experimental stereo 2×; disabled with FSR.

NR/DLAA/SR/FG changes apply live. MSAA changes require a game restart and
exclude neural processing. SR owns temporal reconstruction and excludes NR/DLAA. The addon hides and disables the original upscaler controls.
All effects default off; final eye resolution is unchanged.
[FSR installation and validation](docs/fsr-integration.md) use the bundled AMD runtime
through the addon’s existing GPU transport; no experimental Wine fence patch is needed.

[Upgrade to TTWVR 1.1.3](docs/ttwvr-1.1.3.md) ·
**[Addon setup](neural/docs/STEAM.md)** ·
[Controls, DLAA correction and validation](neural/docs/VR-SETTINGS.md) ·
[Source, build and licensing](neural/README.md)

NVIDIA NR and DLAA remain experimental. Both eyes and consecutive gameplay
frames have been verified in isolated GPU tests. Reported DLAA softness/haze at
distance remains under investigation. **SR and FG are experimental and default off.**
NVIDIA SDKs and runtime DLLs are separate
dependencies under NVIDIA's terms; original addon code is GPL-3.0-only.

## Neural performance update

An optional GPU handoff removes CPU completion waits between the 32-bit game
and 64-bit neural helper. It preserves eye resolution and NR/DLAA settings.
In one matched simulated corridor test, NR rose from **23.5 to 38.6 FPS** and
NR + DLAA from **16.0 to 39.2 FPS** at 2944×2208 per eye. These are not headset
FPS predictions. See the [measurements, Proton research, setup and rollback](docs/proton-neural-performance.md).
The setting is experimental and defaults off pending headset validation.

**DLSS Quality, Balanced, Performance and stereo frame generation are integrated** into the addon.
See [preset controls and measured results](docs/dlss-presets.md); FG still has
an unresolved quit stall after rapid preset cycling.
Both eyes and consecutive generated submissions were verified in the isolated
120 Hz runtime. In the matched corridor, native rendering was **58 FPS**, SR
alone **48 FPS**, and SR + FG **38 real + 38 generated pairs/s**. About 17 further
submissions per second were repeats. SR has not demonstrated a performance win
over native rendering here; headset latency and moving-head quality are untested.
[Measurements and limitations](docs/sr-frame-generation.md) ·
[Implementation, dependencies and controls](neural/docs/SR-FRAME-GENERATION.md).

## Tested status

The Linux patches produced clear, smooth Quest 3 gameplay at the original
**2944×2208 per eye**, with a **59.8 FPS capture at 120 Hz** on an RTX 5090 and
Ryzen 9 5950X. That earlier measurement used Windows MO2; it is a scene snapshot,
not a new Fluorine benchmark. The later Fluorine migration was functionally
confirmed, without a new headset frame-time measurement.

120 Hz describes headset refresh, not 120 game FPS. Other hardware, sustained
outdoor performance, complete controller/Pip-Boy checks and the final 90 Hz
configuration still need validation. The addon keeps TTWVR's built-in DLSS/FSR
path disabled. The separate [native Vulkan FSR experiment](docs/native-vulkan-fsr.md)
has simulated-headset validation; the author's DLSS result is still unresolved.

[Tested versions and evidence](docs/compatibility.md) · [Changes](CHANGELOG.md)

## Legacy setup and development

The previous Windows MO2 installation, Steam commands and addon instructions
are preserved in the **[MO2 archive](docs/archive/mo2/README.md)**. Its launcher
support remains available for existing installations. The shared wrapper keeps
its historical `steam_mo2.py` filename, including when Fluorine calls it.

- [Patch investigation](docs/investigation.md) and [original installation record](docs/installation-record.md)
- [Performance evidence](docs/dxvk-pools.md) and [upscaler experiments](docs/performance.md)
- [Simulated headset testing](docs/simulated-vr.md)
- Launcher checks: `python3 -m unittest discover -s tests -v`
- [Neural build and GPU validation](neural/README.md)
