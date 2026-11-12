# Troubleshooting the Linux patch

[← Installation](installation.md) · [Fluorine reference](native-fluorine.md)

| Symptom | Action |
|---|---|
| Preflight says unsupported/modified Proton | Use the exact tested build. Steam Experimental updates can invalidate both bridge and renderer hashes; do not bypass guards. |
| Preflight rejects TTWVR | Point `mod` at the complete original 1.1.3/1.1.4 mod, not an already patched copy or mixed-version files. |
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
| Utility warning in game | Finish The Best of Times utilities and their separate INI presets. The patch tool does not replace ML Utilities Checker. |
| WiVRn unavailable / `/run/user/0` | Use the generated launcher and its UID-preserving save-mount adapter. Connect WiVRn before starting. |
| Black headset after loading | Check the OpenXR bridge hash/overlay and that the generated launcher is used. A newer Proton needs a revalidated patch. |
| Original DLSS/FSR menu missing | Disable the archived Rendering Addon/Neural Menu, including renamed copies or conflicting files in overwrite. Current setup uses the original author menu. |
| FSR log still mentions DLSS | The author helper can start even with native FSR active. Inspect native-FSR state and actual fresh stereo output; a label alone is insufficient. |
| Manager waits for Fallout to close | Quit the game first; it is still using the mounted mods and saves. |
| Flat play opens VR | Use `--profile "TTW - Flat"`, disable TTWVR in that profile, and close the previous manager/game first. |
| FPS near half refresh rate | Runtime pacing can produce this at current frame times. 90 Hz does not universally impose a 45 FPS cap. Check GPU time/load as well as application FPS. |

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

Use [the complete rollback](installation.md#6-roll-back), with Fallout and
Fluorine closed. Installed Proton files and global runtime registration were
never changed. Restore any Steam launch option you changed manually.

The [custom addon](../archive/rendering-addon/README.md) and
[Windows MO2 workflow](archive/mo2/README.md) are historical references.
