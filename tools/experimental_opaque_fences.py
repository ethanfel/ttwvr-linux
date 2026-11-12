#!/usr/bin/env python3
"""Create an EXPERIMENTAL private DXVK D3D11 fence candidate for TTWVR.

Changes shared D3D11 fences from D3D11_FENCE_BIT (8) to OPAQUE_WIN32_BIT (2),
retaining native Vulkan timeline waits/signals and stock Wine. Only monotonically
increasing fence values are supported. This is NOT a general D3D fence fix.
Input must be this project's exact DXVK pool candidate. Output is a NEW file;
never replace an installed Proton DLL. See docs/native-vulkan-fsr.md.
"""
import argparse
import hashlib
import json
from pathlib import Path

SOURCE_SHA256 = "857f0d0444914dd157b19769a4f4a55f24f53f68eeb249cc9b085b906684e889"
PATCHED_SHA256 = "09e23a2003e03d616858e0c2ded02456b4908b3e17fb66f760ff507ec38e753c"
OFFSET = 0x32973  # RVA equals file offset in this DXVK image.
BEFORE = bytes.fromhex("c785f8feffff08000000")
AFTER = bytes.fromhex("c785f8feffff02000000")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    try:
        original = args.source.read_bytes()
        if hashlib.sha256(original).hexdigest() != SOURCE_SHA256:
            raise ValueError("unsupported d3d11.dll; need the exact DXVK pool candidate")
        if original[OFFSET:OFFSET + len(BEFORE)] != BEFORE:
            raise ValueError("unexpected D3D11Fence constructor instruction")
        patched = bytearray(original)
        patched[OFFSET:OFFSET + len(BEFORE)] = AFTER
        if hashlib.sha256(patched).hexdigest() != PATCHED_SHA256:
            raise ValueError("unexpected output hash")
        args.output.parent.mkdir(parents=True, exist_ok=True)
        with args.output.open("xb") as stream:
            stream.write(patched)
    except (OSError, ValueError) as exc:
        parser.error(str(exc))
    print(json.dumps({"status": "experimental; TTWVR monotonic shared fences only",
                      "source_sha256": SOURCE_SHA256,
                      "patched_sha256": PATCHED_SHA256,
                      "changed_bytes": 1, "offset": hex(OFFSET),
                      "wine_modified": False}, indent=2))


if __name__ == "__main__":
    main()
