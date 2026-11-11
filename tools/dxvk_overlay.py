"""Launch-scoped DXVK source overrides with a separate writable Windows directory."""
import hashlib
import json
from pathlib import Path


ORIGINAL_HASHES = {
    "d3d11.dll": "60a5274a29fcb4dcc1938e5ff9ca89be64bfcbdd45e5a9ed98a5279f3283d877",
    "d3d9.dll": "905fc2bf411d9acced7b1c558f7798bb6a2338b81c7055425c3e5cf0e4b71d96",
    "dxgi.dll": "2c430f5241fb7162154f01397e5a915db779c67adca76c92e5828b655d870f78",
}


def private_dxvk_renderer(command, renderer, windows, compatdata):
    protons = [Path(arg) for arg in command if Path(arg).name == "proton"]
    if len(protons) != 1 or not compatdata:
        raise ValueError("DXVK overlay requires one Proton command and STEAM_COMPAT_DATA_PATH")
    destination = Path(compatdata).resolve() / "pfx/drive_c/windows/syswow64"
    if not windows.is_dir() or not (windows / "kernel32.dll").is_file():
        raise ValueError("DXVK overlay requires a prepared private syswow64 directory")
    if windows.samefile(destination):
        raise ValueError("DXVK private syswow64 directory must differ from the real prefix")
    hashes = json.loads((renderer / "hashes.json").read_text())
    if not isinstance(hashes, dict) or set(hashes) != set(ORIGINAL_HASHES):
        raise ValueError("DXVK hashes.json must list exactly d3d9.dll, d3d11.dll and dxgi.dll")
    source = protons[0].parent / "files/lib/wine/dxvk/i386-windows"
    bindings = []
    for name, original_hash in ORIGINAL_HASHES.items():
        original, candidate = source / name, renderer / name
        if hashlib.sha256(original.read_bytes()).hexdigest() != original_hash:
            raise ValueError(f"DXVK original hash mismatch: {name}; check the Proton version")
        if hashlib.sha256(candidate.read_bytes()).hexdigest() != hashes[name]:
            raise ValueError(f"DXVK candidate hash mismatch: {name}; rebuild the private renderer")
        bindings += ["--ro-bind", str(candidate), str(original)]
    return ["/usr/bin/bwrap", "--bind", "/", "/", "--dev-bind", "/dev", "/dev",
            "--bind", str(windows), str(destination), *bindings, "--", *command]
