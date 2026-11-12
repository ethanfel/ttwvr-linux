# Install the Rendering Addon in Fluorine

[← Addon source and build](../README.md) · [Native TTWVR setup](../../docs/installation.md) ·
[Controls and validation](VR-SETTINGS.md)

This installs **TTWVR Rendering Addon** into the native Fluorine **TTW - VR**
profile. Start with a working [Fluorine/Proton/WiVRn installation](../../docs/installation.md).
The Steam entry point remains `tools/launch_fluorine.py`; no new Steam command
is needed. The isolated development setup is separate and does not enable the
addon in a regular installation automatically.

The addon provides **VR Settings** controls for DLAA, MSAA, NR, DLSS SR and experimental stereo FG. It requires
the matching x86 OpenXR layer and x64 helper even if you start by selecting MSAA.
Obtain NVIDIA SDKs/runtime DLLs separately for neural processing; they are not
bundled with this GPL-3.0-only addon.

## Install and opt in

1. Close Fallout and Fluorine. Back up the VR profile mod list,
   `instance/ttwvr-linux.ini` and private-prefix registry before changes.
   Do not replace loaded DLLs or edit a Wine registry while that prefix is in use.
2. [Build the addon](../README.md#build) and place the validated x86 layer, x64
   helper, runtime INI and separately obtained NVIDIA dependencies in a dedicated
   directory such as `ttwvr/neural-runtime`. Follow the
   [file layout and registration notes](../README.md#installation-and-testing).
   Disable captures/probes, set `transport_identity=0` and
   `experimental_temporal=1`, and leave NR/DLAA/SR/FG `enabled=0` initially.
3. Build the x86 `rendering_addon` target:

   ```sh
   cmake --build /path/to/build32 --target rendering_addon
   ```

   Copy the resulting **TTWVR Rendering Addon** folder into Fluorine's
   `instance/mods` directory. Enable it in **TTW - VR**, disabling the older
   **TTWVR Neural Menu** mod if present. The new folder includes the xNVSE
   startup plugin; scripts alone cannot enable MSAA. No ESP or save changes
   are needed.
4. Copy the [opt-in manifest](../manifest/XR_APILAYER_TTWVR_neural_rendering.opt-in.json)
   beside the layer DLL. Register its absolute Windows path as a DWORD value
   of `0` in the **32-bit**
   `HKLM\SOFTWARE\Khronos\OpenXR\1\ApiLayers\Implicit` registry key of
   **the prefix configured in Fluorine**. This may differ from Steam's original
   `compatdata/22380/pfx`. Do not register the Windows layer with the native
   Linux OpenXR runtime. The manifest also requires
   `ENABLE_TTWVR_NEURAL_RENDERING` in the launching process environment; see
   the [OpenXR loader rules](https://github.com/KhronosGroup/OpenXR-SDK-Source/blob/main/specification/loader/api_layer.adoc).
5. Add this setting to **`instance/ttwvr-linux.ini`**, beside the instance's
   `ModOrganizer.ini`, retaining its existing VR patch settings:

   ```ini
   [VR]
   neural_rendering=true
   ```

Fluorine's configured `steam_mo2.py --native-instance ...` game wrapper enables
the manifest and NVIDIA GPU/API visibility for opted-in VR launches. The
filename is historical; this mode preserves Fluorine's FUSE/Proton command and
does not launch Windows MO2. Flat launches do not enable this opt-in manifest.

## Use the controls

Launch from Steam as usual and open **VR Settings**. The new rows follow
Resolution. Each row saves immediately; NR/DLAA/SR/FG and tuning apply live during
a brief bypass while the helper rebuilds. **MSAA requires restarting Fallout**;
pending selections say `(restart)`, including switching back to Off/DLAA.
MSAA currently excludes neural processing. Final eye resolution remains unchanged.

Enabling `neural_rendering=true` loads the addon; it does not automatically
turn NR or DLAA on. Select the desired AA mode and NR settings in the game.
See [the exact controls, compatibility and test evidence](VR-SETTINGS.md).

See [SR/FG dependencies, controls and limits](SR-FRAME-GENERATION.md).

## Rollback

Close Fallout and Fluorine, set `neural_rendering=false` in the instance INI,
and disable **TTWVR Rendering Addon** in the VR profile. To remove it entirely,
also remove its one registry value and dedicated runtime directory, preserving
other OpenXR layer registrations. `DISABLE_TTWVR_NEURAL_RENDERING=1` remains
an emergency launch bypass. The Steam entry point does not need changing.

## TTWVR 1.1.3 upgrade, 2026-11-12

The matching addon and layer from `4feb91a` are now installed in the regular
Fluorine VR profile alongside the supplied TTWVR 1.1.3 build. Existing settings
and saves were preserved; no Steam-command change was required. The menu is now
**VR Settings** directly on the main/pause menu. See the
[upgrade, rollback and validation record](../../docs/ttwvr-1.1.3.md).

## Initial native addon deployment, before the 1.1.3 upgrade

The verified x86 layer, x64 helper and `TTWVR Rendering Addon` mod were installed
in the regular native Fluorine instance with all game/helper/manager processes
idle. The older `TTWVR Neural Menu` mod was disabled in `TTW - VR`. Previous
binaries, runtime INI and mod list were backed up before replacement.

All **18 profile save/companion files** retained their hashes. The runtime INI
also retained its exact hash: NR/DLAA were off at that initial deployment, with
model B/25%, strength 20% and tone/detail 100% preserved. The addon initially
started with MSAA off. No captures,
simulated-pose control or test automation were installed. Steam command,
OpenXR registration, NVIDIA dependencies and WiVRn's default runtime stay as
configured. Select the desired AA mode in VR Settings on the next launch.
The DLAA correction still needs confirmation in the user's physical headset.

## Historical Windows MO2 deployment

The earlier 2026-11-11 installation and its Windows-manager instructions are
preserved in the [MO2 addon archive](../../docs/archive/mo2/neural-addon.md).
Use the native steps above for current setup.
