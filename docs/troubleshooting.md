# Troubleshooting the Fluorine setup

[← Installation](installation.md) · [Fluorine configuration](native-fluorine.md) ·
[Tested versions](compatibility.md)

| Symptom | What to check |
|---|---|
| A window still says “ModOrganizer” | Fluorine's native binary is `ModOrganizer-core`; inherited names are normal. The primary launch starts `fluorine-manager`, not Windows `ModOrganizer.exe`. |
| TTW game plugin does not exist | Close Fluorine. The instance's `ModOrganizer.ini` needs `gameName=TTW` under `[General]`. |
| Wine registry game-path mismatch | Verify Fluorine uses the intended game copy and private prefix. Accept the update only when both paths are correct. |
| Missing `GECKCustom.ini` / `GECKPrefs.ini` | Accept the manager's copy prompt; these are editor INIs. |
| Wrong game or prefix opens | Verify both instance settings and the dedicated global `fluorine/config.json`; global prefix/Proton settings take precedence. Use the same XDG directories every time. |
| WiVRn socket unavailable, `/run/user/0` appears | Launch through `tools/launch_fluorine.py`; its private save-mount adapter preserves the desktop UID. See [save mounts](native-fluorine.md#save-mount-compatibility-fix). |
| Steam runtime missing in a secondary library | Set `--steam-runtime` to Steam Linux Runtime 4 in the executable's Wrapper options. |
| Manager opens but no game starts | Remove `--manager-only` for automatic launch. The selected executable must be named `Tale of Two Wastelands`, or supply its actual name with `--executable`. |
| ShowOff or another utility is missing/outdated | Finish the guide's utility downloads, including separate INI/preset files; rerun ML Utilities Checker. |
| Crash loading a save in `libvkd3d-shader` | Install both native compiler DLLs in Fluorine's prefix and set `native_d3dcompiler47=true` in `instance/ttwvr-linux.ini`. |
| Menu works but headset goes black in gameplay | Check the OpenXR bridge patch, supported Proton version and instance INI. |
| `OpenXR bridge hash mismatch` / `DXVK original hash mismatch` | Installed Proton differs from the supported build. Revalidate the patches; do not remove the checks. |
| `DXVK candidate hash mismatch` | Rebuild from the pinned source and regenerate the local hash manifest. |
| Flat launch waits for VR | Close Fallout and Fluorine, then launch with `--profile "TTW - Flat"`. Keep that exact profile name and disable TTWVR in it. |
| VR launch has no runtime | Connect WiVRn first. Check the wrapper's `--xr-runtime` and `--xrizer` paths. |
| Manager waits for `FalloutNV.exe` to close | Quit Fallout normally first. An open game keeps its FUSE mods and profile-save mount in use. |
| Low FPS after installing the fixes | Confirm both DXVK paths in `instance/ttwvr-linux.ini`. The earlier Quest 3 capture used 120 Hz; it was not a Fluorine benchmark. |
| 90 Hz gives about 45 FPS | Possible at the observed frame times; refresh rate does not impose a universal half-rate cap. |
| Original DLSS/FSR controls are missing or disabled | The addon hides/disables the stock path and provides DLAA/MSAA and NR. The changed upstream 1.1.3 host has not been revalidated; leave the old renderer/fence experiments disabled. |
| Rendering controls absent from VR Settings | Check the [addon installation](../neural/docs/STEAM.md), enabled Fluorine mod, matching layer/helper, private 32-bit registration and `neural_rendering=true`. Verified TTWVR 1.1.0 and the supplied 1.1.3 build are supported; 1.1.3 needs the updated layer and startup addon together. |
| MSAA choice says `(restart)` | Quit and relaunch Fallout. MSAA changes require a restart, including returning to Off/DLAA; active MSAA prevents NR/DLAA processing. |
| DLAA blurs after movement | Use the current layer/helper containing the [jitter correction](../neural/docs/VR-SETTINGS.md#dlaa-correction). The user still reports softness/haze at distance; this remains under investigation. Compare AA Off or MSAA. Updating TTWVR is not a demonstrated fix for this. |

## Useful logs

- Proton: `steam-22380.log` in the game wrapper's `--state-dir` directory.
- Fluorine: the dedicated instance's logs; check its prepared executable, prefix and FUSE launch.
- TTWVR/DXVK: the managed prefix under `drive_c/users/steamuser/Documents/My Games/FalloutNV/`.
- Addon: the private layer/helper runtime directory; feature evaluation and actual output application matter more than a loaded DLL.
- For an FPS overlay, put `env DXVK_HUD=fps,frametimes` before `python3` in the native Steam command.

The launcher's `--dry-run` shows the selected manager, instance, profile and XDG
directories without launching or writing files. Wrapper options are configured
inside Fluorine; Steam Launch Options use `launch_fluorine.py`.

## Undo a fix

Close Fallout and Fluorine first. In `instance/ttwvr-linux.ini`, remove
`openxr_bridge` to disable the bridge patch, or remove **both** `dxvk_renderer`
and `dxvk_syswow64` to disable the performance patch. Relaunch through the native
entry point. Steam's installed Proton files need no restoration.

For the compiler, remove `native_d3dcompiler47`, restore the two backed-up DLLs
in the managed prefix, and refresh the private `syswow64` copy if retaining the
DXVK overlay. Removing a fix may restore its original failure.

For the addon, follow its [rollback steps](../neural/docs/STEAM.md#rollback).
For a full return to the old Windows manager, use the
[migration/rollback notes](native-fluorine.md#migrate-an-existing-mo2-installation)
and [archived MO2 instructions](archive/mo2/README.md).
