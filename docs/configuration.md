# setup.json reference

[← Installation](installation.md)

Use [the three-field template](../tools/setup.example.json) for the standard workspace. Add overrides only when your existing installation uses different locations.

## How paths are resolved

- `workspace` is an absolute path or a path beginning with `~`.
- Every relative path in setup.json starts at **workspace**, regardless of where setup.json is saved.
- Absolute paths and `~` work for every path field.
- Paths can contain spaces. Shell variables such as `$HOME` are not expanded.
- Without `workspace`, every path must be absolute or begin with `~`, as in older configurations.

The tool only reads existing configuration during discovery. It does not create a prefix, move mods or change Fluorine settings.

## Defaults and overrides

| Field | Default / discovery | Override when… |
|---|---|---|
| `instance` | `workspace/instance` | Your portable instance is elsewhere. |
| `config_home` | `workspace/config` | You used another dedicated XDG_CONFIG_HOME for Fluorine. |
| `data_home` | `workspace/share` | You used another dedicated XDG_DATA_HOME for Fluorine. |
| `fixes` | `workspace/linux-fixes` | You need a fresh output directory for a new deployment. |
| `proton` | `[fluorine] proton_path` in the instance's ModOrganizer.ini | You want an explicit path; it must still match Fluorine. |
| `prefix` | `[fluorine] prefix_path` in the same INI | You want an explicit path ending in `/pfx`; it must match Fluorine. |
| `mod` | The sole enabled mod containing `NVSE/Plugins/TTWVR.dll` | More than one candidate exists, or you want to name it explicitly. |
| `steam_runtime` | `steam_library/steamapps/common/SteamLinuxRuntime_4` | The runtime lives outside that library. This can replace `steam_library`. |
| `profile` | `TTW - VR` | Your VR profile has another name. Keep the flat profile named `TTW - Flat`. |
| `executable` | `Tale of Two Wastelands` | The prepared FalloutNV.exe entry has another title. |
| `xrizer` | `/opt/xrizer` | Your xrizer folder is elsewhere; it must contain `bin/linux64/vrclient.so`. |
| `xr_runtime` | `/usr/share/openxr/1/openxr_wivrn.json` | WiVRn's OpenXR manifest is installed elsewhere. |

`config_home` is the folder **containing** `fluorine/config.json`, not the JSON file itself. Mod and profile directories must remain inside the instance.

An explicit mod path must name the installed original mod under `instance/mods`, not its ZIP. The tool still checks that it is enabled and has supported original files. Ambiguous discovery stops with an error instead of choosing arbitrarily.

`fixes` must be separate from the instance, mod, prefix and Proton directories. Keep this folder and the repository in place after applying.

## Example: existing folders with a few overrides

```json
{
  "workspace": "/games/ttwvr",
  "manager": "/opt/fluorine-manager/fluorine-manager",
  "steam_library": "/games/SteamLibrary",
  "instance": "my-ttw-instance",
  "fixes": "linux-fixes-115",
  "xrizer": "apps/xrizer"
}
```

This selects `/games/ttwvr/my-ttw-instance` and `/games/ttwvr/apps/xrizer`. Proton, prefix and TTWVR mod still come from that instance.

## Reuse local build dependencies

These optional fields are for previously verified local files:

| Field | Expected input |
|---|---|
| `compiler32` | The supported 32-bit native d3dcompiler_47 DLL. |
| `compiler64` | The supported 64-bit native d3dcompiler_47 DLL. |
| `dxvk_dir` | The exact supported renderer bundle, including `hashes.json`. |

Checksums must match. Omit them to let the tool download the compilers and build the pinned DXVK source. [Dependency and patch details](linux-fixes.md).
