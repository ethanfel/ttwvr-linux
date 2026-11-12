#!/usr/bin/env python3
"""Read TTWVR 1.1.3/1.1.4 upscaler counters from a specified Linux/Wine process.

Read-only diagnostic. These are the original status-panel counters, not proof
of valid or fresh pixels. Capture both eyes and test motion separately.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import struct

SUPPORTED = {
    "8384261c4b5fafd90a7fe860ccb7e1e24953a653c2fdbeafb8bd46ab34485f64",
    "ee9e2e1b1d2ae120244ca53f033327ee8bad83fb43a7ffa1b79753c17e50270f",
    "f8a8eb95070eaaee0041fd8901f69e1150abea3cf8115ac7206c24a153c2dedf",
    "4c45d6e5adb5da76dd3d08bf0639d360761d3dc50d891ea0bd19b51e4fb006a0",
    "b82d341690cf0068b561803dca22a4ac07ea8042e6437c68f3ab9a85fcd16530",
}
# 1.1.4 status formatter and resource-state instructions independently reviewed.
BUILD_114 = {'7eb72860d209d9b9bdd774607220ee63323367aa9fa525f6eb1b23184f30f91e', 'd35cc677ea792bbfbb21936656cfdb2d4bc023a1a4e757b79eb8cb31a2e00616', 'e54dccb5e29e1361d36dd9f9a7ee23294b07663f29690cabbaff7168cbd62413', '216e74fae62df07f7b1cea9857097288f1d946b510b5d6bb26effb368efcf127'}
SUPPORTED |= BUILD_114
# Independently checked against the supplied 1.1.3 status formatter.
COUNTERS = {
    "render_width": 0x19987B4, "render_height": 0x19987B8,
    "output_width": 0x19987BC, "output_height": 0x19987C0,
    "posted": 0x19CA4C0, "shown": 0x19CA4C4,
    "not_produced": 0x19CA4C8, "slot_busy": 0x19CA4CC,
    "resets": 0x19CA4D0, "worker_posts": 0x19CA804,
    "legacy_posts": 0x19CAA08, "ready_when_shown": 0x19CAA0C,
    "show_checks": 0x19CAA20,
    "upscaler": 0x1998828,
    "native_fsr_frames_since_status_reset": 0x19ED644,
}


def inspect(pid):
    proc = Path("/proc") / str(pid)
    command = (proc / "cmdline").read_bytes().split(b"\0")[0].lower()
    if not command.endswith(b"falloutnv.exe"):
        raise ValueError("PID is not FalloutNV.exe")
    mappings = [line.split(maxsplit=5) for line in
                (proc / "maps").read_text().splitlines()]
    candidates = [row for row in mappings if len(row) == 6
                  and row[5].endswith("/TTWVR.dll") and int(row[2], 16) == 0]
    if len(candidates) != 1:
        raise ValueError("expected exactly one mapped TTWVR.dll image base")
    mapping = candidates[0]
    base = int(mapping[0].split("-")[0], 16)
    # Read through the process namespace, including launch-scoped overlays.
    dll = proc / "root" / mapping[5].lstrip("/")
    digest = hashlib.sha256(dll.read_bytes()).hexdigest()
    if digest not in SUPPORTED:
        raise ValueError("unsupported mapped DLL hash; refusing unverified offsets")
    shift = 0x64978 if digest in BUILD_114 else 0
    fd = os.open(proc / "mem", os.O_RDONLY)
    try:
        def read(rva, size):
            data = os.pread(fd, size, base + rva + shift)
            if len(data) != size:
                raise ValueError("short process-memory read")
            return data
        result = {name: struct.unpack("<I", read(rva, 4))[0]
                  for name, rva in COUNTERS.items()}
        result["helper_mode_label"] = read(0x19CA508, 96).split(b"\0")[0].decode(errors="replace")
        result["copy_refused"] = bool(read(0x1998E52, 1)[0])
        for name, rva in (("direct_output_capability", 0x19987B1),
                          ("direct_output_active", 0x19987B0),
                          ("native_fsr_enabled", 0x1998830),
                          ("native_fsr_failed", 0x1998831),
                          ("native_fsr_initialized", 0x19ED2EC),
                          ("fsr_fp16_allowed", 0x1B20C18)):
            result[name] = bool(read(rva, 1)[0])
        result["native_fsr_active"] = (result["upscaler"] == 2
            and result["native_fsr_enabled"] and not result["native_fsr_failed"])
    finally:
        os.close(fd)
    return {"pid": pid, "ttwvr_sha256": digest,
            "notice": "non-atomic status counters; pixels require separate validation",
            **result}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("pid", type=int)
    args = parser.parse_args()
    if args.pid <= 0:
        parser.error("PID must be positive")
    try:
        result = inspect(args.pid)
    except (OSError, ValueError) as exc:
        parser.error(str(exc))
    print(json.dumps(result, indent=2))


if __name__ == "__main__":
    main()
