#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Ethanfel and contributors
# SPDX-License-Identifier: GPL-3.0-only
"""Create an EXPERIMENTAL TTWVR 1.1.0/1.1.3/1.1.4/1.1.5 copy that loads its bundled DXVK.

This bypasses only the stock-DXVK detection return. It is not a proven
performance fix. Keep the original, test with a launch-scoped overlay, and
do not redistribute the resulting DLL.
"""
import argparse
import hashlib
import json
from pathlib import Path


# Exact original DLL hashes; offsets independently checked in each build.
BUILDS = {
    "e14567bc1c7d183130f902f9489b8cffe6269de06eea0d79bf8d1d3098c595b1":
        ("1.1.5", 0x550B4C, 0x55174C, 0xA83870),
    "7eb72860d209d9b9bdd774607220ee63323367aa9fa525f6eb1b23184f30f91e":
        ("1.1.4", 0x52618C, 0x526D8C, 0xA51FD0),
    "8a3a71b48d362984fe346c73b68dfbd476d6e138d6831f26f10c6f506cd03880":
        ("1.1.0", 0x4F8E4C, 0x4F9A4C, 0xA1FEB0),
    "4c45d6e5adb5da76dd3d08bf0639d360761d3dc50d891ea0bd19b51e4fb006a0":
        ("1.1.3", 0x513F2C, 0x514B2C, 0xA3DF70),
}
BEFORE = bytes.fromhex("0f 85 89 01 00 00")
AFTER = b"\x90" * 6
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
        source_hash = hashlib.sha256(original).hexdigest()
        if source_hash not in BUILDS:
            raise ValueError("unsupported TTWVR.dll hash; refusing another build")
        version, offset, rva, loader_offset = BUILDS[source_hash]
        if original[offset:offset + len(BEFORE)] != BEFORE:
            raise ValueError("unexpected renderer-detection instruction")
        patched = bytearray(original)
        patched[offset:offset + len(BEFORE)] = AFTER
        if args.winevulkan:
            if original[loader_offset:loader_offset + 32] != LOADER_BEFORE:
                raise ValueError("unexpected Vulkan loader string or padding")
            patched[loader_offset:loader_offset + 32] = LOADER_AFTER
        args.output.parent.mkdir(parents=True, exist_ok=True)
        with args.output.open("xb") as stream:
            stream.write(patched)
    except (OSError, ValueError) as exc:
        parser.error(str(exc))
    print(json.dumps({
        "status": "experimental; runtime validation required",
        "version": version, "source_sha256": source_hash,
        "patched_sha256": hashlib.sha256(patched).hexdigest(),
        "file_offset": hex(offset), "rva": hex(rva),
        "before": BEFORE.hex(), "after": AFTER.hex(),
        "changed_bytes": sum(a != b for a, b in zip(original, patched)),
        "winevulkan_lookup": args.winevulkan,
    }, indent=2))


if __name__ == "__main__":
    main()
