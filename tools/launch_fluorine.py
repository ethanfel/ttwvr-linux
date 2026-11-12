#!/usr/bin/env python3
"""Start a dedicated native Fluorine instance, directly or from Steam."""
import argparse
import json
import os
from pathlib import Path
import shlex


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--manager", type=Path, required=True)
    parser.add_argument("--instance", type=Path, required=True)
    parser.add_argument("--config-home", type=Path, required=True)
    parser.add_argument("--data-home", type=Path, required=True)
    parser.add_argument("--profile", default="TTW - VR")
    parser.add_argument("--executable", default="Tale of Two Wastelands")
    parser.add_argument("--manager-only", action="store_true")
    parser.add_argument("--dry-run", action="store_true")
    parser.add_argument("steam_command", nargs=argparse.REMAINDER,
                        help="Steam's %%command%% after --; Fluorine prepares the actual Proton launch")
    args = parser.parse_args()
    manager = args.manager.expanduser().resolve()
    instance = args.instance.expanduser().resolve()
    shim_dir = Path(__file__).resolve().parent / "fluorine-bin"
    for file in (manager, instance / "ModOrganizer.ini", shim_dir / "unshare"):
        if not file.is_file():
            parser.error(f"required file not found: {file}")
    command = [str(manager), "--instance", str(instance), "-p", args.profile]
    if not args.manager_only:
        command += ["run", "-e", args.executable]
    changes = {
        "XDG_CONFIG_HOME": str(args.config_home.expanduser().resolve()),
        "XDG_DATA_HOME": str(args.data_home.expanduser().resolve()),
        "PATH": str(shim_dir) + os.pathsep + os.environ.get("PATH", os.defpath),
    }
    if args.dry_run:
        print(json.dumps({"command": command, "environment": changes,
                          "shell_preview": shlex.join(command)}, indent=2))
        return 0
    os.execvpe(command[0], command, os.environ | changes)


if __name__ == "__main__":
    raise SystemExit(main())
