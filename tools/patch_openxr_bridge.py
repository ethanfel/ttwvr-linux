#!/usr/bin/env python3
"""Create a private, hash-guarded workaround for Proton's projection-first layers."""
import argparse
import hashlib
import json
from pathlib import Path


SOURCE_SHA256 = "7ee56ba303d02f5e3715b20bdae288c1663e52bfe22262912fba75c8dfac25a5"
PATCHED_SHA256 = "03127b4519cfa35e544b72c0356b7850ce257a842a1107812a756a6dbf2be0b5"
# experimental-11.0-20261105c-x86_64, x86_64-unix/wineopenxr.so.
# In both win32/win64 conversion functions, a projection-first array currently
# reserves 48 bytes for EACH layer. Quads need 112 and the largest supported
# layer in this build needs 120. Reserve 120 for this array shape instead.
# This does not fix every possible heterogeneous array or other Proton builds.
PATCHES = (0x4E4D4, 0x52945)
BEFORE = bytes.fromhex("c7 45 cc 30 00 00 00")
AFTER = bytes.fromhex("c7 45 cc 78 00 00 00")


def patch_bytes(original):
    if hashlib.sha256(original).hexdigest() != SOURCE_SHA256:
        raise ValueError("unsupported wineopenxr.so hash; refusing to patch another build")
    result = bytearray(original)
    for offset in PATCHES:
        if original[offset:offset + len(BEFORE)] != BEFORE:
            raise ValueError(f"unexpected instruction at {offset:#x}")
        result[offset:offset + len(AFTER)] = AFTER
    if hashlib.sha256(result).hexdigest() != PATCHED_SHA256:
        raise ValueError("unexpected patched output hash")
    return bytes(result)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    try:
        data = patch_bytes(args.source.read_bytes())
        args.output.parent.mkdir(parents=True, exist_ok=True)
        # Never overwrite the installed bridge, a previous patch, or a symlink.
        with args.output.open("xb") as stream:
            stream.write(data)
        args.output.chmod(0o755)
    except (OSError, ValueError) as exc:
        parser.error(str(exc))
    print(json.dumps({"source_sha256": SOURCE_SHA256,
                      "patched_sha256": hashlib.sha256(data).hexdigest(),
                      "changed_bytes": 2,
                      "instruction_offsets": [hex(x) for x in PATCHES]}, indent=2))


if __name__ == "__main__":
    main()
