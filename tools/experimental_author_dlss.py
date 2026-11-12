#!/usr/bin/env python3
"""Build an isolated TTWVR 1.1.3 author-DLSS compatibility candidate.

Accepts only the exact original plugin. Includes bundled-renderer activation,
Wine Vulkan lookup and full-size shared output instead of unavailable direct
headset-image output. --native-fsr adds the independently verified Vulkan FSR
repairs. Requires the launch-scoped opaque-fence DXVK patch as well.
Never installs or overwrites a DLL; no vendor binaries are distributed.
"""
import argparse
import hashlib
import json
from pathlib import Path

from experimental_ttwvr_renderer import BEFORE, AFTER, LOADER_BEFORE, LOADER_AFTER
from experimental_native_fsr import SOURCE_SHA256, build as build_native_fsr

# This indirect capability query is a two-byte instruction with no relocation.
# Preserve the later absolute-address store and its existing relocation entry.
CAPABILITY_RVA = 0x520C82
CAPABILITY_OFFSET = 0x520082
CAPABILITY_BEFORE = bytes.fromhex('ffd0')  # call eax: runtime capability getter
CAPABILITY_AFTER = bytes.fromhex('31c0')   # xor eax,eax: use shared output
EXPECTED_COMBINED_SHA256 = 'ee9e2e1b1d2ae120244ca53f033327ee8bad83fb43a7ffa1b79753c17e50270f'
EXPECTED_DLSS_SHA256 = '8384261c4b5fafd90a7fe860ccb7e1e24953a653c2fdbeafb8bd46ab34485f64'


def build(original, native_fsr=False):
    if hashlib.sha256(original).hexdigest() != SOURCE_SHA256:
        raise ValueError('need the exact original TTWVR 1.1.3 plugin')
    if native_fsr:
        result = build_native_fsr(original)
    else:
        result = bytearray(original)
        for offset, old, new in ((0x513f2c, BEFORE, AFTER),
                                 (0xa3df70, LOADER_BEFORE, LOADER_AFTER)):
            if result[offset:offset+len(old)] != old:
                raise ValueError('unexpected renderer/loader bytes')
            result[offset:offset+len(old)] = new
    if result[CAPABILITY_OFFSET:CAPABILITY_OFFSET+2] != CAPABILITY_BEFORE:
        raise ValueError('unexpected direct-output capability instruction')
    result[CAPABILITY_OFFSET:CAPABILITY_OFFSET+2] = CAPABILITY_AFTER
    expected = EXPECTED_COMBINED_SHA256 if native_fsr else EXPECTED_DLSS_SHA256
    if hashlib.sha256(result).hexdigest() != expected:
        raise ValueError('unexpected output hash')
    return result


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('source', type=Path)
    ap.add_argument('output', type=Path)
    ap.add_argument('--native-fsr', action='store_true')
    args = ap.parse_args()
    try:
        original = args.source.read_bytes()
        result = build(original, args.native_fsr)
        args.output.parent.mkdir(parents=True, exist_ok=True)
        with args.output.open('xb') as stream:
            stream.write(result)
    except (OSError, ValueError) as exc:
        ap.error(str(exc))
    print(json.dumps(dict(status='experimental; isolated launch only',
        source_sha256=SOURCE_SHA256,
        patched_sha256=hashlib.sha256(result).hexdigest(),
        changed_bytes=sum(a != b for a, b in zip(original, result)),
        native_fsr=args.native_fsr, shared_output=True,
        opaque_fence_fix_required=True), indent=2))


if __name__ == '__main__':
    main()
