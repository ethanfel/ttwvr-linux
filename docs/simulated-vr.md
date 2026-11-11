# Simulated TTWVR investigation — 2026-11-11

Builds and binary hashes are recorded in [compatibility.md](compatibility.md).
Tests use Proton Experimental 11.0-100, its WoW64 mode, Steam Linux Runtime 4,
MO2 2.5.2, TTWVR 1.1.0 build D13C92BA, and the original user-supplied Monado
runtime. The runtime and global WiVRn registration have not been modified.

The user's flat-profile manual save and companion NVSE file were backed up,
then copied to the separate VR profile. Tests operate on that copy.

## Initial results

- TTWVR loads via xNVSE and selects **OpenXR** directly. xrizer is also exercised
  by Proton's earlier startup probe, but it is not the game's selected backend.
- TTWVR identifies the simulated HMD and the RTX 5090 adapter, creates an
  OpenXR session, and reports `Head tracking active (OpenXR)`.
- The spectator window renders the main menu as a panel in the VR view.
  This establishes visible menu rendering; it does not establish physical
  headset output, comfortable performance, or full gameplay.
- Proton's stock DXVK handles D3D9. The bundled `ttwvr_dxvk_d3d9.dll` is not
  loaded. DLSS/FSR initialization fails because the plugin cannot find
  `vulkan-1.dll`; stock DXVK uses Wine's `winevulkan.dll`.
- An explicit `[Stereo] bUseDXVK=1` did not change renderer selection. That
  experimental INI addition was removed, restoring the supplied INI.

## Save-load crash and compiler experiment

The second run loaded the copied save, then crashed on the main thread:

```text
exception C0000005 at libvkd3d-shader-1.dll+0x10C250
vkd3d:get_bytecode_register_type: Unreachable code reached
```

The stack passes through Wine's built-in `d3dcompiler_47.dll`, then
`TTWVR.dll+0x4C68A4`. This is a concrete shader-compiler failure, separate from
the missing custom DXVK/upscaler path.

The next controlled experiment installs native D3DCompiler 47 in this game's
prefix and sets `WINEDLLOVERRIDES=d3dcompiler_47=n` for the test launch. Both
original prefix DLLs are backed up; no registry override is added. Downloads
and hashes match the [Winetricks d3dcompiler_47 recipe](https://github.com/Winetricks/winetricks/blob/master/src/winetricks),
which obtains the DLLs from [Mozilla's fxc2 repository](https://github.com/mozilla/fxc2/tree/master/dll).

| Prefix location | SHA-256 of native DLL |
|---|---|
| `drive_c/windows/syswow64/d3dcompiler_47.dll` | `2ad0d4987fc4624566b190e747c9d95038443956ed816abfd1e2d389b5ec0851` |
| `drive_c/windows/system32/d3dcompiler_47.dll` | `4432bbd1a390874f3f0a503d45cc48d346abc3a8c0213c289f4b615bf0ee84f3` |

**Result: the copied Vault 101 save loads and the VR spectator view renders the
room.** Proton logs confirm native D3DCompiler 47 was loaded. The previous
compiler crash did not recur; the game subsequently logged a player-requested
normal exit. Loading paused the main loop for about 20 seconds, triggering the
mod's watchdog before recovering. The plugin also logged four failed eye-depth
copies for its blood effect (`0x8876086C`), and the DLSS fallback remains.
DLLs, full logs, crash dumps, and the spectator capture remain local.

## Applying the compiler workaround

Close this game's Wine processes first. Obtain the two hash-checked DLLs from
the sources above (the 32-bit source file is `d3dcompiler_47_32.dll`, the 64-bit
source file is `d3dcompiler_47.dll`). Back up the existing files, then put the
32-bit DLL in the prefix's `drive_c/windows/syswow64/d3dcompiler_47.dll` and
the 64-bit DLL in `drive_c/windows/system32/d3dcompiler_47.dll`. The directory
names are Windows' normal architecture layout, including for WoW64.

To enable the tested launch settings without editing Steam's existing wrapper
command, create `ttwvr-linux.ini` beside `ModOrganizer.exe`:

```ini
[VR]
wow64=true
native_d3dcompiler47=true
```

The wrapper applies these settings only when it selects VR mode. Existing
overrides for other DLLs are preserved. `--native-d3dcompiler47` is also
available explicitly; neither method installs the DLLs. Seven launcher tests
cover runtime import, profile selection, argument preservation, and the new
VR-only settings.

Rollback: close the game and MO2, remove the compatibility INI or set both
values to `false`, and restore the two backed-up prefix DLLs. No Wine registry
override was added. The original TTWVR binaries and INI remain unchanged.

## Custom renderer observation

Static inspection finds a guard in TTWVR's custom-renderer loader: after
`GetModuleHandleW(L"d3d9")`, it queries ordinal `0x6E61` (28257) and returns
early if that export exists. The installed Proton DXVK exports that ordinal.
This is evidence for the stock-DXVK detection bypassing the bundled renderer;
it is not evidence of an NVTF conflict. NVTF has not been modified or disabled.

At this stage no binary patch had been applied. The later physical WiVRn test
required a [private Proton OpenXR bridge workaround](openxr-layers.md) before
the user confirmed visible headset gameplay. Full controller mapping,
calibration, Pip-Boy, stereo correctness, extended play, and VR save/reload
still require validation.
