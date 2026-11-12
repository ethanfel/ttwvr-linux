#!/usr/bin/env python3
"""Create an EXPERIMENTAL TTWVR 1.1.3 native-Vulkan-FSR candidate.

Reads only the exact original DLL, writes a NEW file, and never installs it.
Includes bundled-DXVK activation, Wine Vulkan lookup, missing FSR startup,
and 64-bit Vulkan handle repairs across the 32-bit FidelityFX abstraction.
See docs/native-vulkan-fsr.md for limits and the separate fence requirement.
No game/mod binaries or NVIDIA/AMD runtime DLLs are distributed here.
"""
import argparse
import hashlib
import json
from pathlib import Path

from experimental_ttwvr_renderer import BEFORE, AFTER, LOADER_BEFORE, LOADER_AFTER

SOURCE_SHA256 = "4c45d6e5adb5da76dd3d08bf0639d360761d3dc50d891ea0bd19b51e4fb006a0"
PATCHED_SHA256 = "f8a8eb95070eaaee0041fd8901f69e1150abea3cf8115ac7206c24a153c2dedf"
# Exact-build .text mapping: file offset = RVA - 0xc00.
# Relative calls/jumps and call/pop addressing retain ASLR; no relocations added.
# VK64 marker + high DWORD occupy FfxResource.name[60:64].
# VKP4 marker + high DWORD occupy FfxPipelineState.name[60:64].
# These release-build name tails are unused; the leading strings remain intact.
PATCHES = (
    ('Initialize embedded Vulkan FSR', 0x52103b,
     'e82412b0ff',
     'e9c0440300'),
    ('FSR startup trampoline', 0x555500,
     'cccccccccccccccccccccccccccccccccccccccccccccccccccccc',
     'e80badffffe80000000059888126334401e84ecdacffe925bbfcff'),
    ('VkImage', 0x552890,
     'cccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccc',
     '8b8c24dc0000008988a4000000c780a0000000564b36345f5e81c4c8000000c3'),
    ('VkImage', 0x55287b,
     '5f5e81c4c8000000',
     'e910000000909090'),
    ('VkImage', 0x552a60,
     'cccccccccccccccccccccccccccccccccccccccccccccccccccccccccc',
     '31c081baa0000000564b363475068b82a400000089443e04e92b221d00'),
    ('VkImage', 0x724ca0,
     'c7443e0400000000',
     'e9bbdde2ff909090'),
    ('VkPipeline', 0x722360,
     'cccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccc',
     '83c40c8b8c240c010000898f548e0000c787508e0000564b503431c0e97bffffff'),
    ('VkPipeline', 0x7222f7,
     '83c40c33c0',
     'e964000000'),
    ('VkPipeline', 0x722390,
     'cccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccc',
     '31c981bbd48e0000564b503475068b8bd88e000051ffb390000000e932410000'),
    ('VkPipeline', 0x7264da,
     '6a00ffb390000000',
     'e9b1beffff909090'),
    ('VkPipeline', 0x7223c0,
     'cccccccccccccccccccccccccccccccccccccccccccccccccccccccccc',
     '31d281bb508e0000564b503475068b93548e00006a005251e9fb120000'),
    ('VkPipeline', 0x7236d3,
     '6a006a0051',
     'e9e8ecffff'),
    ('VkBuffer / GetResource', 0x724320,
     'cccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccc',
     '8b873c3900008981a4000000c781a0000000564b36348b8738390000e9e7fdffff'),
    ('VkBuffer / GetResource', 0x724122,
     '8b8738390000',
     'e9f901000090'),
    ('VkBuffer / GetResource', 0x724350,
     'cccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccc',
     '5031c081bc2410010000564b363475078b84241401000089460458e98a200000'),
    ('VkBuffer / GetResource', 0x7263f3,
     'c7460400000000',
     'e958dfffff9090'),
    ('VkBuffer / GetResource', 0x724380,
     'cccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccc',
     '8b8b103900008b4411048987a4000000c787a0000000564b3634e9cc070000'),
    ('VkBuffer / GetResource', 0x724b65,
     '8b8b10390000',
     'e916f8ffff90'),

)


def build(original):
    if hashlib.sha256(original).hexdigest() != SOURCE_SHA256:
        raise ValueError("unsupported TTWVR.dll; need the exact original 1.1.3 archive build")
    result = bytearray(original)
    changes = [("Bundled DXVK", 0x513f2c, BEFORE, AFTER),
               ("Wine Vulkan lookup", 0xa3df70, LOADER_BEFORE, LOADER_AFTER)]
    changes += [(name, rva - 0xc00, bytes.fromhex(old), bytes.fromhex(new))
                for name, rva, old, new in PATCHES]
    occupied = set()
    for name, offset, old, new in changes:
        if len(old) != len(new) or original[offset:offset + len(old)] != old:
            raise ValueError(f"unexpected bytes or patch size: {name} at {offset:#x}")
        positions = set(range(offset, offset + len(old)))
        if occupied & positions:
            raise ValueError("overlapping patch regions")
        occupied |= positions
        result[offset:offset + len(old)] = new
    if hashlib.sha256(result).hexdigest() != PATCHED_SHA256:
        raise ValueError("unexpected resulting DLL hash")
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    try:
        original = args.source.read_bytes()
        patched = build(original)
        args.output.parent.mkdir(parents=True, exist_ok=True)
        with args.output.open("xb") as stream:
            stream.write(patched)
    except (OSError, ValueError) as exc:
        parser.error(str(exc))
    print(json.dumps({"status": "experimental; isolated launch only",
                      "source_sha256": SOURCE_SHA256,
                      "patched_sha256": PATCHED_SHA256,
                      "changed_bytes": sum(a != b for a, b in zip(original, patched)),
                      "native_fsr": "3.1.4 Vulkan, per-eye contexts",
                      "shared_fence_fix_required": True}, indent=2))


if __name__ == "__main__":
    main()
