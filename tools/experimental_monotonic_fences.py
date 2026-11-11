#!/usr/bin/env python3
"""Create a DIAGNOSTIC Proton copy using native, monotonic shared timelines.

NOT a general D3D12-fence fix: D3D fences may rewind, Vulkan timelines may not.
This skips Wine's process-local rewind-emulation metadata at semaphore
creation. Native GPU synchronization remains active. Use only in an isolated
test launch whose fences have been established to be strictly increasing.
Never replace an installed Proton library with this output.
"""
import argparse
import hashlib
import json
from pathlib import Path

SOURCE_SHA256 = "d2f34ac08f9768a75457957b28a50c4f517cb919489f02f9e07e364c7ec8a75c"
PATCHED_SHA256 = "689b18550d5485ea0a21d8bfc0dd886ac8d8f6a21df549d9a6a3b73ae513dd33"
OFFSET = 0x147DDE


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    try:
        original = args.source.read_bytes()
        if hashlib.sha256(original).hexdigest() != SOURCE_SHA256:
            raise ValueError("unsupported win32u.so hash")
        if original[OFFSET:OFFSET + 2] != b"\x74\x45":
            raise ValueError("unexpected fence-emulation branch")
        patched = bytearray(original)
        patched[OFFSET] = 0xEB
        if hashlib.sha256(patched).hexdigest() != PATCHED_SHA256:
            raise ValueError("unexpected output hash")
        args.output.parent.mkdir(parents=True, exist_ok=True)
        with args.output.open("xb") as stream:
            stream.write(patched)
        args.output.chmod(0o755)
    except (OSError, ValueError) as exc:
        parser.error(str(exc))
    print(json.dumps({"status": "diagnostic; monotonic fences only",
                      "source_sha256": SOURCE_SHA256,
                      "patched_sha256": PATCHED_SHA256,
                      "changed_bytes": 1, "offset": hex(OFFSET)}, indent=2))


if __name__ == "__main__":
    main()
