#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Ethanfel and contributors
# SPDX-License-Identifier: GPL-3.0-only
"""Create an EXPERIMENTAL TTWVR 1.1.3/1.1.4/1.1.5/1.1.6/1.1.7 native-Vulkan-FSR candidate.

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


# 1.1.4 sites were reviewed independently against the relocated instructions,
# startup calls, shared settings and unchanged FidelityFX structure access.
SOURCE_SHA256_114 = '7eb72860d209d9b9bdd774607220ee63323367aa9fa525f6eb1b23184f30f91e'
PATCHED_SHA256_114 = '216e74fae62df07f7b1cea9857097288f1d946b510b5d6bb26effb368efcf127'
PATCHES_114 = (('Initialize embedded Vulkan FSR', 5452443, 'e846f0aeff', 'e9c0440300'),
 ('FSR startup trampoline',
  5666656,
  'cccccccccccccccccccccccccccccccccccccccccccccccccccccc',
  'e80badffffe8000000005988813e5a4901e870ababffe925bbfcff'),
 ('VkImage',
  5655280,
  'cccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccc',
  '8b8c24dc0000008988a4000000c780a0000000564b36345f5e81c4c8000000c3'),
 ('VkImage', 5655259, '5f5e81c4c8000000', 'e910000000909090'),
 ('VkImage',
  5655744,
  'cccccccccccccccccccccccccccccccccccccccccccccccccccccccccc',
  '31c081baa0000000564b363475068b82a400000089443e04e9cb261d00'),
 ('VkImage', 7566240, 'c7443e0400000000', 'e91bd9e2ff909090'),
 ('VkPipeline',
  7555680,
  'cccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccc',
  '83c40c8b8c240c010000898f548e0000c787508e0000564b503431c0e97bffffff'),
 ('VkPipeline', 7555575, '83c40c33c0', 'e964000000'),
 ('VkPipeline',
  7555728,
  'cccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccc',
  '31c981bbd48e0000564b503475068b8bd88e000051ffb390000000e932410000'),
 ('VkPipeline', 7572442, '6a00ffb390000000', 'e9b1beffff909090'),
 ('VkPipeline',
  7555776,
  'cccccccccccccccccccccccccccccccccccccccccccccccccccccccccc',
  '31d281bb508e0000564b503475068b93548e00006a005251e9fb120000'),
 ('VkPipeline', 7560659, '6a006a0051', 'e9e8ecffff'),
 ('VkBuffer / GetResource',
  7563808,
  'cccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccc',
  '8b873c3900008981a4000000c781a0000000564b36348b8738390000e9e7fdffff'),
 ('VkBuffer / GetResource', 7563298, '8b8738390000', 'e9f901000090'),
 ('VkBuffer / GetResource',
  7563856,
  'cccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccc',
  '5031c081bc2410010000564b363475078b84241401000089460458e98a200000'),
 ('VkBuffer / GetResource', 7572211, 'c7460400000000', 'e958dfffff9090'),
 ('VkBuffer / GetResource',
  7563904,
  'cccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccc',
  '8b8b103900008b4411048987a4000000c787a0000000564b3634e9cc070000'),
 ('VkBuffer / GetResource', 7565925, '8b8b10390000', 'e916f8ffff90'))


# 1.1.5 locations, branch destinations, status fields and trampoline padding
# independently checked; no modified range intersects a PE relocation.
SOURCE_SHA256_115 = 'e14567bc1c7d183130f902f9489b8cffe6269de06eea0d79bf8d1d3098c595b1'
PATCHED_SHA256_115 = '4c6b2d9cccf0adffd3376a4a7eadffccc5f32c90537922064c35c2bf3c65dfdf'
PATCHES_115 = (('Initialize embedded Vulkan FSR', 5626923, 'e8004dacff', 'e980420300'),
 ('FSR startup trampoline',
  5840560,
  'cccccccccccccccccccccccccccccccccccccccccccccccccccccc',
  'e80badffffe800000000598881764b5401e86a0aa9ffe965bdfcff'),
 ('VkImage',
  5829184,
  'cccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccc',
  '8b8c24dc0000008988a4000000c780a0000000564b36345f5e81c4c8000000c3'),
 ('VkImage', 5829163, '5f5e81c4c8000000', 'e910000000909090'),
 ('VkImage',
  5829648,
  'cccccccccccccccccccccccccccccccccccccccccccccccccccccccccc',
  '31c081baa0000000564b363475068b82a400000089443e04e95b4b1d00'),
 ('VkImage', 7749504, 'c7443e0400000000', 'e98bb4e2ff909090'),
 ('VkPipeline',
  7738944,
  'cccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccc',
  '83c40c8b8c240c010000898f548e0000c787508e0000564b503431c0e97bffffff'),
 ('VkPipeline', 7738839, '83c40c33c0', 'e964000000'),
 ('VkPipeline',
  7738992,
  'cccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccc',
  '31c981bbd48e0000564b503475068b8bd88e000051ffb390000000e932410000'),
 ('VkPipeline', 7755706, '6a00ffb390000000', 'e9b1beffff909090'),
 ('VkPipeline',
  7739040,
  'cccccccccccccccccccccccccccccccccccccccccccccccccccccccccc',
  '31d281bb508e0000564b503475068b93548e00006a005251e9fb120000'),
 ('VkPipeline', 7743923, '6a006a0051', 'e9e8ecffff'),
 ('VkBuffer / GetResource',
  7747072,
  'cccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccc',
  '8b873c3900008981a4000000c781a0000000564b36348b8738390000e9e7fdffff'),
 ('VkBuffer / GetResource', 7746562, '8b8738390000', 'e9f901000090'),
 ('VkBuffer / GetResource',
  7747120,
  'cccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccc',
  '5031c081bc2410010000564b363475078b84241401000089460458e98a200000'),
 ('VkBuffer / GetResource', 7755475, 'c7460400000000', 'e958dfffff9090'),
 ('VkBuffer / GetResource',
  7747168,
  'cccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccc',
  '8b8b103900008b4411048987a4000000c787a0000000564b3634e9cc070000'),
 ('VkBuffer / GetResource', 7749189, '8b8b10390000', 'e916f8ffff90'))


# 1.1.6: instruction references, branch targets, padding and relocations audited.
SOURCE_SHA256_116 = '503266c4f4c2350b357c8c15021d8bde28973254745d9302b8e3cfde66489a27'
PATCHED_SHA256_116 = '676edd310b3f7f24be0da3b1ff139b98b89ab2a1d3c4b8cb53381f83825d9a6c'
PATCHES_116 = (('Initialize embedded Vulkan FSR', 5835115, 'e89126a9ff', 'e980420300'),
 ('FSR startup trampoline',
  6048752,
  'cccccccccccccccccccccccccccccccccccccccccccccccccccccc',
  'e80badffffe8000000005988816ede5501e8fbe3a5ffe965bdfcff'),
 ('VkImage',
  6037376,
  'cccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccc',
  '8b8c24dc0000008988a4000000c780a0000000564b36345f5e81c4c8000000c3'),
 ('VkImage', 6037355, '5f5e81c4c8000000', 'e910000000909090'),
 ('VkImage',
  6037840,
  'cccccccccccccccccccccccccccccccccccccccccccccccccccccccccc',
  '31c081baa0000000564b363475068b82a400000089443e04e9bb391d00'),
 ('VkImage', 7953184, 'c7443e0400000000', 'e92bc6e2ff909090'),
 ('VkPipeline',
  7942624,
  'cccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccc',
  '83c40c8b8c240c010000898f548e0000c787508e0000564b503431c0e97bffffff'),
 ('VkPipeline', 7942519, '83c40c33c0', 'e964000000'),
 ('VkPipeline',
  7942672,
  'cccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccc',
  '31c981bbd48e0000564b503475068b8bd88e000051ffb390000000e932410000'),
 ('VkPipeline', 7959386, '6a00ffb390000000', 'e9b1beffff909090'),
 ('VkPipeline',
  7942720,
  'cccccccccccccccccccccccccccccccccccccccccccccccccccccccccc',
  '31d281bb508e0000564b503475068b93548e00006a005251e9fb120000'),
 ('VkPipeline', 7947603, '6a006a0051', 'e9e8ecffff'),
 ('VkBuffer / GetResource',
  7950752,
  'cccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccc',
  '8b873c3900008981a4000000c781a0000000564b36348b8738390000e9e7fdffff'),
 ('VkBuffer / GetResource', 7950242, '8b8738390000', 'e9f901000090'),
 ('VkBuffer / GetResource',
  7950800,
  'cccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccc',
  '5031c081bc2410010000564b363475078b84241401000089460458e98a200000'),
 ('VkBuffer / GetResource', 7959155, 'c7460400000000', 'e958dfffff9090'),
 ('VkBuffer / GetResource',
  7950848,
  'cccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccc',
  '8b8b103900008b4411048987a4000000c787a0000000564b3634e9cc070000'),
 ('VkBuffer / GetResource', 7952869, '8b8b10390000', 'e916f8ffff90'))


# 1.1.7: independently mapped instructions, branches, padding and global references.
SOURCE_SHA256_117 = '119a085a5b0101f0618b8e076a421b4b89e30d72d9b6eae874f4e5af7bd3fb27'
PATCHED_SHA256_117 = '007ac7d466281a032e6b2ceb475dca361f0a7451539aba782a95540592a289bd'
PATCHES_117 = (('Initialize embedded Vulkan FSR', 5930379, 'e814b5a7ff', 'e920420300'),
 ('FSR startup trampoline',
  6143920,
  'cccccccccccccccccccccccccccccccccccccccccccccccccccccc',
  'e80badffffe800000000598881ae955601e8de72a4ffe9c5bdfcff'),
 ('VkImage',
  6132544,
  'cccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccc',
  '8b8c24dc0000008988a4000000c780a0000000564b36345f5e81c4c8000000c3'),
 ('VkImage', 6132523, '5f5e81c4c8000000', 'e910000000909090'),
 ('VkImage',
  6133008,
  'cccccccccccccccccccccccccccccccccccccccccccccccccccccccccc',
  '31c081baa0000000564b363475068b82a400000089443e04e95b3d1d00'),
 ('VkImage', 8049280, 'c7443e0400000000', 'e98bc2e2ff909090'),
 ('VkPipeline',
  8038720,
  'cccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccc',
  '83c40c8b8c240c010000898f548e0000c787508e0000564b503431c0e97bffffff'),
 ('VkPipeline', 8038615, '83c40c33c0', 'e964000000'),
 ('VkPipeline',
  8038768,
  'cccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccc',
  '31c981bbd48e0000564b503475068b8bd88e000051ffb390000000e932410000'),
 ('VkPipeline', 8055482, '6a00ffb390000000', 'e9b1beffff909090'),
 ('VkPipeline',
  8038816,
  'cccccccccccccccccccccccccccccccccccccccccccccccccccccccccc',
  '31d281bb508e0000564b503475068b93548e00006a005251e9fb120000'),
 ('VkPipeline', 8043699, '6a006a0051', 'e9e8ecffff'),
 ('VkBuffer / GetResource',
  8046848,
  'cccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccc',
  '8b873c3900008981a4000000c781a0000000564b36348b8738390000e9e7fdffff'),
 ('VkBuffer / GetResource', 8046338, '8b8738390000', 'e9f901000090'),
 ('VkBuffer / GetResource',
  8046896,
  'cccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccc',
  '5031c081bc2410010000564b363475078b84241401000089460458e98a200000'),
 ('VkBuffer / GetResource', 8055251, 'c7460400000000', 'e958dfffff9090'),
 ('VkBuffer / GetResource',
  8046944,
  'cccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccc',
  '8b8b103900008b4411048987a4000000c787a0000000564b3634e9cc070000'),
 ('VkBuffer / GetResource', 8048965, '8b8b10390000', 'e916f8ffff90'))


def build(original):
    digest = hashlib.sha256(original).hexdigest()
    if digest == SOURCE_SHA256:
        renderer, loader, patches, expected = 0x513f2c, 0xa3df70, PATCHES, PATCHED_SHA256
    elif digest == SOURCE_SHA256_114:
        renderer, loader, patches, expected = 0x52618c, 0xa51fd0, PATCHES_114, PATCHED_SHA256_114
    elif digest == SOURCE_SHA256_115:
        renderer, loader, patches, expected = 0x550b4c, 0xa83870, PATCHES_115, PATCHED_SHA256_115
    elif digest == SOURCE_SHA256_116:
        renderer, loader, patches, expected = 0x58388c, 0xabaf70, PATCHES_116, PATCHED_SHA256_116
    elif digest == SOURCE_SHA256_117:
        renderer, loader, patches, expected = 0x59ae8c, 0xad6a00, PATCHES_117, PATCHED_SHA256_117
    else:
        raise ValueError("unsupported TTWVR.dll; need the exact original 1.1.3, 1.1.4, 1.1.5, 1.1.6 or 1.1.7 archive build")
    result = bytearray(original)
    changes = [("Bundled DXVK", renderer, BEFORE, AFTER),
               ("Wine Vulkan lookup", loader, LOADER_BEFORE, LOADER_AFTER)]
    changes += [(name, rva - 0xc00, bytes.fromhex(old), bytes.fromhex(new))
                for name, rva, old, new in patches]
    occupied = set()
    for name, offset, old, new in changes:
        if len(old) != len(new) or original[offset:offset + len(old)] != old:
            raise ValueError(f"unexpected bytes or patch size: {name} at {offset:#x}")
        positions = set(range(offset, offset + len(old)))
        if occupied & positions:
            raise ValueError("overlapping patch regions")
        occupied |= positions
        result[offset:offset + len(old)] = new
    if hashlib.sha256(result).hexdigest() != expected:
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
                      "source_sha256": hashlib.sha256(original).hexdigest(),
                      "patched_sha256": hashlib.sha256(patched).hexdigest(),
                      "changed_bytes": sum(a != b for a, b in zip(original, patched)),
                      "native_fsr": "3.1.4 Vulkan, per-eye contexts",
                      "shared_fence_fix_required": True}, indent=2))


if __name__ == "__main__":
    main()
