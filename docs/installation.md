# Install the TTWVR Linux patch

[← README](../README.md) · [Discord community](https://discord.com/invite/ttwvr)

**Already playing TTW in native Fluorine?** Check the four items below, then start at step 1.

**Starting from the Steam games?** Follow [Prepare TTW in Fluorine](preparation.md) first. It covers downloads, folders, conversion and the first test.

<a id="1-get-the-requirements"></a>
## Before you start

- [ ] Native **Fluorine 0.4.1** can launch your **TTW - Flat** profile; utilities pass, and a save reloads.
- [ ] A **TTW - VR** profile has one complete, original **TTWVR 1.1.4** or **1.1.3** mod enabled.
- [ ] Fluorine uses a private prefix and one of the [supported Proton builds](requirements.md#proton).
- [ ] WiVRn connects to your headset, and the [Linux prerequisites](requirements.md#linux-packages) are installed.

Updating an already patched installation? Use [the update procedure](maintenance.md#update-an-existing-installation).

## 1. Download the patch tool

Open a terminal and run:

```sh
git clone https://github.com/ethanfel/ttwvr-linux.git
cd ttwvr-linux
cp tools/setup.example.json setup.json
```

Keep this repository where you cloned it: the generated launcher uses its tools.
Run the remaining commands from this directory, as your normal desktop user.

## 2. Set three paths

Open **setup.json** in a text editor:

```json
{
  "workspace": "~/Games/ttwvr",
  "manager": "apps/fluorine-manager/fluorine-manager",
  "steam_library": "~/.local/share/Steam"
}
```

| Field | What to select |
|---|---|
| `workspace` | Your TTWVR folder, containing `instance`, `config` and `share`. |
| `manager` | The **fluorine-manager executable**. A relative path starts inside the workspace; an absolute path also works. |
| `steam_library` | The Steam library containing **SteamLinuxRuntime_4**. Select the folder **above `steamapps`**. |

The tool reads Proton and the prefix from Fluorine, finds the enabled TTWVR mod, and puts its output in `workspace/linux-fixes`. You do not need to repeat these paths.

**Different folder layout?** Add only the fields that differ using [configuration overrides](configuration.md). Existing full-path configurations still work. Spaces and `~` are supported; JSON does not expand `$HOME` or other shell variables.

<a id="4-check-and-apply"></a>
## 3. Check the installation

```sh
python3 tools/setup_linux.py check setup.json
```

**Continue when:** the command prints **`Preflight passed`**.

Read the displayed mod, Proton and prefix paths to confirm it found your intended installation. This check does not change files. If it reports an error, correct the named path, download or setting and run it again. See [common errors](troubleshooting.md).

## 4. Apply the patch

1. Close Fallout and Fluorine.
2. Run:

```sh
python3 tools/setup_linux.py apply setup.json
```

The first run downloads shader compilers and builds the patched renderer. Leave the terminal open until it finishes; allow several GB for build files and the copied mod.

**Continue when:** the tool prints launch and rollback commands. It creates a separate **TTWVR Linux** mod, activates it in the VR profile, and saves backups under **linux-fixes**. Your original mod, saves and TTWVR settings are retained.

## 5. Launch from Steam

1. Connect your headset in **WiVRn**.
2. Open **workspace/linux-fixes/steam-launch-options.txt**.
3. Copy its entire line.
4. In Steam, right-click **Fallout: New Vegas → Properties → General**.
5. Paste the line into **Launch Options**, replacing any previous TTWVR command.
6. Press **Play**.

Fluorine opens the VR profile and launches Fallout automatically. You can also use the direct launch command printed by the tool.

**Check in the headset:** load a save, turn your head, and confirm both eyes show the scene. Open the mod's **VR Settings** to choose DLSS or FSR, quality level, resolution and sharpness. Existing choices are preserved.

<a id="6-roll-back"></a>
## After installation

[Open the manager or play flat](maintenance.md#launch-options) ·
[Undo the patch](maintenance.md#roll-back) ·
[Update TTWVR or Proton](maintenance.md#update-an-existing-installation) ·
[Troubleshooting](troubleshooting.md)
