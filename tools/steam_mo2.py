#!/usr/bin/env python3
"""Experimental Steam launch wrapper; use --dry-run to inspect without writing."""
import argparse
import configparser
import hashlib
import json
import os
from pathlib import Path
import shlex

from patch_openxr_bridge import SOURCE_SHA256, PATCHED_SHA256
from patch_fallback_resolution import SOURCE_SHA256 as TTWVR_SOURCE_SHA256
from patch_fallback_resolution import PATCHED_SHA256 as TTWVR_FALLBACK_SHA256
from dxvk_overlay import private_dxvk_renderer


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


def wants_vr(mode, command, mo2=None, instance=None):
    if mode != "auto":
        return mode == "vr"
    # Prefer MO2's explicit CLI profile over the last selected profile.
    profile = None
    arguments = command[command.index(str(mo2)) + 1:] if mo2 else []
    for i, arg in enumerate(arguments):
        if arg in ("-p", "--profile") and i + 1 < len(arguments):
            profile = arguments[i + 1]
        elif arg.startswith("--profile="):
            profile = arg.partition("=")[2]
    if profile is None:
        config = configparser.RawConfigParser()
        config.read((instance or mo2.parent) / "ModOrganizer.ini")
        profile = config.get("General", "selected_profile", fallback="")
        if profile.startswith("@ByteArray(") and profile.endswith(")"):
            profile = profile[len("@ByteArray("):-1]
    return profile != "TTW - Flat"


def steam_runtime_indices(command):
    return [i for i, arg in enumerate(command)
            if Path(arg).name == "_v2-entry-point" or
            (Path(arg).name == "run" and
             Path(arg).parent.name.startswith("SteamLinuxRuntime_"))]


def use_imported_openxr(command):
    """Let pressure-vessel import the host selection, then use its rewritten JSON."""
    entries = steam_runtime_indices(command)
    protons = [i for i, arg in enumerate(command) if Path(arg).name == "proton"]
    if not entries:
        return command  # Direct Proton launch uses the host manifest as supplied.
    if len(entries) != 1 or len(protons) != 1:
        raise ValueError("cannot identify one Proton command inside the Steam runtime")
    index = protons[0]
    if index <= entries[0] or "--" not in command[entries[0] + 1:index]:
        raise ValueError("unexpected Steam runtime command structure")
    delimiter = command.index("--", entries[0] + 1, index)
    inner = command[delimiter + 1:index]
    # Fluorine inserts env assignments inside SLR before invoking Proton.
    if inner and (inner[0] != "/usr/bin/env" or
                  any("=" not in arg or arg.startswith("-") for arg in inner[1:])):
        raise ValueError("unexpected command before Proton inside the Steam runtime")
    # The importer reads XR_RUNTIME_JSON outside the container and creates a
    # manifest with container library paths in XDG_CONFIG_DIRS. The host value
    # must stop overriding that manifest once inside the container.
    return command[:index] + ["/usr/bin/env", "-u", "XR_RUNTIME_JSON"] + command[index:]


def native_compiler_override(value):
    """Replace only D3DCompiler 47's rule, preserving other DLL selections."""
    rules = []
    for rule in value.split(";"):
        names, separator, order = rule.partition("=")
        if not separator:
            if rule.strip():
                rules.append(rule)
            continue
        names = [name.strip() for name in names.split(",")
                 if name.strip().lower().lstrip("*") != "d3dcompiler_47"]
        if names:
            rules.append(",".join(names) + "=" + order)
    return ";".join(rules + ["d3dcompiler_47=n"])


def private_openxr_bridge(command, bridge):
    """Overlay one verified Unix bridge in this launch's mount namespace."""
    protons = [Path(arg) for arg in command if Path(arg).name == "proton"]
    if len(protons) != 1:
        raise ValueError("cannot identify one Proton installation for the OpenXR patch")
    original = protons[0].parent / "files/lib/wine/x86_64-unix/wineopenxr.so"
    for path, expected in ((original, SOURCE_SHA256), (bridge, PATCHED_SHA256)):
        if hashlib.sha256(path.read_bytes()).hexdigest() != expected:
            raise ValueError(f"OpenXR bridge hash mismatch: {path}; check the Proton version")
    return ["/usr/bin/bwrap", "--bind", "/", "/", "--dev-bind", "/dev", "/dev",
            "--ro-bind", str(bridge), str(original), "--", *command]


def private_fallback_resolution(command, original, patched):
    """Use the smaller fallback DLL only while both supported hashes match."""
    for path, expected in ((original, TTWVR_SOURCE_SHA256),
                           (patched, TTWVR_FALLBACK_SHA256)):
        if hashlib.sha256(path.read_bytes()).hexdigest() != expected:
            raise ValueError(f"TTWVR fallback hash mismatch: {path}; check the mod version")
    return ["/usr/bin/bwrap", "--bind", "/", "/", "--dev-bind", "/dev", "/dev",
            "--ro-bind", str(patched), str(original), "--", *command]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    manager = parser.add_mutually_exclusive_group(required=True)
    manager.add_argument("--mo2", type=Path)
    manager.add_argument("--native-instance", type=Path,
                         help="native MO2-compatible instance; preserve its prepared game command")
    parser.add_argument("--state-dir", type=Path, required=True)
    parser.add_argument("--steam-runtime", type=Path,
                        help="Steam Linux Runtime directory when a native manager cannot locate it")
    parser.add_argument("--xrizer", type=Path, default=Path("/opt/xrizer"))
    parser.add_argument("--xr-runtime", type=Path,
                        default=Path(os.environ.get("TTWVR_XR_RUNTIME_JSON",
                                                    "/usr/share/openxr/1/openxr_wivrn.json")))
    parser.add_argument("--dry-run", action="store_true")
    parser.add_argument("--mode", choices=("auto", "flat", "vr"), default="auto",
                        help="auto disables VR for the TTW - Flat profile; restart MO2 when changing mode")
    parser.add_argument("--wow64", action="store_true",
                        help="test Proton 11's WoW64 mode with the 64-bit native VR bridge")
    parser.add_argument("--native-d3dcompiler47", action="store_true",
                        help="use an installed native D3DCompiler 47 for VR launches")
    parser.add_argument("--openxr-bridge", type=Path,
                        help="private patched wineopenxr.so for the supported Proton build")
    parser.add_argument("command", nargs=argparse.REMAINDER)
    args = parser.parse_args()
    command = args.command[1:] if args.command[:1] == ["--"] else args.command
    if not command:
        parser.error("supply Steam's command after --")
    mo2 = args.mo2.expanduser().resolve() if args.mo2 else None
    instance = args.native_instance.expanduser().resolve() if args.native_instance else mo2.parent
    state = args.state_dir.expanduser().resolve()
    xrizer = args.xrizer.expanduser().resolve()
    runtime = args.xr_runtime.expanduser().resolve()
    try:
        if mo2:
            command = replace_game(command, mo2)
        else:
            matches = [arg for arg in command
                       if arg.replace("\\", "/").rsplit("/", 1)[-1].lower()
                       in {"falloutnv.exe", "falloutnvlauncher.exe", "nvse_loader.exe"}]
            if len(matches) != 1:
                raise ValueError("expected exactly one Fallout/NVSE executable in native launch")
        vr = wants_vr(args.mode, command, mo2, instance)
        if args.steam_runtime:
            steam_runtime = args.steam_runtime.expanduser().resolve()
            entry = steam_runtime / "_v2-entry-point"
            if not entry.is_file():
                raise ValueError(f"Steam runtime entry point not found: {entry}")
            entries = steam_runtime_indices(command)
            if entries:
                if len(entries) != 1 or Path(command[entries[0]]).parent.resolve() != steam_runtime:
                    raise ValueError("supplied Steam runtime differs from the prepared command")
            else:
                command = [str(entry), "--verb=waitforexitandrun", "--", *command]
        compatibility = configparser.RawConfigParser()
        compatibility.read(instance / "ttwvr-linux.ini")
        vr_wow64 = compatibility.getboolean("VR", "wow64", fallback=False)
        vr_compiler = compatibility.getboolean("VR", "native_d3dcompiler47", fallback=False)
        vr_neural = compatibility.getboolean("VR", "neural_rendering", fallback=False)
        bridge_setting = compatibility.get("VR", "openxr_bridge", fallback="")
        bridge = args.openxr_bridge or (Path(bridge_setting) if bridge_setting else None)
        if bridge and not bridge.is_absolute():
            bridge = instance / bridge
        fallback_paths = []
        for key in ("fallback_resolution_source", "fallback_resolution_patch"):
            value = compatibility.get("VR", key, fallback="")
            path = Path(value).expanduser() if value else None
            if path and not path.is_absolute():
                path = instance / path
            fallback_paths.append(path.resolve() if path else None)
        dxvk_paths = []
        for key in ("dxvk_renderer", "dxvk_syswow64"):
            value = compatibility.get("VR", key, fallback="")
            path = Path(value).expanduser() if value else None
            if path and not path.is_absolute():
                path = instance / path
            dxvk_paths.append(path.resolve() if path else None)
    except (ValueError, configparser.Error) as exc:
        parser.error(str(exc))
    manager_file = mo2 or instance / "ModOrganizer.ini"
    for path in ((manager_file, runtime) if vr else (manager_file,)):
        if not path.is_file():
            parser.error(f"required file not found: {path}")
    if vr and not any((xrizer / "bin" / arch / "vrclient.so").is_file()
               for arch in ("linux64", "linux32")):
        parser.error("xrizer directory contains no native vrclient.so")
    if any(":" in str(p) or "\n" in str(p) for p in (state, xrizer)):
        parser.error("state and xrizer paths cannot contain colon or newline")
    try:
        if vr:
            runtime_manifest = json.loads(runtime.read_text())
            if not runtime_manifest["runtime"]["library_path"]:
                raise ValueError("missing runtime library")
            command = use_imported_openxr(command)
        if vr and bridge:
            command = private_openxr_bridge(command, bridge.resolve())
        if vr and any(fallback_paths):
            if not all(fallback_paths):
                raise ValueError("set both fallback_resolution_source and fallback_resolution_patch")
            command = private_fallback_resolution(command, *fallback_paths)
        if vr and any(dxvk_paths):
            if not all(dxvk_paths):
                raise ValueError("set both dxvk_renderer and dxvk_syswow64")
            command = private_dxvk_renderer(command, *dxvk_paths,
                                            os.environ.get("STEAM_COMPAT_DATA_PATH"))
    except (KeyError, OSError, ValueError) as exc:
        parser.error(str(exc))

    bindings = [p for p in os.environ.get("PRESSURE_VESSEL_FILESYSTEMS_RW", "").split(":") if p]
    for path in [str(state), str(instance)] + ([str(xrizer)] if vr else []):
        if path not in bindings:
            bindings.append(path)
    changes = {
        "VR_PATHREG_OVERRIDE": str(state / "openvrpaths.vrpath"),
        "VR_OVERRIDE": container_path(xrizer) if vr else None,
        "PROTON_VR_RUNTIME": None,
        "XR_RUNTIME_JSON": str(runtime) if vr else None,
        "PRESSURE_VESSEL_IMPORT_OPENXR_1_RUNTIMES": "1" if vr else "0",
        "DXVK_NO_VR": None if vr else "1",
        "PRESSURE_VESSEL_FILESYSTEMS_RW": ":".join(bindings),
        "PROTON_LOG": "1",
        "PROTON_LOG_DIR": str(state),
        "ENABLE_TTWVR_NEURAL_RENDERING": "1" if vr and vr_neural else None,
    }
    if vr and vr_neural:
        changes["PROTON_ENABLE_NVAPI"] = "1"
        changes["PROTON_HIDE_NVIDIA_GPU"] = "0"
    if args.wow64 or (vr and vr_wow64):
        changes["PROTON_USE_WOW64"] = "1"
    if vr and (args.native_d3dcompiler47 or vr_compiler):
        changes["WINEDLLOVERRIDES"] = native_compiler_override(
            os.environ.get("WINEDLLOVERRIDES", ""))
    if vr and all(dxvk_paths):
        changes["DXVK_CONFIG"] = "; ".join(filter(None, [
            os.environ.get("DXVK_CONFIG", ""), "d3d11.disableDirectImageMapping = True"]))
    registry = {"version": 1, "runtime": [container_path(xrizer)] if vr else []}
    if args.dry_run:
        print(json.dumps({"mode": "vr" if vr else "flat", "environment": changes, "openvr_registry": registry,
                          "command": command, "shell_preview": shlex.join(command)}, indent=2))
        return 0
    state.mkdir(parents=True, exist_ok=True)
    registry_file = state / "openvrpaths.vrpath"
    if registry_file.is_symlink():
        parser.error("refusing to overwrite a symlinked OpenVR registry")
    registry_file.write_text(json.dumps(registry, indent=2) + "\n")
    environment = os.environ.copy()
    for key, value in changes.items():
        if value is None:
            environment.pop(key, None)
        else:
            environment[key] = value
    os.execvpe(command[0], command, environment)


if __name__ == "__main__":
    raise SystemExit(main())
