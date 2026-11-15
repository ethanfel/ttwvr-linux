# Troubleshooting the Linux patch

[← Installation](installation.md) · [Fluorine reference](native-fluorine.md)

| Symptom | Action |
|---|---|
| Configuration cannot find the instance | Set `workspace` to the folder containing `instance`, or add an explicit `instance` override. Relative paths start at workspace. |
| Configuration finds no TTWVR mod or several candidates | Enable exactly one original TTWVR mod in the selected VR profile, or set `mod` explicitly. The complete archive must provide `NVSE/Plugins/TTWVR.dll`. |
| Missing Proton/prefix setting | Finish the native instance preparation; the tool reads `[fluorine] proton_path` and `prefix_path` from its ModOrganizer.ini. |
| Preflight says unsupported/modified Proton | Use the exact tested build. Steam Experimental updates can invalidate both bridge and renderer hashes; do not bypass guards. |
| Preflight rejects TTWVR | Point `mod` at the complete original 1.1.3/1.1.4/1.1.5/1.1.6 mod, not an already patched copy or mixed-version files. |
| Existing transaction/destination mod | This tool does not overwrite previous patch deployments. Roll back first; retain backups, then use a fresh fixes directory and original mod. A retained copied mod can be moved aside while disabled. |
| Missing compiler/build command | Install the listed distro prerequisite. MinGW must target i686 with POSIX threads. |
| DXVK build fails | Inspect the first compiler error. After correcting prerequisites, remove only the reported incomplete dependency source/build directories and retry `prepare`; the prepared game has not been activated. |
| Rollback reports changed files | It protects edits made since apply. Save them manually or use `rollback ... --force`, which also snapshots current conflicting files. |
| Fluorine reports FUSE permission denied | Start through the generated launcher on the host. Do not wrap the manager itself in bubblewrap. Ensure FUSE 3 works for your desktop user. |
| Menu crash / missing BSA assets | Use real copies or reflinks for mod assets; the tested Fluorine skips symlinked files. Rebuild its file index after correcting them. |
| Window says “ModOrganizer” | Native Fluorine inherits these names. The launched manager should be `fluorine-manager` / `ModOrganizer-core`, not Windows `ModOrganizer.exe`. |
| TTW game plugin missing | Select Fluorine's `TTW` plugin (`gameName=TTW`), not the Windows plugin name. |
| Registry game path mismatch | Verify the managed game and private prefix first. Accept the path update when they are the intended copy. |
| Missing GECK INIs | Accept the manager's copy prompt; these are editor INIs. |
| Wrong prefix/game starts | Check setup.json, instance settings and dedicated global `fluorine/config.json`; global paths take precedence. |
| `steam_mo2.py`: "supplied Steam runtime differs from the prepared command" | Try removing the explicit `--steam-runtime` argument and its path from the executable's **Wrapper options in Fluorine**, retaining `-- %command%`. See [Steam runtime mismatch](#steam-runtime-mismatch). |
| Utility warning in game | Finish The Best of Times utilities and their separate INI presets. The patch tool does not replace ML Utilities Checker. |
| WiVRn unavailable / `/run/user/0` | Use the generated launcher and its UID-preserving save-mount adapter. Connect WiVRn before starting. |
| Black headset after loading | Check the OpenXR bridge hash/overlay and that the generated launcher is used. A newer Proton needs a revalidated patch. |
| Original DLSS/FSR menu missing | Disable the archived Rendering Addon/Neural Menu, including renamed copies or conflicting files in overwrite. Current setup uses the original author menu. |
| FSR log still mentions DLSS | The author helper can start even with native FSR active. Inspect native-FSR state and actual fresh stereo output; a label alone is insufficient. |
| Manager waits for Fallout to close | Quit the game first; it is still using the mounted mods and saves. |
| Flat play opens VR | Use `--profile "TTW - Flat"`, disable TTWVR in that profile, and close the previous manager/game first. |
| FPS near half refresh rate | Runtime pacing can produce this at current frame times. 90 Hz does not universally impose a 45 FPS cap. Check GPU time/load as well as application FPS. |

## Steam runtime mismatch

If `steam_mo2.py` reports:

```text
supplied Steam runtime differs from the prepared command
```

Fluorine has already included a Steam runtime in its prepared launch command,
but the wrapper's explicit `--steam-runtime` setting conflicts with it.
The explicit setting is a fallback for cases where Fluorine cannot locate a
runtime, such as some secondary-library setups.

A user-confirmed workaround:

1. Open the selected game executable's **Wrapper options inside Fluorine**.
2. Remove only `--steam-runtime` and the path immediately following it, for example:

   ```text
   --steam-runtime "/path/to/SteamLinuxRuntime_4"
   ```

3. Keep all other arguments, including the final `-- %command%`.
4. Save the executable settings and retry the launch.

This lets the wrapper retain the runtime Fluorine already selected. Make this
change in Fluorine's executable settings, not in Steam's Launch Options.

If it still fails, include the complete **Wrapper options** and the latest log
from `<instance>/logs/` in your support report. Those show the prepared command
and runtime paths; a fresh Proton log may not exist because the wrapper rejects
this launch before running Proton.

## Logs and checks

Proton logs are under **fixes/state/steam-22380.log**. Fluorine's instance logs
show its prepared command and mount failures. TTWVR logs are under the private
prefix's `drive_c/users/steamuser/Documents/My Games/FalloutNV/`.
Use `python3 fixes/launch-ttwvr.py --dry-run` to inspect manager/profile paths.
For the FPS overlay, prepend `env DXVK_HUD=fps,frametimes` to the generated
launch command.

The read-only `tools/inspect_ttwvr_upscaler.py <Linux-PID>` recognizes supported
TTWVR builds. Successful initialization is not proof of fresh rendering: check
both eyes, consecutive frames and head movement. See [DLSS evidence](author-dlss.md)
and [native FSR evidence](native-vulkan-fsr.md).

## Undo a fix

Use [the complete rollback](maintenance.md#roll-back), with Fallout and
Fluorine closed. Installed Proton files and global runtime registration were
never changed. Restore any Steam launch option you changed manually.

The [custom addon](../archive/rendering-addon/README.md) and
[Windows MO2 workflow](archive/mo2/README.md) are historical references.
