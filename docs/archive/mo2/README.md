# Archived Windows MO2 workflow

[← Current Fluorine setup](../../installation.md) · [Repository](../../../README.md)

The main setup now uses **native Fluorine with FUSE**, launching Fallout through
Proton. These instructions preserve the previous **Windows Mod Organizer 2
2.5.2 under Proton** workflow as it stood on 2026-11-12. They are historical
references for existing installations and rollback, not the default setup path.

- [Old installation and Steam launch commands](installation.md)
- [Old Linux patch setup](linux-fixes.md)
- [Old troubleshooting](troubleshooting.md)
- [Old neural addon opt-in and deployment record](neural-addon.md)
- [Migration from MO2 to Fluorine](../../native-fluorine.md#migrate-an-existing-mo2-installation)

Existing MO2 launcher support is retained. The shared `tools/steam_mo2.py` file
also powers Fluorine's `--native-instance` integration, so it remains in `tools/`.
Its filename does not mean the native path launches Windows MO2. New setup and
rendering documentation targets Fluorine; historical measurements still name
the manager used when they were collected.

Do not combine the old `--mo2 ... ModOrganizer.exe` Steam command with the native
`launch_fluorine.py` command. When rolling back, close the game and both managers
first and back up each profile. Saves diverge after migration; transfer only
wanted newer saves together with their matching `.nvse` companions.

The complete pre-rewrite tree is available at
[commit 3b9b6fb](https://github.com/ethanfel/ttwvr-linux/tree/3b9b6fb4ce82204013b1270e0976d658cfeb23be).
