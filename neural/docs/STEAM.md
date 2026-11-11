# Use the neural menu from the regular Steam VR launch

The isolated prototype installs its menu, layer registration and NVIDIA runtime
files only in the copied setup. The ordinary Steam command still points at the
original MO2 and prefix, so it does not automatically acquire those additions.

After validating in a separate setup, opt an existing VR installation in:

1. Close Fallout and MO2. Back up the target profile mod list, compatibility INI
   and Wine registry before changing them. Never overwrite loaded binaries or
   edit a Wine registry file while its prefix is in use.
2. Put the validated x86 layer, x64 helper, INI and separately obtained NVIDIA
   runtime DLLs in a dedicated directory accessible to the Steam container, as
   described in the [installation notes](../README.md#installation-and-testing).
   Disable captures/probes, set `transport_identity=0` and
   `experimental_temporal=1`, and leave NR/DLAA `enabled=0` initially.
3. Copy [`menu/`](../menu/) into an MO2 mod and enable it in the **VR profile**.
   No ESP or save changes are needed.
4. Copy the [opt-in manifest](../manifest/XR_APILAYER_TTWVR_neural_rendering.opt-in.json)
   beside the layer DLL. Register its absolute Windows path as a DWORD value of
   `0` in the target prefix's **32-bit**
   `HKLM\SOFTWARE\Khronos\OpenXR\1\ApiLayers\Implicit` registry key.
   Do not register it with the native Linux runtime. This manifest additionally
   requires `ENABLE_TTWVR_NEURAL_RENDERING` in the launching process environment;
   see the [OpenXR loader's layer rules](https://github.com/KhronosGroup/OpenXR-SDK-Source/blob/main/specification/loader/api_layer.adoc).
5. Add this setting to `ttwvr-linux.ini` beside the target `ModOrganizer.exe`,
   retaining its existing VR settings:

   ```ini
   [VR]
   neural_rendering=true
   ```

The existing `steam_mo2.py` launch command can stay unchanged. For opted-in VR
launches, the wrapper enables this manifest and NVIDIA API/GPU visibility.
Flat launches and configurations without this setting do not enable the opt-in
manifest. This setting loads the addon; the in-game NR and DLAA switches remain
independent. Open the pause menu and press X, or press F10 with Fallout focused.
Settings take effect after Apply without restarting Fallout.

Rollback: close Fallout/MO2, set `neural_rendering=false` and disable the menu mod
in the VR profile. To remove the addon entirely, also remove its one registry
value and dedicated directory. Keep the prefix's other layer entries intact.
`DISABLE_TTWVR_NEURAL_RENDERING=1` remains an emergency launch bypass.

## Local deployment record, 2026-11-11

The validated addon was copied to a dedicated regular-installation directory and
the menu enabled in `TTW - VR`. The target prefix was verified idle before its
32-bit registration was added; registry/profile/compatibility backups were kept.
The existing Steam launch option passed a read-only dry run with the regular
MO2, WiVRn and neural opt-in. All eight game save hashes were unchanged. No
simulated-headset variables or test automation were deployed. NR/DLAA and capture
probes start off; the user's existing B/25%, strength 20% tuning was retained.
The 12 launcher tests passed, including opt-in, default-off and flat-mode checks.
This is installation validation, not a new physical-headset rendering result.
