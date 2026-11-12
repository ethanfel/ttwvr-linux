#!/usr/bin/env python3
"""Export in-game OpenXR NR captures only when both eye metadata confirm output."""
from pathlib import Path
import argparse
import json
from PIL import Image, ImageDraw
from prepare_replay import read_dds

ap = argparse.ArgumentParser(description=__doc__)
ap.add_argument('left', type=Path)
ap.add_argument('output', type=Path)
args = ap.parse_args()
left = args.left
right = left.with_name(left.name.replace('-eye0.dds', '-eye1.dds'))
metadata = [json.loads(p.with_suffix('.json').read_text()) for p in (left, right)]
assert [m['eye'] for m in metadata] == [0, 1]
assert metadata[0]['frame'] == metadata[1]['frame']
assert all(m['neural_rendering'] or m['dlaa'] or m['mode'] == 'capture_only' for m in metadata)
assert metadata[0]['mode'] == metadata[1]['mode']
feature = 'nr' if metadata[0]['mode'] == 'spatial_nr_reset_every_frame' else metadata[0]['mode']
assert feature in ('nr', 'dlaa', 'temporal_nr', 'temporal_nr_dlaa', 'capture_only')
args.output.mkdir(parents=True, exist_ok=True)
frame = metadata[0]['frame']
images = []
for eye, p in enumerate((left, right)):
    _, a = read_dds(p)
    im = Image.fromarray(a[:, :, [2, 1, 0]])
    im.save(args.output / f'{feature}-frame{frame}-eye{eye}.png')
    images.append(im)
w, h = images[0].size
stereo = Image.new('RGB', (w * 2, h + 48))
for eye, im in enumerate(images):
    stereo.paste(im, (eye * w, 48))
draw = ImageDraw.Draw(stereo)
draw.text((20, 15), f'In-game {feature} | left eye | frame {frame} | {w}x{h} | prototype', fill='white')
draw.text((w + 20, 15), f'In-game {feature} | right eye | frame {frame} | native resolution', fill='white')
stereo.save(args.output / f'{feature}-frame{frame}-stereo-full.png')
stereo.resize((2048, round(stereo.height * 2048 / stereo.width))).save(args.output / f'{feature}-frame{frame}-stereo-preview.png')
print(args.output / f'{feature}-frame{frame}-stereo-preview.png')
