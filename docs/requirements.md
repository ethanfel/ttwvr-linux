# Downloads and Linux prerequisites

[← Preparation](preparation.md) · [Patch installation](installation.md)

## Games and mods

| Download | Where to get it | What to use |
|---|---|---|
| Fallout: New Vegas Ultimate and Fallout 3 GOTY | Steam | English versions, all DLC. |
| TTW installer package and YUPTTW | [The Best of Times: TTW](https://thebestoftimes.moddinglinked.com/ttw.html) | Extract the package; the Linux converter reads its `.mpi` file. |
| Native TTW converter | [MPI Installer download](https://www.nexusmods.com/site/mods/1657), [author's instructions](https://github.com/SulfurNitride/TTW_Linux_Installer) | `mpi_installer_gui`. |
| TTW utilities and INI presets | [The Best of Times: Essential Mods](https://thebestoftimes.moddinglinked.com/essentials.html) | Include the separately listed INI downloads. |
| FNV4GB for Proton | [Nexus files](https://www.nexusmods.com/newvegas/mods/62552?tab=files) | The **Proton** download; tested version 1.5. |
| Fluorine | [Official 0.4.1 release](https://github.com/SulfurNitride/Fluorine-Manager/releases/tag/v0.4.1) | Native Linux x86_64 archive. |
| TTWVR | [Author's download thread](https://discord.com/channels/747967102895390741/1560413111423344730), [community invite](https://discord.com/invite/ttwvr) | Complete original **1.1.5** archive; exact supplied **1.1.4** and **1.1.3** are also supported. |
| WiVRn | [WiVRn](https://github.com/WiVRn/WiVRn) | Install the desktop server and headset client; verify they connect. |
| xrizer | [Download and installation](https://github.com/Supreeeme/xrizer#download--usage) | Extract the runtime; note its folder for setup.json. Our launcher selects it without changing global runtime registration. |

## Proton

Use the supplied **proton-cachyos-wineopenxr-fix.tar.gz** from [this Discord source channel](https://discord.com/channels/1570233209462194208/1570481280825561098). Discord access is required.

1. Extract the **whole archive** into Steam's `compatibilitytools.d` directory so Fluorine can find it.
2. Check the resulting folder contains `proton` and `version`.
3. Select it in Fluorine when preparing the instance.

For a default Steam installation, the resulting path is typically:

```text
~/.local/share/Steam/compatibilitytools.d/proton-cachyos-wineopenxr-fix/
```

| Supported build | OpenXR handling |
|---|---|
| `cachyos-11.0-20261015-slr` from the supplied archive | Uses its built-in OpenXR fix. |
| `experimental-11.0-20261105c-x86_64` | The tool supplies a private bridge workaround. |

Install **Steam Linux Runtime 4** from Steam's Library with **Tools** visible. Locate its folder with **Properties → Installed Files → Browse**. In setup.json, `steam_library` is the parent of `steamapps`, not the runtime folder itself.

The tool verifies exact runtime files. A newer Proton release or matching folder name is insufficient. Keep each complete build together; do not mix Wine DLLs or Unix libraries from different builds.

<details>
<summary>Archive checksum and provenance</summary>

Tested CachyOS archive SHA256:

```text
0e6f57024d5577fcb70bf973f0416a876e0a6215247de7c905be35e3761e3642
```

The channel link was supplied by the user. An upstream source commit for this archive has not been established. See [the runtime validation](cachyos-openxr-validation.md) for evidence and measured limits.

</details>

## Linux packages

Install these using your distribution's package manager. Package names vary; the patch tool reports missing commands during `check`.

| Purpose | Required programs |
|---|---|
| Run the tool | Python **3.11+**, Git |
| Mount the mods and private runtime | `bwrap` (bubblewrap), `fusermount3` (FUSE 3), `unshare` |
| Build the renderer | `meson`, `ninja`, `glslangValidator` |
| Build Windows 32-bit DLLs | `i686-w64-mingw32-gcc`, `i686-w64-mingw32-g++`, `i686-w64-mingw32-windres`; use the **POSIX threads** toolchain |

Use working Vulkan drivers with the required 32-bit libraries. DLSS requires a supported NVIDIA RTX GPU. Current DLSS and FSR validation used an RTX 5090; [other-hardware limits](compatibility.md) remain documented separately.

Leave space for a separate New Vegas copy, private prefix, TTW conversion output, mod copies and several GB of build files. Run the patch tool as your desktop user, without sudo.
