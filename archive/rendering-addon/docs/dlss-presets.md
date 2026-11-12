# DLSS Quality, Balanced and Performance — 12 November 2026

The native **VR Settings → DLSS SR** row now cycles **Off / Quality / Balanced /
Performance**. Presets switch live; changing MSAA still requires a restart.
Existing settings are preserved. An INI without `mode` retains Quality behavior.

| Preset | Scene input per eye | Final output per eye | Jitter phases |
|---|---|---|---|
| Quality | 1963×1472 | 2944×2208 | 18 |
| Balanced | 1708×1281 | 2944×2208 | 24 |
| Performance | 1472×1104 | 2944×2208 | 32 |

These dimensions were queried from the installed NGX runtime and checked again
when creating each feature. The helper selects the corresponding NGX quality
value and preset hint; all three currently use model preset K. This is actual
lower-resolution scene rendering with DLSS reconstruction. Final tone mapping,
HUD and headset output retain their original size. SR remains exclusive with
NR/DLAA. Frame generation is independent and optional.

## Matched 90 Hz measurements

RTX 5090, Proton Experimental 11, isolated Monado simulated headset/null
compositor; the same saved corridor and frozen pose used in the earlier tests.
FG, NR and DLAA are off. Opaque GPU handoff is on. Five-second settling followed
by approximately 30 seconds per mode; no captures, image processing or builds
ran during the timing windows. The native baseline was repeated at the end.

| Mode | Application FPS | Mean whole-GPU use | Mean GPU power |
|---|---:|---:|---:|
| Native / effects off | 38.43 | 35.1% | 91.1 W |
| DLSS Quality | 39.36 | 44.4% | 129.6 W |
| DLSS Balanced | 38.42 | 43.0% | 127.0 W |
| DLSS Performance | 38.71 | 42.4% | 125.8 W |
| Native / effects off (repeat) | 39.69 | 36.3% | 95.1 W |

All three modes remain within the repeated native baseline's roughly 38–40 FPS
range. Balanced and Performance reduced measured GPU use slightly relative to
Quality (about 127 W / 126 W versus 130 W), while still costing more than effects
off (91–95 W). This is a small single-pass difference, not a demonstrated
meaningful FPS gain.

Whole-GPU counters include desktop activity. This single scene and paced runtime
cannot establish a general FPS benefit or predict WiVRn performance. Compare
rendering cost as well as FPS. See the machine-readable [evidence](evidence/dlss-presets-20261112.json)
and the [earlier Quality / FG measurements](sr-frame-generation.md).

## Validation

- Quality, Balanced and Performance: both eyes on two consecutive frames, with
  matching color/depth/motion dimensions, `sr_evaluate ... output_applied=1`,
  full-size reconstructed output and finite HDR. Alpha matched bilinear input
  within the existing hardware-filtering/FP16 tolerance.
- Balanced and Performance with FG: actual generated stereo images and
  pixel-identical real-image RGB round trips. This is not a headset quality test.
- Native controller cycle through all presets, Back/resume and readable
  Performance text in the shortened **DLSS SR** row. Mode changes stop the old
  helper before replacing shared transport, restore native bindings and reset
  each eye's history at a stereo boundary.
- Direct quit with Performance active and FG off passed; six private test files
  were restored and all 64 tracked regular files were unchanged before installation.
  Installation replaces only the matched binaries and preserves the runtime INI.
- Five native tests pass; x86 layer and x64 helper builds pass. Matched protocol
  **7** is required; older helpers are rejected.
- Private off/on side-by-side stereo previews were inspected. Game assets and
  private captures are not distributed.

A rapid preset cycle with FG enabled followed by resume and quit exposed an
unresolved shutdown stall: the presentation drain timed out, the helper exited,
and the game remained in NVIDIA `vkDestroyDevice`. The verified isolated process
needed SIGTERM. Earlier slower menu/quit tests passed; FG teardown must not be
considered fully reliable. FG remains optional and defaults off. This preset
change does not fix the reported DLAA haze.

## Installation and controls

Close the game/helper, back up and replace the matching layer/helper pair.
Preserve the existing runtime INI. No extra NVIDIA dependency is needed beyond
`nvngx_dlss.dll`; `nvngx_dlssg.dll` is only for optional FG. Keep the existing
Fluorine profile and Steam command.

```ini
[super_resolution]
enabled=0
mode=Quality
capture_probe=0
```

The CLI accepts `sr quality`, `sr balanced`, `sr performance`, `sr off` and
`sr on` (resume the last selected preset). See [build and controls](../neural/docs/SR-FRAME-GENERATION.md).

Input sizing follows NVIDIA's [DLSS integration guidance](https://github.com/NVIDIA-RTX/Streamline/blob/main/docs/ProgrammingGuideDLSS.md#30-check-dlss-settings-and-setup-viewport-rendering-size);
the sizes above are measured from the local runtime, not inferred from a label.
Captures preceded a menu-label-only change from “DLSS Super Resolution” to
“DLSS SR”; final-build controller checks and timings use the shorter label.
Physical-headset motion, ghosting, streaming latency and comfort remain untested
for the new presets.
