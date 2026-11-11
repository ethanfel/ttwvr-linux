# Tale of Two Realities on Linux / WiVRn

Work in progress: installation tooling and compatibility investigation for
KUPOkinz's **Tale of Two Realities (TTWVR)** with Tale of Two Wastelands.

**No working Linux VR session has been demonstrated yet.** This repository is
not a release of TTWVR, TTW, or a replacement VR mod. A configuration helper is
not evidence that the graphics interop works. Confirmed observations and
unresolved work are recorded in [docs/compatibility.md](docs/compatibility.md).

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

`tools/steam_mo2.py` is an **untested in-game** Steam launch wrapper. It preserves
Steam's supplied Proton/container command and replaces only the New Vegas
executable argument with a portable Mod Organizer 2 executable. It writes its
own OpenVR path registry, sets WiVRn/xrizer environment variables for that launch,
and does not edit the user's global OpenVR configuration or game files.

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
before this wrapper can launch the modded game. The wrapper has opened MO2
through Steam successfully; a game launch from MO2 remains unverified.

With Proton 11, `--wow64` enables the optional 64-bit host-side Wine/VR path
for the 32-bit game. This is a compatibility experiment, not yet verified in
game. For Monado simulation, supply its manifest through `--xr-runtime` and set
`SIMULATED_ENABLE=1 XRT_COMPOSITOR_NULL=1 XRT_COMPOSITOR_NULL_FPS=90` in the
launch environment. The user's system-wide runtime selection is not modified.

Use [CHANGELOG.md](CHANGELOG.md) to distinguish changes actually made from
experiments still to perform.
