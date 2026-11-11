# Tale of Two Realities on Linux / WiVRn

Work in progress: installation tooling and compatibility investigation for
KUPOkinz's **Tale of Two Realities (TTWVR)** with Tale of Two Wastelands.

**TTWVR now displays loaded gameplay on a Quest 3 through WiVRn under Proton 11
WoW64. The full-resolution performance patch has passed an initial 120 Hz
headset test: a 59.8 FPS spectator capture and user-confirmed clear, smooth
gameplay. Full controller and sustained-session checks remain.** This repository contains
installation tooling and compatibility findings, not TTWVR or TTW binaries.
Confirmed observations and unresolved work are recorded in
[docs/compatibility.md](docs/compatibility.md).

## Inputs

- Legitimate English Steam installations of Fallout: New Vegas Ultimate Edition
  and Fallout 3 GOTY, with all DLC.
- TTW installer, YUPTTW, and prerequisites from
  [The Best of Times](https://thebestoftimes.moddinglinked.com/).
- The user's TTWVR archive from
  [KUPOkinz's Flat2VR thread](https://discord.com/channels/747967102895390741/1560413111423344730).
  That thread requires Discord access; it was not read during this investigation.
- Proton, WiVRn, and an OpenVR-to-OpenXR implementation such as xrizer.

No game assets, mod archives, third-party binaries, savegames, credentials, or
personal configuration are included here. All authors retain ownership of their
work. Obtain prerequisites from their original authors.

## Tools

`tools/create_profiles.py` prepares new flat/VR MO2 profiles from staged mods,
ordered JSON lists, launcher-generated INIs, and a custom INI. It refuses to
overwrite existing instance/profile configuration. See `--help` and the
[installation record](docs/installation.md).

Run a read-only installation audit (the report does not include absolute paths):

```sh
python3 tools/audit.py --fnv '/path/to/Fallout New Vegas' \
  --fo3 '/path/to/Fallout 3 goty' --ttwvr '/path/to/TTWVR.zip'
```

`tools/steam_mo2.py` is an experimental Steam launch wrapper. It preserves
Steam's supplied Proton/container command and replaces only the New Vegas
executable argument with a portable Mod Organizer 2 executable. It writes its
own OpenVR path registry, sets WiVRn/xrizer environment variables for that launch,
and does not edit the user's global OpenVR configuration or game files.

Mode defaults to `auto`: the `TTW - Flat` profile disables the VR probe and
does not require WiVRn or xrizer. Other profiles use VR. MO2's `-p`/`--profile`
argument takes precedence over the last selected profile. `--mode flat` or
`--mode vr` overrides detection. Restart MO2 through the wrapper after changing
between flat and VR profiles so the launch environment matches the profile.

Preview it before configuring Steam (use actual paths):

```sh
python3 tools/steam_mo2.py --dry-run \
  --mo2 /path/to/MO2/ModOrganizer.exe --state-dir /path/to/local/vr-state \
  --xrizer /opt/xrizer --xr-runtime /usr/share/openxr/1/openxr_wivrn.json \
  -- /path/to/proton waitforexitandrun '/path/to/Fallout New Vegas/FalloutNVLauncher.exe'
```

The eventual Steam launch option has the same arguments, without `--dry-run`,
and ends in `-- %command%` rather than the example Proton command. Start WiVRn
and connect the headset first. MO2 and the prerequisites must be configured
before this wrapper can launch the modded game. The wrapper has launched both
flat TTW gameplay and a TTWVR save with the simulated headset through MO2.

Loading a save in TTWVR exposed a crash in Wine's shader compiler. Installing
native D3DCompiler 47 and enabling it for VR launches passed that same save-load
check. See the [compiler workaround and rollback](docs/simulated-vr.md), including
the optional `ttwvr-linux.ini` beside MO2. The wrapper does not install DLLs.

A separate Proton OpenXR conversion bug made the headset go black after loading
gameplay. The [private OpenXR bridge patch](docs/openxr-layers.md) resolves that
observed failure. It is restricted to one verified Proton build and is applied
only inside the game's launch environment. It does not modify Steam's installed
Proton or redistribute its binaries.

The [performance investigation](docs/performance.md) records the renderer and
Vulkan-loader experiments, and a standalone reproducer for Proton's separate
cross-process GPU-fence crash. The experimental TTWVR DLL changes are not
enabled by the normal launcher; they have not made DLSS usable.

A [full-resolution DXVK memory-pool patch](docs/dxvk-pools.md) reduces repeated
eye-buffer allocations. The matching simulated control showed 18.8 FPS versus
44.1 FPS with the patch, at the same resolution. A subsequent 120 Hz simulation
showed 58.9 FPS. The physical Quest 3 retest at 120 Hz showed 59.8 FPS, and the
user confirmed clear, very smooth gameplay. The patch is configured locally
for VR; sustained-session validation remains pending.

For Steam's container command, the wrapper gives the selected OpenXR manifest
to pressure-vessel's importer, then clears the host override before Proton so
the loader uses the imported manifest. Launcher checks can be run with:

```sh
python3 -m unittest discover -s tests -v
```

With Proton 11, `--wow64` enables the optional 64-bit host-side Wine/VR path
for the 32-bit game. TTWVR's OpenXR initialization and head tracking have been
observed with this mode. For Monado simulation, supply its manifest through `--xr-runtime` and set
`SIMULATED_ENABLE=1 XRT_COMPOSITOR_NULL=1 XRT_COMPOSITOR_NULL_FPS=90` in the
launch environment. The user's system-wide runtime selection is not modified.

Use [CHANGELOG.md](CHANGELOG.md) to distinguish changes actually made from
experiments still to perform.
