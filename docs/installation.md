# Install TTWVR with native Fluorine

[← README](../README.md) · [Troubleshooting](troubleshooting.md) ·
[Fluorine configuration](native-fluorine.md)

This is the primary manual setup for **TTW 3.4 / TTWVR 1.1.3**, using
**Fluorine 0.4.1 with FUSE**. Fluorine runs natively on Linux; Fallout runs through
Proton. The tested migration started with an existing working TTW installation.
The fresh setup below uses the same native instance and launch configuration.

Already have working TTW in Windows MO2? Start with the
[migration checklist](native-fluorine.md#migrate-an-existing-mo2-installation),
then verify the Linux fixes and launch settings below. The old MO2 setup is
[archived](archive/mo2/README.md).

## 1. Get the requirements

| Download / install | Source |
|---|---|
| English New Vegas Ultimate + Fallout 3 GOTY, all DLC | Your Steam library |
| Native Fluorine Manager, tested v0.4.1 | [Official release](https://github.com/SulfurNitride/Fluorine-Manager/releases/tag/v0.4.1) |
| TTW, YUPTTW, xNVSE and required utilities/presets | [The Best of Times](https://thebestoftimes.moddinglinked.com/intro.html) |
| Native TTW conversion tool | [TTW Linux Installer](https://github.com/SulfurNitride/TTW_Linux_Installer) |
| FNV4GB **for Proton**, tested 1.5 | [Author's download page](https://www.nexusmods.com/newvegas/mods/62552?tab=files) |
| Tale of Two Realities / TTWVR 1.1.3 | [KUPOkinz's Flat2VR thread](https://discord.com/channels/747967102895390741/1560413111423344730), Discord access required |
| WiVRn and xrizer | [WiVRn](https://github.com/WiVRn/WiVRn), [xrizer](https://github.com/Supreeeme/xrizer#download--usage) |

Install Python 3, Git, curl, bubblewrap (`/usr/bin/bwrap`) and the FUSE support
required by Fluorine. The DXVK build also needs Meson, Ninja, glslang and the
i686 MinGW-w64 C/C++ compiler with POSIX thread support. Install working Vulkan
drivers with their 32-bit support.

The supported Proton build is **`experimental-11.0-20261105c-x86_64`**, used with
**Steam Linux Runtime 4**. Check Proton's `version` file. Other builds require
patch revalidation; selecting the latest Experimental is not sufficient.

Launch each unmodded game once through Steam to initialize its prefix and INIs,
then close both games. Clone this repository:

```sh
git clone https://github.com/ethanfel/ttwvr-linux.git
```

## 2. Set up and verify flat TTW

Choose a dedicated workspace for the native instance, XDG configuration, game
and Proton prefix. The tested setup uses independent game/prefix copies so the
original Steam installation remains available. Use ordinary copies or reflinks,
not hardlinks. Keep the full compatdata directory, including its `pfx` subdirectory.
See the [layout and first-run configuration](native-fluorine.md#instance-layout-and-configuration).

Create a **Tale of Two Wastelands** instance in Fluorine, pointing at the intended
New Vegas game directory. The internal game name is `TTW`. Configure:

- The supported Proton installation, the dedicated prefix ending in `/pfx`, and Steam app ID **22380**.
- **FUSE** as the mod deployment backend.
- A profile named **TTW - Flat**, with profile-specific INIs and saves.
- An executable named **Tale of Two Wastelands**, pointing to that game's `FalloutNV.exe`.
- The [game wrapper](native-fluorine.md#game-wrapper-inside-fluorine) in the executable's **Wrapper options**.

Follow [The Best of Times](https://thebestoftimes.moddinglinked.com/intro.html)
for required mods, utility presets, load order and INIs. Use Fluorine to install
and arrange the mods. Replace the guide's Windows manager/launch steps with this
native setup. Linux-specific steps:

1. Back up `FalloutNV.exe`, then apply **FNV4GB for Proton** to the managed game.
2. Convert the extracted TTW `.mpi` with the native installer, using both game
   directories and `instance/mods/Tale of Two Wastelands` as the output.
3. Install the required root files in the managed game directory and the
   remaining mods in the instance; include separately downloaded INI presets.
4. Start the instance through `launch_fluorine.py` with `--profile "TTW - Flat"`
   as shown in the [launcher reference](native-fluorine.md#native-manager-and-steam-entry-point).
   The tested 4GB patch autoloads xNVSE through `FalloutNV.exe`.

**Checkpoint:** ML Utilities Checker reports **“All utilities installed!”**, a
new game reaches the TTW intro, and a save can be loaded. Disable the checker
after it passes. Keep this flat profile and its saves.

## 3. Create the VR profile

With Fallout closed, duplicate the flat profile as **TTW - VR**, retaining
separate INIs and saves. Install the author's TTWVR archive in Fluorine and
enable it only in the VR profile. Give it final asset override priority, then
follow its included calibration and control instructions.

To continue an existing game, copy the desired save and its matching `.nvse`
companion to the VR profile's `saves` directory. Keep the originals.
The optional Rendering Addon is installed later, after base VR works.

## 4. Apply the Linux fixes

**[Follow the three patch steps](linux-fixes.md)** using the prefix and game
managed by Fluorine: native D3DCompiler 47, the private OpenXR bridge, and the
DXVK eye-buffer pool patch. Create **`instance/ttwvr-linux.ini`**, beside
`ModOrganizer.ini`. This file belongs to the native instance, not the Fluorine
application directory.

All three fixes are part of the tested VR setup. Keep TTWVR's original
`Resolution=150%` (2944×2208 in the tested configuration). The performance fix
does not depend on lowering resolution or enabling DLSS.

## 5. Launch in VR

First configure the [wrapper inside Fluorine](native-fluorine.md#game-wrapper-inside-fluorine).
Then put this command on one line in **New Vegas → Properties → Launch Options**,
replacing every path:

```text
python3 "/path/to/ttwvr-linux/tools/launch_fluorine.py" --manager "/path/to/fluorine-manager/fluorine-manager" --instance "/path/to/ttwvr/instance" --config-home "/path/to/ttwvr/config" --data-home "/path/to/ttwvr/share" -- %command%
```

This selects **TTW - VR** and runs the executable named **Tale of Two Wastelands**
automatically. Fluorine prepares Proton's command; the original Steam command
is consumed by the native entry point. A local shortcut can contain this same
command. The setup is manual; this repository does not install the games or mods.

1. Start WiVRn and connect the headset. The earlier Quest 3 test used **120 Hz**.
2. Start New Vegas in Steam. Fluorine mounts the selected mods and launches Fallout.
3. Load the copied save; verify headset gameplay, tracking, controls and calibration.

Always use this entry point so the profile-save mount retains the desktop user
ID needed for WiVRn. Fluorine's binary and some dialogs still say
**ModOrganizer**; this is expected for its native port.

To open the manager without starting Fallout, insert `--manager-only` before
`-- %command%`. For flat play, close Fallout and Fluorine, then use
`--profile "TTW - Flat"` before the separator. The flat profile disables the VR
patch path and neural opt-in. `--dry-run` prints the launch without starting it.

## 6. Add in-game AA and neural rendering controls

After base VR works, follow the **[Rendering Addon installation](../neural/docs/STEAM.md)**.
Install it as a mod in Fluorine's VR profile, with the matching layer/helper and
private-prefix registration. Your Steam command remains the same.

Open **VR Settings** for Off/DLAA/MSAA 2×/4×/8× and NR tuning. NR/DLAA settings
apply live; MSAA requires a restart and currently excludes NR/DLAA. NVIDIA
runtime files must be obtained separately. The addon defaults to effects off.
See [tested behavior and limits](../neural/docs/VR-SETTINGS.md).

## Existing installations and other managers

- [Migrate from Windows MO2](native-fluorine.md#migrate-an-existing-mo2-installation), preserving profiles, saves and rollback copies.
- [Archived Windows MO2 setup](archive/mo2/README.md), including its old Steam commands.
- [Amethyst](https://github.com/ChrisDKN/Amethyst-Mod-Manager) is another native manager, but its TTWVR launch integration has not been tested here.

The native path was functionally confirmed with a migrated save. The earlier
59.8 FPS headset capture is not a Fluorine benchmark; see the
[compatibility and evidence summary](compatibility.md).
