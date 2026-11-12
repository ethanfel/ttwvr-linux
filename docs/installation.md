# Prepare an install, then patch it for Linux

[← README](../README.md) · [Troubleshooting](troubleshooting.md)

The tool applies Linux compatibility to a **prepared native Fluorine install**.
It does not install Fallout, convert TTW, download Nexus mods or validate every
TTW utility version. Finish the preparation phase and verify flat TTW first.

## 1. Get the requirements

| Requirement | Source / tested version |
|---|---|
| English New Vegas Ultimate + Fallout 3 GOTY, all DLC | Your Steam library |
| TTW, YUPTTW, xNVSE, utilities and required INI presets | [The Best of Times](https://thebestoftimes.moddinglinked.com/intro.html) |
| Native TTW conversion tool | [TTW Linux Installer](https://github.com/SulfurNitride/TTW_Linux_Installer) |
| FNV4GB **for Proton** | [Author's download page](https://www.nexusmods.com/newvegas/mods/62552?tab=files), tested 1.5 |
| Native Fluorine | [Official v0.4.1 release](https://github.com/SulfurNitride/Fluorine-Manager/releases/tag/v0.4.1), FUSE backend |
| Original TTWVR 1.1.4 or 1.1.3 | [KUPOkinz's Flat2VR thread](https://discord.com/channels/747967102895390741/1560413111423344730); Discord access required |
| WiVRn and xrizer | [WiVRn](https://github.com/WiVRn/WiVRn), [xrizer](https://github.com/Supreeeme/xrizer#download--usage) |
| Proton / Steam runtime | `experimental-11.0-20261105c-x86_64` / Steam Linux Runtime 4 |

The patch is pinned to exact Proton and TTWVR file hashes. **A newer Proton
Experimental is not automatically supported.** Retain the tested build in your
own Steam installation; if preflight rejects it, stop and use a supported build
or wait for an updated patch. Do not bypass the checks.

Install Python **3.11+**, Git, bubblewrap (`/usr/bin/bwrap`), FUSE 3 with
`fusermount3`, and `unshare`. The dependency build needs Meson, Ninja, glslang,
and the **i686 MinGW-w64 C/C++ toolchain with POSIX threads**, including windres.
Use working Vulkan drivers and the required 32-bit libraries. DLSS requires a
supported NVIDIA RTX GPU; FSR's current validation was also on RTX 5090.
Distro package names differ. The tool reports missing commands before apply.

## 2. Prepare and verify TTW in Fluorine

Use a dedicated game copy, initialized private Proton prefix, and native
Fluorine instance. Follow the [Fluorine preparation/migration reference](native-fluorine.md)
for its layout, `create-portable` command and global config paths.

1. Install TTW and all required utilities/presets from the guide.
2. Apply FNV4GB for Proton to the managed game. Configure Fluorine's **TTW** game
   plugin, **FUSE**, the supported Proton and private prefix ending in `/pfx`.
3. Configure an executable named **Tale of Two Wastelands**, pointing to the
   managed `FalloutNV.exe`, with Proton enabled and Steam app ID **22380**.
4. Create **TTW - Flat** with profile-local INIs and saves. Launch it, check that
   ML Utilities Checker reports **“All utilities installed!”**, reach the intro,
   save and reload. Disable the checker after it passes.
5. Copy the profile as **TTW - VR**. Keep the flat profile and original saves.
   Install the complete **original, unpatched** TTWVR archive as its own mod and
   enable it in the VR profile. Keep the author-supplied helpers and runtimes.

Use actual files or reflinks for mod assets. Fluorine 0.4.1 skips symlinked
assets; symlinked BSAs caused a menu crash in testing. Do not hard-link game/mod
files when making an isolated installation that you will later patch.

The selected profile must use `instance/mods` and `instance/profiles`.
Custom external mod-directory layouts are not supported by this first tool.
The private prefix and global `config/fluorine/config.json` must point to the
same prepared installation. The tool rejects conflicting paths.

## 3. Describe your prepared install

Clone this repository somewhere you will keep it, then copy the template:

```sh
git clone https://github.com/ethanfel/ttwvr-linux.git
cd ttwvr-linux
cp tools/setup.example.json setup.json
```

Edit **setup.json** with your absolute paths. Spaces are supported.
`mod` is the extracted TTWVR directory under `instance/mods`, not its ZIP.
`fixes` must be a **new, separate directory** outside the instance, mod, prefix
and Proton directories. It will hold dependencies, backups and the launcher.
Keep the repository and fixes directory in place after installation.

`config_home` and `data_home` are the same dedicated XDG directories used to
prepare Fluorine. They are not the paths to `config.json` or the manager binary.
The defaults for profile/executable and WiVRn/xrizer match the template; change
them only to match your installation. Keep the flat profile named `TTW - Flat`.

## 4. Check and apply

```sh
python3 tools/setup_linux.py check setup.json
```

This reads the prepared install without changing it. It checks the exact plugin
and helper files, Proton bridge/renderer, Fluorine paths, active mod assets and
required tools. It also reproduces the binary patch in memory. It does not
replace the flat-game utility check or prove headset connectivity.

Close Fallout and Fluorine, then:

```sh
python3 tools/setup_linux.py apply setup.json
```

The first apply downloads checksum-pinned native shader compilers from
[Mozilla's fxc2 repository](https://github.com/mozilla/fxc2/tree/master/dll),
clones the pinned DXVK revision and builds it with the pool and opaque-fence
source patches. Build output is shown in the terminal. No NVIDIA SDK download
or custom D3D12 helper build is required. Allow several GB of disk space for
the source/build, private Windows directory and copied mod.

To complete the downloads/build separately, while continuing to use the
existing install, run `python3 tools/setup_linux.py prepare setup.json` first.
`apply` reuses the verified dependency cache. Both use the same config file.

Apply then:

- Copies TTWVR into **TTWVR Linux 1.1.4** (or 1.1.3) and patches that copy for
  bundled renderer activation, Wine Vulkan loading, DLSS output and native FSR.
- Preserves the original mod and its INI settings, including calibration,
  resolution and upscaler choice. Disables previous TTWVR/menu-addon entries
  only in the chosen VR profile and gives the new mod priority.
- Installs both native compiler files in the private prefix, creates private
  OpenXR/DXVK overlays and configures the selected Fluorine executable.
- Enables the author's path and NVAPI; disables the archived neural layer.
- Records backups in **fixes/transaction.json**, then creates the launcher and
  **fixes/steam-launch-options.txt**. Saves are never edited.

An existing transaction or destination mod is rejected. This is a guarded
first-install tool, not an in-place updater. Keep rollback records when
upgrading; use a fresh fixes directory and original upstream mod for a new run.

## 5. Launch

Connect your headset in WiVRn. Run the exact command printed by apply:

```sh
python3 "/path/to/ttwvr/linux-fixes/launch-ttwvr.py"
```

For Steam, copy the generated **steam-launch-options.txt** into **New Vegas →
Properties → Launch Options**. Its form is:

```text
python3 "/path/to/ttwvr/linux-fixes/launch-ttwvr.py" -- %command%
```

The tool prints the properly quoted command for your paths. Fluorine runs on
the host and mounts its FUSE view before the game-only overlays are applied.
It selects the VR profile and starts Fallout automatically. Names such as
“ModOrganizer” in native Fluorine dialogs are normal.

Use the original **VR Settings** to choose **DLSS** or **FSR**, upscaler level,
resolution and sharpness. The tool preserves your choices; it does not force
200% or promise an FPS target. In the tested author path, 100%/150%/200% produced
2944×2208 / 4480×3360 / 5888×4416 per-eye output. These are measured on one
runtime configuration, not universal dimensions. See [resolution evidence](author-dlss.md).

Launcher options:

- `--dry-run`: show the native manager command without starting it.
- `--manager-only`: open Fluorine to manage this instance.
- `--profile "TTW - Flat"`: flat play, with TTWVR disabled in that profile.

Close Fallout and Fluorine before changing launch modes. The wrapper keeps
WiVRn's default runtime registration and installed Proton files unchanged.

## 6. Roll back

With Fallout and Fluorine closed:

```sh
python3 tools/setup_linux.py rollback "/path/to/ttwvr/linux-fixes/transaction.json"
```

This restores configuration, mod selection and compiler files, including original
symlinks. It disables the added mod through the restored modlist. Private build
files and the copied mod remain available for inspection; saves remain intact.
Restore your previous Steam launch option separately if you changed it manually.

If you edited settings after apply, rollback stops before restoring any file.
Keep those edits manually, or add `--force` to save the current conflicting
files under `before-forced-rollback-*` and then restore the original backups.
Do not remove just the fence fix while retaining the patched author renderer.

## Optional local dependency inputs

Advanced users may add `compiler32` / `compiler64` paths to setup.json to avoid
those downloads; exact known hashes are required. `dxvk_dir` accepts the exact
previously validated private renderer bundle with `hashes.json`. Unknown
prebuilt binaries are refused; omit this field to build from pinned source.
Locally built outputs carry their revision, patch hashes and file checksums.

[Compatibility and limits](compatibility.md) · [Linux fixes explained](linux-fixes.md) ·
[Archived Windows MO2](archive/mo2/README.md)
