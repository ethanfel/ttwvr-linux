# TTWVR 1.1.3 compatibility and menu update

[← README](../README.md) · [Addon controls](../neural/docs/VR-SETTINGS.md)

The supplied TTWVR1.1.3.zip moves **VR Settings** to the main/pause menu, beside
Settings. The Rendering Addon now follows that menu and retains its seven rows
after Resolution. Back returns to the parent menu; normal resume remains available.
The earlier addon accepted only TTWVR 1.1.0, so updating the base mod alone would
leave its native rendering controls and renderer integration unavailable.

## Identified archive

| File | SHA-256 |
|---|---|
| TTWVR1.1.3.zip | `b58b07aadc4bd1076242baa5f80d17109db026cd80f236f6f1ccc437d42bc665` |
| NVSE/Plugins/TTWVR.dll | `4c45d6e5adb5da76dd3d08bf0639d360761d3dc50d891ea0bd19b51e4fb006a0` |

The archive's `meta.ini` still reports 1.1.2. Compatibility is selected by the
actual DLL hash, not the filename or metadata. The new DLL, two companion hosts
and supplied INI changed; existing menu assets and meshes match the previous
installation. This repository distributes neither the TTWVR archive nor NVIDIA
SDK/runtime files.

## Upgrade an existing Fluorine installation

1. Quit Fallout and Fluorine. Back up the active profile's `modlist.txt`, existing
   TTWVR INI, addon INI and private rendering runtime before changing files.
2. Install the supplied archive as a separate mod, for example **Tale of Two
   Realities 1.1.3**. Keep the previous mod folder for rollback. Enable the new
   version and disable the previous version in the VR profile.
3. Preserve the previous `NVSE/Plugins/TTWVR/TTWVR.ini`, including any overriding
   copy. The archive defaults enable DLSS and change sharpness/UI values; do not
   silently replace the user's chosen settings.
4. Rebuild/install the matching `TTWVRRenderingAddon.dll` and
   `TTWVRNeuralRenderingLayer.dll` from this revision. Keep the existing neural
   helper, runtime dependencies and INIs; this port changes no helper algorithm
   or protocol. See [build instructions](../neural/README.md) and
   [Fluorine addon setup](../neural/docs/STEAM.md).
5. Start through the existing Fluorine Steam command. Open **VR Settings**
   directly from the main/pause menu. No Steam-command change is needed.

Saves, profile INIs, rendering resolution and NR/DLAA/MSAA selections need no
migration. To roll back, stop the game, select the previous TTWVR mod and restore
its matching addon/layer backup. Do not replace DLLs while a game has them loaded.

## Deployment completed, 2026-11-12

The verified port at commit `4feb91a` was installed in the regular Fluorine
instance with the game and manager stopped. The VR profile now enables the
separate 1.1.3 mod and disables 1.1.0; the old folder and a rollback backup remain.
The x86 addon and OpenXR layer were updated together. The existing x64 helper
matched the build and needed no replacement.

All **53 checked settings/save files** retained their hashes, including the
previous TTWVR INI, neural runtime preferences, profile INIs and saves. The flat
profile's mod list was also preserved. Steam launch options, WiVRn registration
and the Titanfall installation were unchanged. This records a completed local
installation, not a physical-headset validation of the new version.

## Implementation review

The layer and startup DLL share one exact-hash layout table in
[`TTWVRBuild.cpp`](../neural/src/TTWVRBuild.cpp). It retains the verified 1.1.0
layout and adds the supplied 1.1.3 layout. Unknown builds bypass integration.
Device access, native menu construction/edit state, settings overrides, scene
preparation, per-eye setup and motion submission were individually remapped.
Relevant renderer functions matched the previous bodies after normalizing module
addresses and branch relocations; getters and referenced globals were checked.

Menu option layout and engine vector ownership remain unchanged. The addon checks
stock row labels as well as flags before hiding Upscaler, Upscaler Level and
Sharpness. This avoids hiding an adjacent stock option if a layout is wrong.
Full Body and the other original VR options remain available. No TTWVR binary is
patched on disk. NR, DLAA, exposure, motion conventions and jitter algorithms are
unchanged by this compatibility port. Frame generation remains absent.

## Validation

Validation uses the isolated copied game/profile and Monado simulated headset,
without changing the production WiVRn registration or Titanfall installation.
Both Windows builds compile, and all four native tests pass: projection/jitter,
live settings, eye history/image ownership and OpenXR lifecycle.

The revised menu was exercised with gamepad inputs. All addon controls appear
after Resolution; the original upscaler controls are hidden and Full Body remains.
Back/resume works, and Neural Rendering can be switched on in the menu.
DLAA captures at frames **6060/6061** contain both eyes at **2944×2208** and actual
successful output application in capture metadata. The same run logs
`dlaa_evaluate` with `output_applied=1` for both eyes. Same-frame input/output stereo
previews have finite values and preserve alpha.

Combined NR/DLAA captures at **7710/7711** also contain both applied eyes. The
run logs both `neural_evaluate` and `dlaa_evaluate` with `output_applied=1`.
The combined same-frame previews retain finite RGB values and unchanged alpha.

Selecting **MSAA 4×** in the native menu disabled NR/DLAA and displayed
`(restart)`. After restart, the menu showed active MSAA 4×. Frames **310/311**
from the new run contained matching **2944×2208**, four-sample HDR color and
D24S8 depth targets, with **1510/1481** matching scene draw events. Both final
eyes were captured; the neural helper remained bypassed. Stereo previews were
inspected. These tests used the private legacy MO2 harness; deployment uses the
existing native Fluorine installation. Physical headset confirmation of 1.1.3
is still pending.

The final rebuilt DLLs were also exercised at a stationary simulated pose:
DLAA **1200/1201**, effects off **1920/1921**, and combined NR/DLAA
**4110/4111**. Final stereo previews were inspected with matching poses,
FOVs and image rectangles. The moving simulated camera had intersected the player
model in the earlier neural captures; the stationary run cleared that obstruction
without changing Full Body or production settings. Physical body tracking still
requires headset confirmation. The simulated movement change affected only the
test process, which was closed afterward.

These are compatibility and local GPU checks, not a new headset performance or
comfort benchmark. The earlier reported DLAA softness/haze remains under
investigation; this update does not claim to fix it. The addon keeps TTWVR's stock
upscaler disabled; the changed upstream upscaler host has not been revalidated.

## Later neural performance build

The [GPU handoff update](proton-neural-performance.md) uses a new layer/helper
protocol. When installing that build, replace **both** the x86 layer and x64
`neural_host.exe` with a matching pair while the game is closed. The 1.1.3 menu
bootstrap remains compatible. Keep your INIs and use the linked opt-in/rollback
instructions; this does not enable stock DLSS/FSR or establish a DLAA haze fix.
