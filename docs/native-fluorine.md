# Native Linux manager: Fluorine

[← Installation](installation.md)

The native launch integration targets [Fluorine Manager v0.4.1](https://github.com/SulfurNitride/Fluorine-Manager/releases/tag/v0.4.1),
using its FUSE backend and built-in TTW game plugin. Fluorine is a native Qt/C++
port of MO2. Its executable is `ModOrganizer-core`, and some dialogs still say
“ModOrganizer”; this does not imply Windows MO2 is running.

## Copy the working installation first

Close the game and manager. Preserve the original MO2 directory and Steam launch
option. Make independent copies (not hardlinks) of `mods`, `profiles` including
saves, `overwrite`, `ModOrganizer.ini` and `ttwvr-linux.ini`. Create fresh
`downloads`, `cache` and `logs` directories. Copy the game directory, Proton
compatdata and private DXVK `syswow64` directory for an isolated first test.
Do not point two running managers at the same game/Data directory.

Install the official Fluorine release. The Linux x86_64 v0.4.1 tarball used here
has SHA256 `df4a734363aff2e5eaf0dc05a68c87f3281293ae07838f68553cd309c52b4585`.
The matching source tag resolves to `8a4f2aced385a842f0b3989ae84279959760d64d`.
No game, mod or NVIDIA runtime binaries are included in this repository.

Use dedicated XDG config/data directories for this instance. Configure Fluorine
with the copied prefix, supported Proton build, Steam app ID 22380 and FUSE.
Its global `fluorine/config.json` prefix and Proton paths take precedence over
instance settings; verify both point to the copies. Use the internal game
identifier `TTW`, adjust the managed game directory and all instance paths,
and point executable entries to the copied `FalloutNV.exe`. Keep profile INIs,
saves, mod order and plugin order intact. The tested profile enabled 25 of 26
mods and all 18 TTW masters.

A registry path mismatch prompt is expected when the game is copied. Accept
it only after checking the manager uses the copied prefix and the managed path
is the intended game copy.

If using the neural addon, copy its runtime separately and update the **copied
prefix's** OpenXR layer manifest registration to the new manifest. Keep the
addon opt-in and existing NR/DLAA settings. Do not overwrite loaded DLLs.

## Game wrapper inside Fluorine

In each executable's **Wrapper options**, enter one line:

```text
/usr/bin/python3 /path/to/ttwvr-linux/tools/steam_mo2.py --native-instance /path/to/native/instance --state-dir /path/to/native/state --steam-runtime "/path/to/SteamLinuxRuntime_4" -- %command%
```

Here `%command%` is expanded by Fluorine. The wrapper preserves its prepared
Fallout/NVSE command, applies the existing hash-checked VR patches and reads
`ttwvr-linux.ini` from the native instance. Set that file's patch paths to the
correct copies. Relative paths are resolved against the instance directory.
`TTW - Flat` disables VR automatically; `TTW - VR` enables the existing VR path.

The explicit Steam runtime handles Fluorine 0.4.1 failing to find a runtime in
a secondary Steam library. An already prepared matching runtime is retained
without nesting another container. OpenXR selection is imported outside the
container and its host manifest override is removed before Proton runs.

WiVRn is selected by default at `/usr/share/openxr/1/openxr_wivrn.json`.
`--xr-runtime` overrides it, or use `TTWVR_XR_RUNTIME_JSON` for a one-off test.
Neither path changes the desktop's default runtime registration.

## Native manager and Steam entry point

Launch the dedicated instance using:

```sh
python3 /path/to/ttwvr-linux/tools/launch_fluorine.py \
  --manager /path/to/fluorine-manager \
  --instance /path/to/native/instance \
  --config-home /path/to/native/config \
  --data-home /path/to/native/share
```

This selects `TTW - VR` and runs `Tale of Two Wastelands`. Add `--manager-only`
to manage mods without starting the game. Use `--profile "TTW - Flat"` for flat
play. `--dry-run` prints the command without launching or writing files.

For Steam's Fallout New Vegas Launch Options, put the same command on one line
and append `-- %command%`. This entry point consumes Steam's original game
command; Fluorine prepares the actual Proton launch using its own configured
prefix. Do not combine it with the old Windows MO2 launch option.

Always start this instance through `launch_fluorine.py` (or a shortcut calling
it) so the save-mount compatibility fix below is present.

## Save mount compatibility fix

Fluorine 0.4.1 wraps profile saves with `unshare --user --mount -r`. In the first
TTWVR test this mapped the game to UID 0; Steam's container then used
`XDG_RUNTIME_DIR=/run/user/0`, instead of the desktop user's runtime directory
where WiVRn exposes its socket. The native manager itself still ran normally.

`launch_fluorine.py` prepends a private `tools/fluorine-bin` directory to its
child process PATH. Its `unshare` adapter recognizes **only** Fluorine's exact
profile-save mount invocation. It replaces that invocation with a bubblewrap
bind mount that preserves the caller's UID and drops mount capabilities before
starting the game wrapper. Other unshare invocations pass through unchanged.
No system executable, kernel setting or Fluorine binary is modified.

The corrected full Proton launch was checked at UID 1000 with
`XDG_RUNTIME_DIR=/run/user/1000`; the restarted scene also applied DLAA to both
eyes on consecutive frames (624–626). The save mount is private to the game process
tree. An integration test verifies existing saves are readable, new writes reach
the profile, nested bubblewrap works and the mount disappears on exit.

## Validation and limits

On 2026-11-12 the native FUSE launch loaded xNVSE, TTW and TTWVR, displayed the
neural settings menu and loaded a copied save (confirmed by the user). The
simulated-headset run used full 2944×2208 eye images. DLAA logs included
`dlaa_evaluate ... output_applied=1` for both eyes on consecutive frames.
NR was off and DLAA was on, matching the existing installation settings.
The user subsequently confirmed that the native setup works in normal use.
This is functional confirmation; headset latency, streaming quality and frame
times were not measured during this migration.

For the optional real save-mount integration check, run as the desktop user:

```sh
TTWVR_TEST_USERNS=1 python3 -m unittest discover -s tests -v
```

For simulated launches, set `TTWVR_XR_RUNTIME_JSON` to the Monado simulated
runtime and set `SIMULATED_ENABLE=1`, `XRT_COMPOSITOR_NULL=1` and
`XRT_COMPOSITOR_NULL_FPS=90` only on that launch. Normal shortcuts should omit
these overrides.

A separate game copy is not automatically updated when Steam updates its
original installation. Treat it as a pinned installation and reconcile updates
with both game and manager closed; revalidate patch hashes before use.
Fluorine and old MO2 saves also diverge after the copy. For rollback, close the
native game and manager, restore the old Steam launch option and copy only
wanted newer saves (including their `.nvse` companions) back after backing up
both profiles. The original game, manager and prefix remain available.
