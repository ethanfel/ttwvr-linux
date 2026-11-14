# Launch options, rollback and updates

[← Installation](installation.md) · [Troubleshooting](troubleshooting.md)

## Launch options

Use the launch command printed by apply. For the standard workspace:

| Action | Command |
|---|---|
| Play VR | `python3 ~/Games/ttwvr/linux-fixes/launch-ttwvr.py` |
| Open Fluorine only | `python3 ~/Games/ttwvr/linux-fixes/launch-ttwvr.py --manager-only` |
| Play flat | `python3 ~/Games/ttwvr/linux-fixes/launch-ttwvr.py --profile "TTW - Flat"` |
| Show the manager command without launching | `python3 ~/Games/ttwvr/linux-fixes/launch-ttwvr.py --dry-run` |

Use your actual generated path if different. Close Fallout and Fluorine before switching modes. Keep TTWVR disabled in **TTW - Flat**.

For Steam, use the exact line in `linux-fixes/steam-launch-options.txt`; it includes `-- %command%` and correct quoting for spaces.

## Roll back

1. Close Fallout and Fluorine.
2. Open a terminal in the `ttwvr-linux` repository.
3. Run the rollback command printed by apply. For the standard workspace:

```sh
python3 tools/setup_linux.py rollback ~/Games/ttwvr/linux-fixes/transaction.json
```

4. Restore your previous Steam Launch Options if you changed them manually.

**Expected result:** original configuration, compiler files and mod selection are restored. Saves are untouched. The generated mod and build files remain on disk, but the restored modlist disables the generated mod.

If rollback reports changed files, it has stopped before restoring anything. Preserve your later edits manually first. You can then use `--force`: it saves the conflicting current files in `before-forced-rollback-*` before restoring the original backups.

## Update an existing installation

The tool deliberately refuses to overwrite an existing deployment.

1. Close Fallout and Fluorine.
2. Keep your old prefix, rollback records and current settings backed up.
3. Run the recorded rollback above.
4. Move the retained, disabled **TTWVR Linux 1.1.5**, **1.1.4** or **1.1.3** mod folder outside `instance/mods`.
5. Install and enable the complete supported **original TTWVR** mod in the VR profile.
6. Add a fresh output path to setup.json, for example `"fixes": "linux-fixes-new"`.
7. Run `check`, then `apply`, following [the installation guide](installation.md#4-check-and-apply).
8. Replace Steam Launch Options with the newly generated line.

When also changing Proton, copy the **entire private compatdata folder** first, then select the copied `pfx` and new Proton in Fluorine. Its instance and dedicated global settings must agree. Retain the old prefix for rollback; do not downgrade a prefix already upgraded by another Proton build.

## Build dependencies in advance

From the repository, run:

```sh
python3 tools/setup_linux.py prepare setup.json
```

This downloads/verifies the shader compilers and builds the patched renderer without activating the VR patch. Later, `apply` reuses that verified cache. Close the game and manager before `apply`.

[What the patch changes](linux-fixes.md) · [Optional local build inputs](configuration.md#reuse-local-build-dependencies)
