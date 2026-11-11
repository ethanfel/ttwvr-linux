#!/usr/bin/env python3
"""Experimental Steam launch wrapper; use --dry-run to inspect without writing."""
import argparse
import json
import os
from pathlib import Path
import shlex


def replace_game(command, mo2):
    matches = [i for i, arg in enumerate(command)
               if arg.replace("\\", "/").rsplit("/", 1)[-1].lower()
               in {"falloutnv.exe", "falloutnvlauncher.exe"}]
    if len(matches) != 1:
        raise ValueError("expected exactly one FalloutNV.exe/FalloutNVLauncher.exe argument "
                         "in Steam's %command%; refusing to guess")
    result = list(command)
    result[matches[0]] = str(mo2)
    return result


def container_path(path):
    value = str(path)
    return "/run/host" + value if value == "/usr" or value.startswith("/usr/") else value


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--mo2", type=Path, required=True)
    parser.add_argument("--state-dir", type=Path, required=True)
    parser.add_argument("--xrizer", type=Path, default=Path("/opt/xrizer"))
    parser.add_argument("--xr-runtime", type=Path,
                        default=Path("/usr/share/openxr/1/openxr_wivrn.json"))
    parser.add_argument("--dry-run", action="store_true")
    parser.add_argument("--wow64", action="store_true",
                        help="test Proton 11's WoW64 mode with the 64-bit native VR bridge")
    parser.add_argument("command", nargs=argparse.REMAINDER)
    args = parser.parse_args()
    command = args.command[1:] if args.command[:1] == ["--"] else args.command
    if not command:
        parser.error("supply Steam's command after --")
    mo2 = args.mo2.expanduser().resolve()
    state = args.state_dir.expanduser().resolve()
    xrizer = args.xrizer.expanduser().resolve()
    runtime = args.xr_runtime.expanduser().resolve()
    for path in (mo2, runtime):
        if not path.is_file():
            parser.error(f"required file not found: {path}")
    if not any((xrizer / "bin" / arch / "vrclient.so").is_file()
               for arch in ("linux64", "linux32")):
        parser.error("xrizer directory contains no native vrclient.so")
    if any(":" in str(p) or "\n" in str(p) for p in (state, xrizer)):
        parser.error("state and xrizer paths cannot contain colon or newline")
    try:
        runtime_manifest = json.loads(runtime.read_text())
        if not runtime_manifest["runtime"]["library_path"]:
            raise ValueError("missing runtime library")
        command = replace_game(command, mo2)
    except (KeyError, ValueError) as exc:
        parser.error(str(exc))

    bindings = [p for p in os.environ.get("PRESSURE_VESSEL_FILESYSTEMS_RW", "").split(":") if p]
    for path in (str(state), str(xrizer), str(mo2.parent)):
        if path not in bindings:
            bindings.append(path)
    changes = {
        "VR_PATHREG_OVERRIDE": str(state / "openvrpaths.vrpath"),
        "VR_OVERRIDE": container_path(xrizer),
        "XR_RUNTIME_JSON": str(runtime),
        "PRESSURE_VESSEL_IMPORT_OPENXR_1_RUNTIMES": "1",
        "PRESSURE_VESSEL_FILESYSTEMS_RW": ":".join(bindings),
        "PROTON_LOG": "1",
        "PROTON_LOG_DIR": str(state),
    }
    if args.wow64:
        changes["PROTON_USE_WOW64"] = "1"
    registry = {"version": 1, "runtime": [container_path(xrizer)]}
    if args.dry_run:
        print(json.dumps({"environment": changes, "openvr_registry": registry,
                          "command": command, "shell_preview": shlex.join(command)}, indent=2))
        return 0
    state.mkdir(parents=True, exist_ok=True)
    registry_file = state / "openvrpaths.vrpath"
    if registry_file.is_symlink():
        parser.error("refusing to overwrite a symlinked OpenVR registry")
    registry_file.write_text(json.dumps(registry, indent=2) + "\n")
    environment = os.environ.copy()
    environment.update(changes)
    os.execvpe(command[0], command, environment)


if __name__ == "__main__":
    raise SystemExit(main())
