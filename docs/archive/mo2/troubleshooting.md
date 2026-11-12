> **Archived Windows MO2 workflow (2026-11-12).** Retained for existing installations and rollback.
> Use the [Fluorine installation guide](../../installation.md) for the current setup.
> [Archive index](README.md) · [Migrate to Fluorine](../../native-fluorine.md#migrate-an-existing-mo2-installation)

# Troubleshooting

[← Installation](installation.md) · [Tested versions](../../compatibility.md)

| Symptom | What to check |
|---|---|
| MO2 says the TTW game plugin does not exist | Close MO2. In `ModOrganizer.ini`, use `gameName=TTW` under `[General]`. |
| Missing `GECKCustom.ini` / `GECKPrefs.ini` message | Accept MO2's copy prompt; these are editor INIs. |
| ShowOff or another utility is missing/outdated | Complete the guide's utility downloads, including separate INI/preset files; rerun ML Utilities Checker. |
| Crash loading a save in `libvkd3d-shader` | Install both native compiler DLLs and set `native_d3dcompiler47=true`. |
| Menu works but headset goes black in gameplay | Check the OpenXR bridge patch and the supported Proton version. |
| `OpenXR bridge hash mismatch` / `DXVK original hash mismatch` | Proton files differ from the supported build. Revalidate the patches; do not remove the checks. |
| `DXVK candidate hash mismatch` | Rebuild from the pinned source and regenerate the local manifest. |
| Flat launch waits for VR | Close MO2, relaunch with `--mode flat`, and select `TTW - Flat`. |
| VR launch has no runtime | Connect WiVRn first and check `--xr-runtime` and `--xrizer` paths. |
| Low FPS after installing the fixes | Confirm both DXVK settings in `ttwvr-linux.ini`. The tested Quest 3 result used 120 Hz. |
| 90 Hz gives about 45 FPS | Possible at the current frame times; refresh rate does not impose a universal half-rate cap. |
| DLSS does not work | Known limitation. Leave the experimental renderer/fence patches disabled. |

## Useful logs

- Proton: `steam-22380.log` in the launcher's `--state-dir` directory.
- TTWVR/DXVK: the game's prefix under
  `drive_c/users/steamuser/Documents/My Games/FalloutNV/`.
- For an FPS overlay, put `env DXVK_HUD=fps,frametimes` before `python3` in
  the Steam launch option.

## Undo a fix

Close the game and MO2 first. Remove `openxr_bridge` to disable the bridge
patch, or remove **both** `dxvk_renderer` and `dxvk_syswow64` to disable the
performance patch. Restart MO2. Steam's original Proton files need no restoration.

For the compiler, remove `native_d3dcompiler47`, restore the two backed-up DLLs,
and refresh the private `syswow64` copy if retaining the DXVK overlay.
Removing a fix may bring back its original failure.
