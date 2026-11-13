#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Ethanfel and contributors
# SPDX-License-Identifier: GPL-3.0-only
"""Read-only audit. Never loads a DLL or extracts a supplied archive."""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import zipfile

MASTERS = {
    "fnv": ["FalloutNV.esm", "DeadMoney.esm", "HonestHearts.esm",
            "OldWorldBlues.esm", "LonesomeRoad.esm", "GunRunnersArsenal.esm",
            "CaravanPack.esm", "ClassicPack.esm", "MercenaryPack.esm", "TribalPack.esm"],
    "fo3": ["Fallout3.esm", "Anchorage.esm", "ThePitt.esm", "BrokenSteel.esm",
            "PointLookout.esm", "Zeta.esm"],
}


def sha256_file(path):
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def pe_info(data):
    if len(data) < 64 or data[:2] != b"MZ":
        raise ValueError("not a PE executable")
    offset = struct.unpack_from("<I", data, 60)[0]
    if offset + 24 > len(data) or data[offset:offset + 4] != b"PE\0\0":
        raise ValueError("invalid PE header")
    machine = struct.unpack_from("<H", data, offset + 4)[0]
    flags = struct.unpack_from("<H", data, offset + 22)[0]
    return {"machine": {0x14c: "i386", 0x8664: "amd64"}.get(machine, hex(machine)),
            "large_address_aware": bool(flags & 0x20)}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--fnv", required=True, type=Path)
    parser.add_argument("--fo3", required=True, type=Path)
    parser.add_argument("--ttwvr", required=True, type=Path)
    args = parser.parse_args()
    report = {"games": {}, "ttwvr": {}}
    for key, exe in [("fnv", "FalloutNV.exe"), ("fo3", "Fallout3.exe")]:
        root = getattr(args, key)
        if not root.is_dir():
            parser.error(f"{key}: game directory does not exist")
        data_dirs = [p for p in root.iterdir() if p.is_dir() and p.name.casefold() == "data"]
        files = {p.name.casefold() for d in data_dirs for p in d.iterdir() if p.is_file()}
        entry = {"missing_masters": [n for n in MASTERS[key] if n.casefold() not in files],
                 "multiple_data_directories": len(data_dirs) > 1}
        executable = root / exe
        if executable.is_file():
            entry["executable"] = {"sha256": sha256_file(executable),
                                   **pe_info(executable.read_bytes())}
        else:
            entry["missing_executable"] = exe
        report["games"][key] = entry
    with zipfile.ZipFile(args.ttwvr) as archive:
        info = report["ttwvr"]
        info["sha256"] = sha256_file(args.ttwvr)
        info["entry_count"] = len(archive.infolist())
        info["expanded_bytes"] = sum(f.file_size for f in archive.infolist())
        info["binaries"] = []
        for member in archive.infolist():
            if not member.filename.lower().endswith((".dll", ".exe")):
                continue
            if member.file_size > 128 * 1024 * 1024:
                raise ValueError("unexpectedly large binary; inspect archive manually")
            data = archive.read(member)
            info["binaries"].append({"file": member.filename,
                                     "sha256": hashlib.sha256(data).hexdigest(),
                                     **pe_info(data)})
    print(json.dumps(report, indent=2))
    return int(any(g.get("missing_masters") or g.get("missing_executable") or
                   g.get("multiple_data_directories") for g in report["games"].values()))


if __name__ == "__main__":
    raise SystemExit(main())
