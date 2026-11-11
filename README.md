# TTWVR on Linux with WiVRn

Run **Tale of Two Wastelands** in VR using KUPOkinz's **Tale of Two Realities**,
Proton and WiVRn. This repository provides the Linux launcher and compatibility
patches; download the games and mods from their authors.

**Working on Quest 3:** clear, smooth gameplay at the original 2944×2208 eye
resolution, with a **59.8 FPS capture at 120 Hz**. Tested with an RTX 5090 and
Ryzen 9 5950X. This is an initial gameplay result, not a sustained benchmark.

## Start here

1. Install English **Fallout: New Vegas Ultimate Edition** and **Fallout 3 GOTY**.
2. Set up TTW and its utilities in Mod Organizer 2; check that flat gameplay works.
3. Install TTWVR in a separate VR profile.
4. Apply the three Linux fixes and configure the Steam launcher.
5. Connect WiVRn and launch the VR profile. The tested Quest 3 setup uses 120 Hz.

**[Follow the installation guide →](docs/installation.md)**

The patches currently require **Proton Experimental
`experimental-11.0-20261105c-x86_64`**. They verify file hashes and reject other
builds. Selecting today's Proton Experimental does not guarantee a match.

## Problems resolved

| Problem | Fix |
|---|---|
| Crash when loading a save | [Native D3DCompiler 47](docs/linux-fixes.md#1-native-shader-compiler) |
| Menu visible, then a black headset image | [Private Proton OpenXR bridge patch](docs/openxr-layers.md) |
| Poor FPS at full resolution | [DXVK eye-buffer memory reuse](docs/dxvk-pools.md) |
| WiVRn runtime missing inside Steam's container | Launcher handles OpenXR runtime import |
| Flat mode hangs without a connected headset | Separate flat launch mode |
| MO2 says the TTW game plugin is missing | Use the internal game identifier `TTW` |

The OpenXR and DXVK patches apply only to the VR launch. Steam's installed
Proton files stay unchanged.

## Known limits

- **DLSS is not working.** The successful setup preserves full resolution.
- **120 Hz is the headset refresh rate, not 120 game FPS.** At 90 Hz, current
  frame times may lead to about 45 FPS; a physical 90 Hz retest is still pending.
- Other hardware, full controller coverage and long sessions remain unverified.

[Troubleshooting](docs/troubleshooting.md) ·
[Tested versions and status](docs/compatibility.md) ·
[Changes](CHANGELOG.md)

<details>
<summary>Technical records and developer checks</summary>

- [Installation record](docs/installation-record.md) and [investigation history](docs/investigation.md)
- [OpenXR regression test](docs/openxr-layers.md#verification)
- [Performance measurements](docs/dxvk-pools.md) and [DLSS investigation](docs/performance.md)
- [Simulated headset testing](docs/simulated-vr.md)

Run launcher checks from the repository: `python3 -m unittest discover -s tests -v`.
The optional audit and profile tools describe their inputs with `--help`.

</details>
