# Fluorine configuration and launch reference

[← Installation](installation.md) · [Troubleshooting](troubleshooting.md) ·
[MO2 archive](archive/mo2/README.md)

The primary integration targets
[Fluorine Manager v0.4.1](https://github.com/SulfurNitride/Fluorine-Manager/releases/tag/v0.4.1),
its **FUSE** backend and built-in **TTW** game plugin. Fluorine is a native Qt/C++
port of MO2. Its running executable is `ModOrganizer-core`, and some dialogs
still say “ModOrganizer”; that is expected in the native application.

Use the official release's `fluorine-manager` launcher. The tested Linux x86_64
v0.4.1 tarball has SHA256
`df4a734363aff2e5eaf0dc05a68c87f3281293ae07838f68553cd309c52b4585`;
the source tag is `8a4f2aced385a842f0b3989ae84279959760d64d`.
These identify the tested release, not every future compatible build.

## Instance layout and configuration

One dedicated workspace keeps the paths explicit:

```text
ttwvr/
  game/                         managed New Vegas copy
  compat/pfx/                   private Proton prefix, inside copied compatdata
  config/fluorine/config.json    dedicated Fluorine global settings
  share/                        dedicated Fluorine XDG data
  instance/
    ModOrganizer.ini            native instance configuration
    ttwvr-linux.ini              this project's VR patch/opt-in settings
    mods/
    profiles/TTW - Flat/
    profiles/TTW - VR/
    overwrite/
    downloads/
  linux-fixes/                  private compiler/OpenXR/DXVK files and backups
  state/                        launcher logs
```

The repository and Fluorine application can live elsewhere. Paths in commands
are examples; replace them with absolute paths for your installation. The
manager's prefix path ends in `/pfx`, not the parent compatdata directory.

For an existing installation, use the [migration checklist](#migrate-an-existing-mo2-installation).
For a fresh instance, follow [the preparation steps](preparation.md#3-create-and-open-the-native-instance). They set a workspace path once and use Fluorine's `create-portable` command to create the instance. The [setup.json reference](configuration.md) explains automatic path discovery and optional overrides.

Configure the supported Proton build, the private prefix, Steam app ID **22380**,
**FUSE**, and executable **Tale of Two Wastelands** pointing to the managed
`FalloutNV.exe`, with its working directory set to the managed game directory.
Enable Proton for that executable. Its wrapper is specified in the next section.

Fluorine's global `config/fluorine/config.json` prefix and Proton settings take
precedence over instance settings. Verify that both resolve to the intended
installation. The relevant global values are:

```json
{
  "app_id": 22380,
  "prefix_path": "/path/to/ttwvr/compat/pfx",
  "proton_name": "Proton - Experimental",
  "proton_path": "/path/to/steamapps/common/Proton - Experimental"
}
```

These are the relevant keys, not a command to replace an existing configuration.
Use the same dedicated `--config-home` and `--data-home` for every launch.
The instance stores its native settings under `[fluorine]` in
`ModOrganizer.ini`, including `vfs_backend=fuse` and the matching prefix/Proton
paths. Leave optional io_uring/passthrough features at their defaults for the
initial setup; the validated configuration did not require them.

## Game wrapper inside Fluorine

The [setup tool](installation.md#4-check-and-apply) writes this automatically
for the selected game executable. The following is the manual reference.

In each executable's **Wrapper options**, enter one line:

```text
/usr/bin/python3 "/path/to/ttwvr-linux/tools/steam_mo2.py" --native-instance "/path/to/ttwvr/instance" --state-dir "/path/to/ttwvr/state" --steam-runtime "/path/to/SteamLinuxRuntime_4" --xrizer "/opt/xrizer" -- %command%
```

This is an executable **Wrapper options** value inside Fluorine, not the Steam
Launch Options command. Despite its historical name, `steam_mo2.py` in
`--native-instance` mode does not start Windows MO2. Its legacy `--mo2` mode
is documented only in the [archive](archive/mo2/README.md).

Here `%command%` is expanded by Fluorine. The wrapper preserves its prepared
Fallout/NVSE command, applies the existing hash-checked VR patches and reads
`ttwvr-linux.ini` from the native instance. Set that file's patch paths to the
correct copies. Relative paths are resolved against the instance directory.
`TTW - Flat` disables VR automatically; `TTW - VR` enables the VR path.
Keep these exact profile names. The xrizer directory must contain
`bin/linux64/vrclient.so`; replace `/opt/xrizer` if installed elsewhere.

The explicit Steam runtime handles Fluorine 0.4.1 failing to find a runtime in
a secondary Steam library. An already prepared matching runtime is retained
without nesting another container. OpenXR selection is imported outside the
container and its host manifest override is removed before Proton runs.

WiVRn is selected by default at `/usr/share/openxr/1/openxr_wivrn.json`.
`--xr-runtime` overrides it, or use `TTWVR_XR_RUNTIME_JSON` for a one-off test.
Neither path changes the desktop's default runtime registration.

## Native manager and Steam entry point

Use the generated `linux-fixes/launch-ttwvr.py` after applying the patch tool. The
command below is the underlying entry point for manual preparation/migration.

Launch the dedicated instance using:

```sh
python3 "/path/to/ttwvr-linux/tools/launch_fluorine.py" \
  --manager "/path/to/fluorine-manager/fluorine-manager" \
  --instance "/path/to/ttwvr/instance" \
  --config-home "/path/to/ttwvr/config" \
  --data-home "/path/to/ttwvr/share"
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

The following records the earlier migration, before the custom addon was
archived. The current setup uses the original author menu and renderer; see
[current compatibility](compatibility.md) and [1.1.4](ttwvr-1.1.4.md).

On 2026-11-12 the native FUSE launch loaded xNVSE, TTW and TTWVR, displayed the
neural settings menu and loaded a copied save (confirmed by the user). The
simulated-headset run used full 2944×2208 eye images. DLAA logs included
`dlaa_evaluate ... output_applied=1` for both eyes on consecutive frames.
At that migration checkpoint NR was off and DLAA was on. The later
[Rendering Addon deployment](../archive/rendering-addon/neural/docs/STEAM.md#initial-native-addon-deployment-before-the-113-upgrade)
retained the subsequently saved state with both features off; the two records
describe different checkpoints.
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

## Migrate an existing MO2 installation

Close the game and manager. Preserve the original MO2 directory and Steam launch
option. Make independent copies (not hardlinks) of `mods`, `profiles` including
saves, `overwrite`, `ModOrganizer.ini` and `ttwvr-linux.ini`. Create fresh
`downloads`, `cache` and `logs` directories. Copy the game directory, Proton
compatdata and private DXVK `syswow64` directory for an isolated first test.
Do not point two running managers at the same game/Data directory.

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

For archival reproduction only: if using the old neural addon, copy its runtime separately and update the **copied
prefix's** OpenXR layer manifest registration to the new manifest. Keep the
addon opt-in and existing NR/DLAA settings. Do not overwrite loaded DLLs.

Fluorine and old MO2 saves also diverge after the copy. For rollback, close the
native game and manager, restore the old Steam launch option and copy only
wanted newer saves (including their `.nvse` companions) back after backing up
both profiles. The original game, manager and prefix remain available.
