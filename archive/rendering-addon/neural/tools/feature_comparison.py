#!/usr/bin/env python3
"""Compare captured pre/post-feature HDR from the exact same game frame.

The preview uses a labelled HDR display proxy, not the game's tone mapper.
"""
import argparse
import json
from pathlib import Path
import numpy as np
from PIL import Image, ImageDraw
from prepare_replay import read_dds
from scipy.ndimage import map_coordinates

ap = argparse.ArgumentParser(description=__doc__)
ap.add_argument('captures', type=Path)
ap.add_argument('frame', type=int)
ap.add_argument('output', type=Path)
args = ap.parse_args()
args.output.mkdir(parents=True, exist_ok=True)
preview = Image.new('RGB', (1600, 1240))
draw = ImageDraw.Draw(preview)
metrics = []
for eye in range(2):
    arrays = [read_dds(args.captures / f'feature-frame-{args.frame}-eye-{eye}-{kind}-hdr.dds')[1] for kind in ('input', 'output')]
    assert all(np.isfinite(a).all() for a in arrays)
    reference = arrays[0].astype('f4')
    upscaled = arrays[0].shape != arrays[1].shape
    if upscaled:
        ih, iw = reference.shape[:2]; oh, ow = arrays[1].shape[:2]
        assert iw < ow and ih < oh
        y, x = np.mgrid[:oh, :ow].astype('f4')
        coords = [(y + .5) * ih / oh - .5, (x + .5) * iw / ow - .5]
        reference = np.stack([map_coordinates(reference[:, :, k], coords, order=1, mode='nearest') for k in range(4)], -1)
    alpha_error = float(np.abs(reference[:, :, 3] - arrays[1][:, :, 3]).max())
    alpha_equal = np.array_equal(reference[:, :, 3], arrays[1][:, :, 3])
    # Hardware bilinear weights have finite fractional precision; allow the
    # two-axis 8-bit filtering bound plus half-float output rounding.
    alpha_bound = 2 / 256 + .001 if upscaled else 0
    assert alpha_error <= alpha_bound, 'Processing changed HDR alpha'
    delta = np.abs(reference[:, :, :3] - arrays[1][:, :, :3].astype('f4'))
    metrics.append({'eye': eye, 'input_size': list(arrays[0].shape[:2][::-1]), 'output_size': list(arrays[1].shape[:2][::-1]), 'comparison_reference': 'bilinear input' if upscaled else 'input', 'alpha_max_error': alpha_error, 'alpha_error_bound': alpha_bound, 'alpha_unchanged': alpha_equal, 'rgb_mean_absolute_change': float(delta.mean()),
                    'changed_pixel_fraction': float(np.any(delta > .0001, axis=-1).mean()),
                    'rgb_input_range': [float(arrays[0][:, :, :3].min()), float(arrays[0][:, :, :3].max())],
                    'rgb_output_range': [float(arrays[1][:, :, :3].min()), float(arrays[1][:, :, :3].max())]})
    for col, hdr in enumerate(arrays):
        rgb = np.maximum(hdr[:, :, :3].astype('f4'), 0)
        rgb /= 1 + np.max(rgb, axis=-1, keepdims=True)
        rgb = np.where(rgb <= .0031308, rgb * 12.92, 1.055 * np.maximum(rgb, 0) ** (1/2.4) - .055)
        im = Image.fromarray(np.clip(rgb * 255, 0, 255).astype('u1'))
        im.save(args.output / f'frame{args.frame}-eye{eye}-{("input", "output")[col]}-hdr-proxy.png')
        preview.paste(im.resize((800, 600)), (col * 800, eye * 620 + 20))
        draw.text((col * 800 + 5, eye * 620 + 3), f'Eye {eye} | {("Before feature (scene HDR)", "After feature")[col]} | same frame {args.frame} | HDR proxy', fill='white')
preview.save(args.output / 'same-frame-feature-comparison.png')
(args.output / 'comparison.json').write_text(json.dumps({'frame': args.frame, 'same_input': True, 'preview': 'HDR display proxy; not game tone mapping', 'eyes': metrics}, indent=2) + '\n')
print(json.dumps(metrics, indent=2))
