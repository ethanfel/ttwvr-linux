# What the Linux patch changes

Use the [prepared-install tool](installation.md#4-check-and-apply) for the current
setup. These fixes work together; the tool installs and rolls them back as one
configuration. No installed Proton library is overwritten.

| Fix | Purpose | Location |
|---|---|---|
| Native D3DCompiler 47, 32/64-bit | Avoid the observed save-load shader compilation crash | Private prefix, original files/links backed up |
| Hash-guarded OpenXR bridge | Correct projection-first layer allocation that caused black headset output | Private `wineopenxr.so`, game-only bind overlay |
| 32 MiB DXVK mapped pool | Avoid repeated allocations for large 32-bit eye buffers | Private source-built DXVK |
| DXVK opaque shared timelines | Preserve GPU fence handoff for this author's increasing timeline values | Same private DXVK build; not a general rewindable D3D fence implementation |
| Author renderer/Wine loader | Activate bundled Vulkan renderer and resolve `winevulkan.dll` | New patched TTWVR mod copy |
| Shared DLSS output | Use full-size shared output when direct headset images cannot be imported | Same TTWVR copy; original D3D11 helper retained |
| Native Vulkan FSR | Initialize embedded FSR and preserve full-width Vulkan handles in the 32-bit game | Same TTWVR copy |
| Native Fluorine save mount | Preserve desktop UID so the game can reach WiVRn | Launch-scoped `unshare` adapter |

The generated `instance/ttwvr-linux.ini` selects `author_renderer=true`,
`neural_rendering=false`, WoW64, native shader compilers and private overlay
paths. The wrapper enables NVAPI and disables the archived neural layer for
this VR path. The original TTWVR menu remains in use.

[Compiler evidence](simulated-vr.md) · [OpenXR bridge](openxr-layers.md) ·
[Pool diagnosis](dxvk-pools.md) · [Native FSR/fences](native-vulkan-fsr.md) ·
[DLSS output diagnosis](author-dlss.md) · [1.1.4 exact-build port](ttwvr-1.1.4.md)

## Dependency build

`setup_linux.py prepare setup.json` builds DXVK revision
`cf0a0d90f8965147741ad17dcd0b263817572ab3` with both repository source patches,
using the upstream 32-bit cross file. It records patch and output hashes.
The normal launcher overlays its three DLLs on Proton's 32-bit source directory
and a complete private `syswow64` copy on the prefix. This lets Proton populate
that copy without unlinking mounted individual DLLs or touching installed files.

Fluorine itself must start outside bubblewrap. Its FUSE mount is prepared first;
only the game process receives these overlays. Mod assets must be regular files
or reflinks, since the tested Fluorine catalog skips symlinks.

## Undo a fix

Use the [recorded rollback command](installation.md#6-roll-back). It restores
backed-up settings and compiler files before the next launch. Do not combine
half of the author-renderer fixes with a different renderer/fence setup.
The older manual pool-only procedure remains documented as historical evidence
in [DXVK pools](dxvk-pools.md); it is not the complete current setup.
