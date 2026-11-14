# TTWVR on Linux

Play **Tale of Two Wastelands in VR** using **Tale of Two Realities / TTWVR**, native **Fluorine**, **Proton** and **WiVRn**. These patches enable the mod's original renderer, **DLSS**, **FSR**, VR settings and controls on the tested Linux setup.

## Start here

| Your starting point | Guide |
|---|---|
| I have the Steam games and need to install TTW | [Prepare TTW in Fluorine](docs/preparation.md) |
| TTW already works in native Fluorine | [Apply the Linux patch](docs/installation.md) |
| I already applied this patch | [Launch options, updates and rollback](docs/maintenance.md) |

The standard setup file has three entries:

```json
{
  "workspace": "~/Games/ttwvr",
  "manager": "apps/fluorine-manager/fluorine-manager",
  "steam_library": "~/.local/share/Steam"
}
```

The tool reads Proton and the prefix from Fluorine and finds the enabled TTWVR mod. It checks compatibility, builds the required dependencies, creates a patched mod copy and generates your Steam launch command. Original mods and saves are retained, with backups for rollback.

You supply the games and mod downloads. No game, TTWVR or NVIDIA DLLs are redistributed here.

## Supported setup

| Component | Supported / tested |
|---|---|
| Mod manager | Native Fluorine **0.4.1**, FUSE backend |
| TTWVR | Exact supplied **1.1.5**, **1.1.4** and **1.1.3** archives |
| Proton | Supplied CachyOS WineOpenXR-fix build or exact Experimental; [downloads and versions](docs/requirements.md#proton) |
| Upscaling | Author's DLSS helper and native Vulkan FSR |
| Headset connection | WiVRn; validation used Quest 3 and RTX 5090 |

Both-eye rendering, consecutive frames and movement were checked. The user confirmed good headset output with the author renderer and later played 1.1.4 through CachyOS/WiVRn with NMC. The [1.1.5 update](docs/ttwvr-1.1.5.md) passed isolated DLSS/FSR GPU checks; physical 1.1.5 confirmation remains pending. Other GPUs, all mod features and long sessions have not all been validated.

[Compatibility and limits](docs/compatibility.md) ·
[DLSS evidence](docs/author-dlss.md) · [FSR evidence](docs/native-vulkan-fsr.md)

## Help and reference

- [TTWVR Discord community](https://discord.com/invite/ttwvr)
- [Troubleshooting](docs/troubleshooting.md)
- [Optional setup.json fields](docs/configuration.md)
- [What the Linux patches change](docs/linux-fixes.md)
- [Fluorine technical reference](docs/native-fluorine.md)
- [Discord resource post](docs/discord-resource-post.txt)
- [Changes](CHANGELOG.md)
- Archives: [custom rendering addon](archive/rendering-addon/README.md), [Windows MO2 workflow](docs/archive/mo2/README.md)

## License

This project's original code, including the Linux tools, tests and patch
contributions, is licensed under **GPL-3.0-only**; see [LICENSE](LICENSE).
The archived addon uses the same license. Third-party components retain their
licenses and attributions; see [THIRD-PARTY-NOTICES.md](THIRD-PARTY-NOTICES.md).

TTWVR belongs to its author. Follow the original mod's distribution terms.
NVIDIA SDKs and runtime DLLs remain separate dependencies under NVIDIA's terms.
