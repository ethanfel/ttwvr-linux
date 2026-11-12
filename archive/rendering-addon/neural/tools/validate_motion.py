#!/usr/bin/env python3
"""Check captured TTWVR camera/object motion against consecutive rendered images.

Readback and previews are diagnostic only. This does not validate headset comfort.
"""
import argparse
import json
from pathlib import Path
import numpy as np
from PIL import Image, ImageDraw
from scipy.ndimage import map_coordinates
from prepare_replay import read_dds


def validate(captures, frame, output):
    output.mkdir(parents=True, exist_ok=True)
    report = {'frame': frame, 'previous_frame': frame - 1, 'eyes': []}
    preview = Image.new('RGB', (1600, 1860))
    draw = ImageDraw.Draw(preview)
    for eye in range(2):
        camera = json.loads((captures / f'camera-frame-{frame}-eye-{eye}.json').read_text())
        assert camera['frame'] == frame and camera['eye'] == eye
        assert camera['previous_frame'] == frame - 1 and camera['verified_draws'] > 0
        def scene(f, kind):
            return read_dds(captures / f'scene-frame-{f}-pass-{eye}-{kind}.dds')[1]
        raw = scene(frame, 'depth-raw')
        assert raw.max() <= 0xffffff
        depth = raw[:, :, 0].astype('f8') / 16777215.0
        motion = read_dds(captures / f'motion-frame-{frame}-eye-{eye}.dds')[1].astype('f4')
        assert np.isfinite(motion).all()
        current = scene(frame, 'ldr')[:, :, :3].astype('f4')
        previous = scene(frame - 1, 'ldr')[:, :, :3].astype('f4')
        h, w = depth.shape
        image_space = 'ldr'
        if current.shape[:2] != (h, w):
            # SR's tone-mapped output is larger and already unjittered. Validate
            # low-grid motion against the matching pre-feature HDR instead.
            image_space = 'scene_hdr_proxy'
            def proxy(f):
                hdr = np.maximum(read_dds(captures / f'feature-frame-{f}-eye-{eye}-input-hdr.dds')[1][:, :, :3].astype('f4'), 0)
                assert hdr.shape[:2] == (h, w) and np.isfinite(hdr).all()
                return hdr / (1 + hdr.max(axis=-1, keepdims=True)) * 255
            current, previous = proxy(frame), proxy(frame - 1)
        # D3D9 pixel centres are integer coordinates. Its clip-to-window map
        # is x=(ndc.x+1)*w/2, y=(1-ndc.y)*h/2.
        y, x = np.mgrid[:h:2, :w:2]
        previous_camera = json.loads((captures / f'camera-frame-{frame-1}-eye-{eye}.json').read_text())
        jitter = np.array(camera.get('jitter', [0, 0]))
        previous_jitter = np.array(previous_camera.get('jitter', [0, 0]))
        matrix = lambda key: np.array(camera[key], dtype='f8').reshape(4, 4)
        transform = np.linalg.inv(matrix('view') @ matrix('projection')) @ matrix('previous_view') @ matrix('previous_projection')
        clip = np.stack(((x + jitter[0]) * 2 / w - 1, 1 - (y + jitter[1]) * 2 / h, depth[::2, ::2], np.ones_like(x)), -1)
        projected = clip @ transform
        px = (projected[:, :, 0] / projected[:, :, 3] + 1) * w / 2 - previous_jitter[0]
        py = (1 - projected[:, :, 1] / projected[:, :, 3]) * h / 2 - previous_jitter[1]
        vectors = np.stack((px - x, py - y), -1)
        native = motion[::2, ::2]
        objects = native[:, :, 2] > .5
        vectors[objects] = native[objects, :2] + jitter - previous_jitter
        cur = current[::2, ::2]
        valid = (x > 100) & (x < w - 100) & (y > 100) & (y < h - 100)
        images = {'identity': previous[::2, ::2]}
        for name, sign in [('reprojected', 1), ('reversed', -1)]:
            coords = [y + sign * vectors[:, :, 1], x + sign * vectors[:, :, 0]]
            images[name] = np.stack([map_coordinates(previous[:, :, k], coords, order=1, mode='nearest') for k in range(3)], -1)
        metrics = {}
        for region, mask in [('all', valid), ('background', valid & ~objects), ('objects', valid & objects)]:
            metrics[region] = {name: float(np.abs(cur - image).mean(axis=-1)[mask].mean()) for name, image in images.items()} if mask.any() else None
        report['eyes'].append({'eye': eye, 'image_space': image_space, 'width': w, 'height': h, 'verified_draws': camera['verified_draws'], 'relative_wvp_error': camera['relative_error'],
                               'motion_valid_fraction': float(objects.mean()), 'pixel_mae': metrics,
                               'motion_percentiles': np.percentile(vectors, [1, 50, 99], axis=(0, 1)).tolist()})
        for row, (name, image) in enumerate([('Current', cur), ('Previous warped to current', images['reprojected']), ('Absolute difference x4', np.abs(cur - images['reprojected']) * 4)]):
            im = Image.fromarray(np.clip(image[:, :, ::-1], 0, 255).astype('u1')).resize((800, 600))
            preview.paste(im, (eye * 800, row * 620 + 20))
            draw.text((eye * 800 + 5, row * 620 + 3), f'Eye {eye} | {name} | frame {frame}', fill='white')
    (output / 'motion-validation.json').write_text(json.dumps(report, indent=2) + '\n')
    preview.save(output / 'motion-reprojection-stereo.png')
    print(json.dumps(report, indent=2))


if __name__ == '__main__':
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('captures', type=Path)
    ap.add_argument('frame', type=int)
    ap.add_argument('output', type=Path)
    args = ap.parse_args()
    validate(args.captures, args.frame, args.output)
