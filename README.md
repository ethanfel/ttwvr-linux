# TTWVR on Linux

Linux compatibility patches for **Tale of Two Realities / TTWVR**, using native
**Fluorine**, **Proton** and **WiVRn**. The focus is the original mod's renderer,
VR settings and controls, including its **DLSS and FSR upscalers**.

**[Prepare your install → apply the Linux patch → launch](docs/installation.md)**

The setup tool checks your prepared installation, builds private dependencies,
creates a separate patched TTWVR mod, configures Fluorine and generates a
launcher with rollback. You supply the games, TTW, required utilities and the
original TTWVR archive. No game, mod or NVIDIA DLLs are redistributed here.

```sh
# Copy tools/setup.example.json to setup.json and fill in your paths first.
python3 tools/setup_linux.py check setup.json
python3 tools/setup_linux.py apply setup.json
```

The tool prints your direct launch command, Steam launch options and rollback
command. Run as your desktop user, with Fallout and Fluorine closed for apply.
[What it changes and how to undo it](docs/linux-fixes.md).

## Supported path

| Feature | Implementation / status |
|---|---|
| Native mod management | Fluorine 0.4.1, FUSE; keeps the prepared TTW profiles |
| Game renderer | Author's bundled Vulkan/DXVK renderer |
| DLSS | Author's **64-bit D3D11 NGX helper**, with repaired shared output and GPU fences |
| FSR | Author's **native Vulkan FSR 3.1.4** in the 32-bit game |
| VR menu and resolution | Original TTWVR controls; existing calibration/settings preserved |
| TTWVR versions | Exact supplied 1.1.3 and 1.1.4 builds; unknown files rejected |
| Proton | Exact `experimental-11.0-20261105c-x86_64` files; rolling updates require revalidation |
| Custom NR / DLAA / SR / FG | [Archived source and evidence](archive/rendering-addon/README.md); excluded from this setup |

DLSS is not a direct Vulkan NGX implementation: Vulkan renders the game and
its inputs are processed by the author's D3D11 helper. The previous custom
D3D12 processing helper is unnecessary for this path. The upstream archive
may still contain unused D3D12 dependencies; this tool retains its contents.

## Evidence and limits

The 1.1.3 author path passed consecutive stereo and movement checks for DLSS
Quality/Balanced/Performance, native FSR and resolution changes. The user
confirmed good physical WiVRn output at **200% / DLSS Quality**.
The [1.1.4 port](docs/ttwvr-1.1.4.md) passed isolated DLSS Quality at 200% and
FSR Quality at 100%, including both eyes, consecutive frames and actual
successful NGX evaluations. Physical 1.1.4 confirmation remains separate.

[DLSS diagnosis, captures and measurements](docs/author-dlss.md) ·
[Native Vulkan FSR repairs](docs/native-vulkan-fsr.md) ·
[Tested hardware and remaining checks](docs/compatibility.md)

This is an exact-build compatibility project. Full feature parity is the goal;
voice, multiplayer, every menu setting, other GPUs and long sessions have not
all been validated. Simulated-headset FPS is not a WiVRn streaming benchmark.

## Reference

- [Preparation and patch tool](docs/installation.md)
- [Fluorine configuration and migration](docs/native-fluorine.md)
- [Troubleshooting and rollback](docs/troubleshooting.md)
- [Discord resource post](docs/discord-resource-post.txt)
- [Supplied CachyOS OpenXR build: isolated validation](docs/cachyos-openxr-validation.md)
- [Changes](CHANGELOG.md)
- [Archived custom rendering addon](archive/rendering-addon/README.md)
- [Archived Windows MO2 path](docs/archive/mo2/README.md)

TTWVR belongs to its author. Follow the original mod's distribution terms.
The archived addon remains **GPL-3.0-only**, with NVIDIA SDKs/runtime DLLs
separate under NVIDIA's terms. Keep those notices when reusing its source.
