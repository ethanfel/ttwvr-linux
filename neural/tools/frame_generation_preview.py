#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Inspect one captured FG stereo pair and compare its real/color round trip."""

from pathlib import Path
import argparse
import json
import numpy as np
from PIL import Image, ImageDraw
from prepare_replay import read_dds

ap = argparse.ArgumentParser(description=__doc__)
ap.add_argument("captures", type=Path)
ap.add_argument("frame", type=int)
ap.add_argument("output", type=Path)
args = ap.parse_args()
run = args.captures
out = args.output
out.mkdir(exist_ok=True, parents=True)
rows = []
matches = list(run.glob(f"capture-*-frame{args.frame}-layer0-eye0.json"))
assert len(matches) == 1, "Expected exactly one captured source frame"
for f in matches:
    a = json.loads(f.read_text())
    frame = a["frame"]
    stats = []
    for kind in ("original", "fg-real", "fg-generated"):
        imgs = []
        for eye in (0, 1):
            meta = f.with_name(f.name.replace("-eye0.json", f"-eye{eye}.json"))
            m = json.loads(meta.read_text())
            assert m["frame"] == frame and m["eye"] == eye
            assert m["fg_interpolated"] and m["fg_format"] == 29
            name = meta.stem + ("" if kind == "original" else "-" + kind) + ".dds"
            p = run / name
            _, data = read_dds(p)
            rgb = data[:, :, :3] if kind != "original" else data[:, :, [2, 1, 0]]
            imgs.append(Image.fromarray(rgb))
            if kind == "original":
                stats.append(
                    {"eye": eye, "original": rgb.astype(np.int16), "metadata": m}
                )
            else:
                s = next(x for x in stats if x["eye"] == eye)
                delta = np.abs(rgb.astype(np.int16) - s["original"])
                if kind == "fg-real":
                    assert not delta.any(), (
                        "Real frame RGB changed during format conversion"
                    )
                s[kind] = {
                    "max_rgb_change": int(delta.max()),
                    "mean_rgb_change": float(delta.mean()),
                    "changed_pixels": int(np.any(delta, axis=2).sum()),
                }
        w, h = imgs[0].size
        panel = Image.new("RGB", (w * 2, h + 70))
        d = ImageDraw.Draw(panel)
        for eye, im in enumerate(imgs):
            panel.paste(im, (eye * w, 70))
            d.text(
                (eye * w + 20, 15),
                f"Frame {frame} | eye {eye} | {kind} | TTWVR capture",
                fill="white",
            )
        panel.resize((2048, round(panel.height * 2048 / panel.width))).save(
            out / f"frame{frame}-{kind}-stereo.png"
        )
    for s in stats:
        s.pop("original")
    rows.extend(stats)
(out / "metrics.json").write_text(json.dumps(rows, indent=2) + "\n")
print(
    json.dumps(
        [{k: v for k, v in s.items() if k != "metadata"} for s in rows], indent=2
    )
)
