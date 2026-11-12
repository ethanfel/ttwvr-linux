> **Archived Windows MO2 workflow (2026-11-12).** Retained for existing installations and rollback.
> Use the [Fluorine installation guide](../../installation.md) for the current setup.
> [Archive index](README.md) · [Migrate to Fluorine](../../native-fluorine.md#migrate-an-existing-mo2-installation)

# Installation

[← README](../../../README.md) · [Troubleshooting](troubleshooting.md)

This is a manual setup for **TTW 3.4 / TTWVR 1.1.0**. If flat TTW already works,
start at step 3, retaining the Steam wrapper from step 2.

## 1. Get the requirements

| Download / install | Source |
|---|---|
| English New Vegas Ultimate + Fallout 3 GOTY, all DLC | Your Steam library |
| TTW, YUPTTW, xNVSE and required utilities/presets | [The Best of Times](https://thebestoftimes.moddinglinked.com/intro.html) |
| Portable Mod Organizer 2 (tested: 2.5.2) | [MO2 releases](https://github.com/ModOrganizer2/modorganizer/releases/tag/v2.5.2) |
| FNV4GB **for Proton** (tested: 1.5) | [Author's download page](https://www.nexusmods.com/newvegas/mods/62552?tab=files) |
| Tale of Two Realities / TTWVR 1.1.0 | [KUPOkinz's Flat2VR thread](https://discord.com/channels/747967102895390741/1560413111423344730), Discord access required |
| WiVRn and xrizer | [WiVRn](https://github.com/WiVRn/WiVRn), [xrizer](https://github.com/Supreeeme/xrizer#download--usage) |

Install Python 3, Git, curl and bubblewrap (`/usr/bin/bwrap`). The performance patch
also needs Meson, Ninja, glslang and the i686 MinGW-w64 C/C++ compiler with POSIX
thread support. Use working Vulkan drivers, including their 32-bit support.

The supported Proton build is **`experimental-11.0-20261105c-x86_64`** with
Steam Linux Runtime 4. Check the `version` file in your Proton installation.
Other builds need patch revalidation; do not bypass a hash mismatch.

Launch each unmodded game once through Steam to initialize its prefix and INIs.
Close both games before setup. Clone this repository:

```sh
git clone https://github.com/ethanfel/ttwvr-linux.git
```

## 2. Set up and verify flat TTW

Extract portable MO2 to a directory you own. In **New Vegas → Properties →
General → Launch Options**, paste this as one line, replacing the paths:

```text
python3 "/path/to/ttwvr-linux/tools/steam_mo2.py" --mode flat --mo2 "/path/to/MO2/ModOrganizer.exe" --state-dir "/path/to/ttwvr-state" -- %command%
```

Select the supported Proton build in Steam's Compatibility settings, then
start New Vegas to open MO2. Create a portable instance for **Tale of Two
Wastelands**, pointing at the New Vegas directory.

Follow [The Best of Times](https://thebestoftimes.moddinglinked.com/intro.html)
for mod order, utilities and INIs, with these Linux-specific steps:

- Use **FNV4GB for Proton**. Back up `FalloutNV.exe` before patching it.
- Convert TTW using [the native Linux installer](https://github.com/SulfurNitride/TTW_Linux_Installer).
  Select the extracted TTW `.mpi`, both game directories and MO2's
  `mods/Tale of Two Wastelands` as the output directory.
- Name the MO2 profile **`TTW - Flat`** and enable profile-specific INIs and saves.
- Run `FalloutNV.exe` from MO2; the tested Proton 4GB patch autoloads xNVSE.

**Checkpoint:** ML Utilities Checker reports **“All utilities installed!”** and
a new game reaches the TTW intro. Disable the checker after it passes, make a
save, and close the game and MO2.

## 3. Create the VR profile

Duplicate the working flat profile as **`TTW - VR`**, with separate INIs and
saves. Install the author's TTWVR archive and enable it only in this profile.
Give it final asset override priority in MO2's left pane. Follow the included
TTWVR instructions for its controls and calibration.

Copy the flat save and its matching `.nvse` companion into the VR profile's
`saves` directory if you want to continue from that point; keep the originals.

## 4. Apply the Linux fixes

**[Follow these three patch steps](linux-fixes.md)** to install the native shader
compiler, prepare the private OpenXR bridge, and build the DXVK performance fix.
That page also creates `ttwvr-linux.ini` beside `ModOrganizer.exe`.

All three fixes are part of the tested setup. Keep TTWVR's original
`Resolution=150%`; the performance fix does not need reduced resolution or DLSS.

## 5. Launch in VR

Replace Steam's launch option with this single line, adjusting the paths:

```text
python3 "/path/to/ttwvr-linux/tools/steam_mo2.py" --mode vr --mo2 "/path/to/MO2/ModOrganizer.exe" --state-dir "/path/to/ttwvr-state" --xrizer "/opt/xrizer" --xr-runtime "/usr/share/openxr/1/openxr_wivrn.json" -- %command% -p "TTW - VR"
```

The xrizer path must contain `bin/linux64/vrclient.so`. Adjust the runtime
manifest path if your WiVRn installation uses another location.

1. Start WiVRn and connect your headset.
2. On the tested Quest 3, select **120 Hz** in WiVRn's headset video settings.
3. Start New Vegas in Steam, then run `FalloutNV.exe` from MO2's VR profile.
4. Load the copied save. Check that gameplay stays visible in the headset and
   head tracking works; then check your controls and calibration.

The measured result was about **60 game FPS at 120 Hz**, at the original eye
resolution. Performance will depend on your hardware and scene.

To return to flat play, close the game and MO2, use the step 2 launch option,
and select **`TTW - Flat`**. Always restart MO2 when changing launch modes.

## Native manager alternative

For migrating an existing MO2 TTW/VR installation, see the tested
[Fluorine native launch integration](../../native-fluorine.md).

[Amethyst](https://github.com/ChrisDKN/Amethyst-Mod-Manager) is a native Linux
mod manager with TTW installation and 4GB patch wizards. Its
[TTW guide](https://github.com/ChrisDKN/Amethyst-Mod-Manager/wiki/3.5-Installing-Viva-New-Vegas-and-Tale-of-Two-Wastelands#tale-of-two-wastelands)
covers game detection, the manually downloaded `.mpi`, conversion and missing
requirements, including the separate JIP/NVTF INIs.

This can simplify the base installation. **Amethyst + TTWVR has not been tested
in this repository.** The native integration currently supports Fluorine
MO2-compatible instances; Amethyst does not automatically apply these VR fixes. Its launch
path needs separate validation before using the patches with it.
