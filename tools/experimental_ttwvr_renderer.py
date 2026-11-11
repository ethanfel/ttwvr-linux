#!/usr/bin/env python3
"""Create an EXPERIMENTAL TTWVR 1.1.0 copy that loads its bundled DXVK.

This bypasses only the stock-DXVK detection return. It is not a proven
performance fix. Keep the original, test with a launch-scoped overlay, and
do not redistribute the resulting DLL.
"""
import argparse
import hashlib
import json
from pathlib import Path


SOURCE_SHA256 = "8a3a71b48d362984fe346c73b68dfbd476d6e138d6831f26f10c6f506cd03880"
OFFSET = 0x4F8E4C
BEFORE = bytes.fromhex("0f 85 89 01 00 00")
AFTER = b"\x90" * 6
LOADER_OFFSET = 0xA1FEB0
LOADER_BEFORE = "vulkan-1.dll".encode("utf-16le").ljust(32, b"\x00")
LOADER_AFTER = "winevulkan.dll".encode("utf-16le").ljust(32, b"\x00")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument("--winevulkan", action="store_true",
                        help="also look up DXVK's actual Wine Vulkan module")
    args = parser.parse_args()
    try:
        original = args.source.read_bytes()
        if hashlib.sha256(original).hexdigest() != SOURCE_SHA256:
            raise ValueError("unsupported TTWVR.dll hash; refusing another build")
        if original[OFFSET:OFFSET + len(BEFORE)] != BEFORE:
            raise ValueError("unexpected renderer-detection instruction")
        patched = bytearray(original)
        patched[OFFSET:OFFSET + len(BEFORE)] = AFTER
        if args.winevulkan:
            if original[LOADER_OFFSET:LOADER_OFFSET + 32] != LOADER_BEFORE:
                raise ValueError("unexpected Vulkan loader string or padding")
            patched[LOADER_OFFSET:LOADER_OFFSET + 32] = LOADER_AFTER
        args.output.parent.mkdir(parents=True, exist_ok=True)
        with args.output.open("xb") as stream:
            stream.write(patched)
    except (OSError, ValueError) as exc:
        parser.error(str(exc))
    print(json.dumps({
        "status": "experimental; runtime validation required",
        "source_sha256": SOURCE_SHA256,
        "patched_sha256": hashlib.sha256(patched).hexdigest(),
        "file_offset": hex(OFFSET), "rva": "0x4f9a4c",
        "before": BEFORE.hex(), "after": AFTER.hex(),
        "changed_bytes": sum(a != b for a, b in zip(original, patched)),
        "winevulkan_lookup": args.winevulkan,
    }, indent=2))


if __name__ == "__main__":
    main()
