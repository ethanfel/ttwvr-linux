#!/usr/bin/env python3
"""Create a separate TTWVR 1.1.0 DLL with 1920x1440 fallback rendering defaults."""
import argparse
import hashlib
import json
from pathlib import Path
import struct

SOURCE_SHA256 = "8a3a71b48d362984fe346c73b68dfbd476d6e138d6831f26f10c6f506cd03880"
PATCHED_SHA256 = "12c3edcaa07cef1bd8bcab3a9fefc7bad9b4bcc2e738a34d8043e8ad686f43e5"
PATCHES = [
    (0x98EF5, struct.pack("<I", 2944), struct.pack("<I", 1920)),
    (0x98EFF, struct.pack("<I", 2208), struct.pack("<I", 1440)),
    (0x98F09, struct.pack("<I", 2944), struct.pack("<I", 1920)),
    (0x98F13, struct.pack("<I", 2208), struct.pack("<I", 1440)),
    (0x84F598, b"2944\0", b"1920\0"),
    (0x84F5B0, b"2208\0", b"1440\0"),
]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    try:
        original = args.source.read_bytes()
        if hashlib.sha256(original).hexdigest() != SOURCE_SHA256:
            raise ValueError("unsupported TTWVR.dll hash; refusing another build")
        result = bytearray(original)
        for offset, before, after in PATCHES:
            if original[offset:offset + len(before)] != before:
                raise ValueError(f"unexpected rendering default at {offset:#x}")
            result[offset:offset + len(after)] = after
        if hashlib.sha256(result).hexdigest() != PATCHED_SHA256:
            raise ValueError("unexpected patched output hash")
        args.output.parent.mkdir(parents=True, exist_ok=True)
        with args.output.open("xb") as stream:
            stream.write(result)
    except (OSError, ValueError) as exc:
        parser.error(str(exc))
    print(json.dumps({"source_sha256": SOURCE_SHA256,
                      "patched_sha256": PATCHED_SHA256,
                      "changed_bytes": 11,
                      "fallback_size": [1920, 1440]}, indent=2))


if __name__ == "__main__":
    main()
