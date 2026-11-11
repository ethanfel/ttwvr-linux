# Compatibility evidence

## Supplied build

TTWVR's `meta.ini` identifies version **1.1.0**. The ZIP contains 1,043 entries,
617,531,116 uncompressed bytes.

| Component | SHA-256 | PE machine |
|---|---|---|
| TTWVR.zip | `8cdb8ce254a27421b2b33eea77126f0ed0aa3aba94aeffbb8f13bcc82a607040` | archive |
| TTWVR.dll | `8a3a71b48d362984fe346c73b68dfbd476d6e138d6831f26f10c6f506cd03880` | i386 |
| ttwvr_dxvk_d3d9.dll | `c7f64a18da8fd5f455e9453ade4abcfaef9dec63cc1cb80a89215a12b6131cf2` | i386 |
| ttwvr_dlss_host.exe | `3952fb4f8715b98d8980b5e6f6a50429c47a90c11b95f14994a2c2b3d9dc2564` | amd64 |
| openvr_api.dll | `ab696e4f218a95b3e396bc310f9fe6485df48c99c0969762083212b1e1f025a6` | i386 |

The supplied author's guide states Windows-only support. The binary imports
`D3D11CreateDevice` and DXGI. Its strings mention custom DXVK, Vulkan shared
memory/semaphores, a 64-bit upscaling helper, and D3D11 OpenVR submission textures.
These are static observations, **not proof of a Wine incompatibility** and not
proof that the corresponding code paths execute with every configuration.

No source was provided in the ZIP. Do not replace its custom DXVK with another
VR mod's `d3d9.dll`: that would change the project under test and may break the
plugin's custom interop. Any future patch must name the failing API, include
runtime evidence, and record the exact affected build.

## Host observed

- EndeavourOS / Arch; NVIDIA 615.78.08, including 32-bit Vulkan driver support.
- WiVRn server/dashboard 26.9.
- xrizer 0.6.r5.g63fc7eb, with `bin/linux64/vrclient.so` present.
- No `bin/linux32/vrclient.so` found in that xrizer installation.
- Steam's existing New Vegas prefix was created with Proton Experimental 11.
- Proton supplies both 32-bit and 64-bit Windows VR bridge DLLs. Which native
  xrizer architecture this game needs remains to be checked at runtime.
- The original New Vegas executable does not have the Large Address Aware bit.
- The user-level OpenVR/OpenXR configuration directories were empty at inspection;
  a WiVRn compositor socket path was present. This does not establish connection
  state because the inspection did not query the running desktop services.

The installed Proton 11 script accepts `PROTON_USE_WOW64=1` and selects Wine's
WoW64 architecture for that launch. Its x86_64 Unix VR bridges contain the
`/linux64` runtime path. This is a candidate way to use the installed 64-bit
xrizer with 32-bit New Vegas; it is exposed by the wrapper's `--wow64` option.
It subsequently initialized TTWVR's direct OpenXR backend with the simulated
headset and rendered a loaded save after the compiler workaround below. No
existing prefix is deleted or recreated by the wrapper, and no global Steam
compatibility setting is changed.

## First launch sequence

The initial ordinary Steam launch on 2026-11-11 completed successfully according
to the user. The xNVSE runtime log reports version 6.4.9, `init complete`, and
`NewGameHook`; the loader log records a normal return from the game. The plugin
directory was empty in this run because MO2 was not used. This validates the
base-game launch and xNVSE autoloading, not TTW or VR initialization.

The first wrapper launch opened MO2 2.5.2 and loaded `game_ttw.dll`. MO2 rejected
the generated display name as its internal game identifier; `gameName=TTW` is
the correction. Separately, Proton's VR startup probe loaded xrizer but reported
that `/usr/share/openxr/1/openxr_wivrn.json` was unavailable inside the container.
The wrapper now supplies the host selection to pressure-vessel, then removes
`XR_RUNTIME_JSON` immediately before the nested Proton command. This allows the
OpenXR loader to use pressure-vessel's imported manifest with rewritten library
paths via `XDG_CONFIG_DIRS`. Direct Proton commands retain the original variable.
This correction has a controlled subprocess check and was subsequently verified
with native and TTWVR simulated OpenXR launches. The failing probe occurred
before any TTWVR plugin was launched.

After removing the stale manifest override, the flat launch waited on VR state
while WiVRn had no connected headset. The wrapper now detects `TTW - Flat`
(or accepts `--mode flat`), supplies an empty private OpenVR registry, removes
inherited runtime overrides, and sets DXVK's `DXVK_NO_VR=1`. MO2 and the game
subsequently opened. Restart MO2 when switching to VR so this flat-only
environment does not carry into a VR game. The environment is process-local.

The game log now confirms ShowOff 1.84 and Crash Logger 5.17 loaded successfully,
alongside JIP LN 57.30, JohnnyGuitar 5.28, Stewie Tweaks 10.05, Engine
Optimizations 1.0, and TTW NVSE 3.3.3b. After adding the remaining guide files,
all 16 utility DLLs reported successful loading. The user supplied a screenshot
of ML Utilities Checker's **All utilities installed!** result, confirming its
plugin checks, the LAA patch, and the custom INI's `iNumHWThreads=3` value.
The temporary checker was disabled after that successful test.
The user then confirmed that a new game reached TTW's intro scene. The runtime
log records `NewGameHook`. Flat-mode startup and initial gameplay are verified;
extended gameplay, save/load, and VR remain separate checks.

1. Complete TTW conversion and the official dependencies; verify TTW in flat mode.
2. Add the supplied TTWVR as the final asset override in a dedicated MO2 profile.
3. Start WiVRn and connect the headset. Launch through Steam's Proton environment.
4. Inspect xNVSE/plugin logs and TTWVR/DXVK/Proton logs. Check the actual VR bridge
   architecture before assuming a 32-bit runtime library is necessary.
5. Verify initialization, stereo output, tracking, controller input, calibration,
   Pip-Boy, loading a new game, and saving/reloading.
6. Only after a failure is reproduced, change one component or option at a time.
   Preserve original files and record checksums, diffs, and rollback instructions.

## Simulated-headset preflight

The user supplied an existing Monado runtime built from source revision
`3acbda6b7dacbe014749020e8543fded54ef076b` in the TF2VR autotest directory. It
is reusable independently of that game. Its intended native-app environment is:

```sh
XR_RUNTIME_JSON=/path/to/monado-build/openxr_monado-dev.json
SIMULATED_ENABLE=1
XRT_COMPOSITOR_NULL=1
XRT_COMPOSITOR_NULL_FPS=90
```

The original runtime was not changed. In the restricted agent process, the
provided OpenXR preflight failed at libusb's udev hotplug-monitor initialization
(`XRT_ERROR_PROBER_CREATION_FAILED`, followed by `XR_ERROR_RUNTIME_UNAVAILABLE`).

A separate build from the same source disabled `XRT_HAVE_LIBUSB`,
`XRT_HAVE_LIBUDEV`, `XRT_HAVE_HIDAPI`, and `XRT_HAVE_INTERNAL_HID`. Configuration
also needed `PYTHONPATH` unset because an inherited Resolve scripting path
contained a leading colon that broke Monado's CMake generator expressions.

That build successfully created the OpenXR instance, selected a simulated HMD
with two views, and started a 90 Hz null compositor. It then failed Vulkan
initialization. A standalone `vulkaninfo --summary` in the same restricted
process could not load the NVIDIA ICD and reported no Vulkan drivers. Therefore
this is not evidence of a TTWVR failure or of a broken host driver. Neither
preflight reached a successful rendering session, and a host-side test remains
necessary.

After desktop restrictions were removed, the **original user-supplied runtime**
passed the same native OpenXR preflight with `result=0`, a simulated HMD, and two
640x720 views. This confirms native runtime/system initialization on the host;
it does not establish a TTWVR frame submission or controller test. Repeating
that native preflight inside Steam Linux Runtime 4, with runtime import enabled
and the host override cleared inside the container, also returned `result=0`.
With `SIMULATED_LEFT=wmr SIMULATED_RIGHT=wmr`, it also selected both simulated
WMR controllers. A local game-test launcher is prepared with a separate
controller input file, the `TTW - VR` profile, and optional Proton WoW64 mode.

## TTWVR simulated save-load result

The launcher subsequently initialized TTWVR's **direct OpenXR** backend,
reported active head tracking, and rendered its main menu. A copied Vault 101
save then exposed a crash in Wine's built-in D3DCompiler 47 / libvkd3d shader
compiler. Native D3DCompiler 47 resolved that observed crash: the save loaded,
the spectator view rendered the room, and the game later exited normally.

See [the detailed test, workaround, hashes, and rollback](simulated-vr.md).
Proton's stock DXVK is still in use; the supplied custom DXVK is bypassed by a
DXVK-detection guard. DLSS/FSR initialization remains unavailable, and the mod
reported failed depth copies for its blood effect. The simulated WMR trigger
test did not establish working input. Physical WiVRn display, stereo
correctness, controllers, calibration, and sustained gameplay remain unverified.

## Primary references

- [TTW official installation guide](https://thebestoftimes.moddinglinked.com/ttw.html)
- [TTW essential utilities](https://thebestoftimes.moddinglinked.com/essentials.html)
- [TTW download page](https://mod.pub/ttw/133/files)
- [WiVRn Steam/OpenVR notes](https://github.com/WiVRn/WiVRn/blob/master/docs/steamvr.md)
- [xrizer runtime selection](https://github.com/Supreeeme/xrizer#download--usage)
- [Proton's OpenVR setup](https://github.com/ValveSoftware/Proton/blob/experimental_11.0/proton)
- [Steam runtime OpenXR manifest discovery](https://gitlab.steamos.cloud/steamrt/steam-runtime-tools/-/blob/main/steam-runtime-tools/graphics-drivers-openxr-1.c)
- [Steam container runtime imports](https://gitlab.steamos.cloud/steamrt/steam-runtime-tools/-/blob/main/pressure-vessel/runtime.c)
- [DXVK's optional OpenVR initialization](https://github.com/doitsujin/dxvk/blob/master/src/dxvk/dxvk_openvr.cpp)

The wrapper follows WiVRn's documented `PRESSURE_VESSEL_IMPORT_OPENXR_1_RUNTIMES=1`
approach and xrizer's `VR_OVERRIDE`. The locally installed Proton script also
explicitly accepts `VR_PATHREG_OVERRIDE`, imports that JSON, selects the native
runtime, and generates its Windows-side VR registry.
