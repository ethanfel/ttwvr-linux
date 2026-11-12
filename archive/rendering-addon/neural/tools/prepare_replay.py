#!/usr/bin/env python3
"""Prepare a spatial NR diagnostic from paired TTWVR scene captures.

Motion is intentionally zero and replay MUST reset history on every evaluation.
This does not validate motion, temporal reconstruction, live VR or DLAA.
"""
from pathlib import Path
import argparse
import json
import re
import struct
import numpy as np
from PIL import Image, ImageDraw


def read_dds(path):
    data = path.read_bytes()
    if data[:4] != b'DDS ' or len(data) < 148:
        raise ValueError(f'Not a DX10 DDS: {path}')
    height, width = struct.unpack_from('<II', data, 12)
    fmt = struct.unpack_from('<I', data, 128)[0]
    dtype, channels = {10: ('<f2', 4), 42: ('<u4', 1), 41: ('<f4', 1),
                       34: ('<f2', 2), 54: ('<f2', 1), 28: ('u1', 4), 29: ('u1', 4), 88: ('u1', 4), 91: ('u1', 4)}[fmt]
    pixels = np.frombuffer(data, dtype, offset=148).reshape(height, width, channels)
    return data[:148], pixels


def write_dds(path, header, array, fmt):
    h = bytearray(header)
    height, width, channels = array.shape
    struct.pack_into('<III', h, 12, height, width, width * channels * array.dtype.itemsize)
    struct.pack_into('<I', h, 128, fmt)
    path.write_bytes(h + array.tobytes())


def windows_path(path):
    return 'Z:' + str(path.resolve()).replace('/', '\\')


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('captures', type=Path)
    ap.add_argument('frame', type=int)
    ap.add_argument('output', type=Path)
    args = ap.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    lines, metrics, previews = [], [], []
    for eye in range(2):
        prefix = args.captures / f'scene-frame-{args.frame}-pass-{eye}'
        hdr_path = prefix.with_name(prefix.name + '-hdr.dds')
        header, hdr = read_dds(hdr_path)
        _, raw = read_dds(prefix.with_name(prefix.name + '-depth-raw.dds'))
        _, bloom = read_dds(prefix.with_name(prefix.name + '-bloom.dds'))
        _, lum = read_dds(prefix.with_name(prefix.name + '-luminance-candidate.dds'))
        text = prefix.with_name(prefix.name + '-constants.json').read_text()
        # The initial prototype wrote unused nonfinite register values as nan.
        # Preserve that evidence; only normalize it while reading old captures.
        constants = json.loads(re.sub(r'(?<![\w])nan(?![\w])', 'null', text))
        c1 = constants['pixel_constants'][1][0]
        alpha = bloom[:, :, 3].astype('f4')
        if not np.isfinite(hdr).all() or raw.max() > 0xffffff:
            raise ValueError('Nonfinite HDR or depth outside the verified D24 layout')
        if np.ptp(alpha) != 0 or not c1 or c1 <= 0:
            raise ValueError('Exposure is not a verified uniform positive scalar')
        exposure = c1 / max(float(alpha.flat[0]), c1)
        paths = [args.output / f'eye{eye}-{kind}.dds' for kind in ('depth', 'motion-zero', 'exposure')]
        depth = raw.astype('<f4') / 16777215.0
        write_dds(paths[0], header, depth, 41)
        write_dds(paths[1], header, np.zeros((*hdr.shape[:2], 2), dtype='<f2'), 34)
        write_dds(paths[2], header, np.full((1, 1, 1), exposure, dtype='<f2'), 54)
        lines += [windows_path(hdr_path), *(windows_path(p) for p in paths)]
        _, ldr = read_dds(prefix.with_name(prefix.name + '-ldr.dds'))
        errors = []
        for candidate in range(2):
            files = list(args.captures.glob(f'capture-*-frame{args.frame}-layer0-eye{candidate}.dds'))
            if len(files) != 1:
                raise ValueError('Need one unambiguous matching OpenXR capture')
            _, xr = read_dds(files[0])
            errors.append(float(np.abs(ldr[:, :, :3].astype('f4') - xr[:, :, :3]).mean()))
        if errors[eye] * 3 >= errors[1-eye]:
            raise ValueError('Eye correspondence is ambiguous')
        metrics.append(dict(pass_index=eye, matching_eye=eye, mean_pixel_error_by_eye=errors,
                            c1_x=c1, bloom_alpha=float(alpha.flat[0]),
                            adapted_luminance_rgb_sum=float(lum[0, 0, :3].astype('f4').sum()),
                            exposure=exposure, depth_range=[float(depth.min()), float(depth.max())]))
        color = Image.fromarray(ldr[:, :, [2, 1, 0]]).resize((800, 600))
        # Diagnostic reciprocal depth contrast, not an asserted world-distance unit.
        contrast = 1 / np.maximum(1 - depth[:, :, 0], 1e-6)
        contrast = np.clip(np.log2(contrast) / 10, 0, 1)
        depth_image = Image.fromarray((contrast * 255).astype('u1')).convert('RGB').resize((800, 600))
        previews.append((color, depth_image))
    (args.output / 'spatial-replay.txt').write_text('\n'.join(lines) + '\n')
    (args.output / 'evidence.json').write_text(json.dumps(dict(frame=args.frame, spatial_only=True,
        motion_verified=False, reset_every_evaluation=True, eyes=metrics), indent=2) + '\n')
    preview = Image.new('RGB', (1600, 1240))
    for eye, (color, depth) in enumerate(previews):
        preview.paste(color, (eye * 800, 20))
        preview.paste(depth, (eye * 800, 640))
    draw = ImageDraw.Draw(preview)
    draw.text((10, 3), 'Left: tone-mapped scene', fill='white')
    draw.text((810, 3), 'Right: tone-mapped scene', fill='white')
    draw.text((10, 623), 'Matching depth (contrast enhanced, not linear distance)', fill='white')
    preview.save(args.output / 'scene-and-depth-stereo.png')
    print(json.dumps(metrics, indent=2))


if __name__ == '__main__':
    main()
