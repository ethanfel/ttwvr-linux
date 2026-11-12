# Native VR settings, MSAA and DLAA motion correction

The **TTWVR Rendering Addon** adds its controls directly to **VR Settings**,
after Resolution. It is an independently enableable Fluorine mod containing
an xNVSE startup DLL and the legacy fallback scripts. Its matching OpenXR layer
and 64-bit neural helper remain in the private runtime directory. Build/install
steps are in [the README](../README.md#in-game-controls) and [Steam setup](STEAM.md).

## Controls

| Control | Behavior |
|---|---|
| Anti-aliasing | Off, native-resolution DLAA, MSAA 2×/4×/8× |
| Neural Rendering | Independent Off/On when MSAA is off |
| NR model | A/B |
| NR processing scale | 25/50/75/100% per dimension; final eye size unchanged |
| NR strength/tone/detail | 0–100%, in 10% steps |
| DLSS Super Resolution | Off / Quality; owns temporal AA, excludes NR/DLAA |
| Frame gen. | Off / 2x (experimental); independent of SR |

Rows save immediately. NR/DLAA/SR/FG settings apply live at a stereo frame boundary.
MSAA changes require restarting Fallout; the menu marks pending choices with
`(restart)`, including a return from MSAA to Off/DLAA. MSAA currently excludes
NR/DLAA/SR/FG because their GPU depth transport requires single-sampled depth.
Selecting MSAA disables neural processing. Enabling NR clears requested MSAA
for the next launch; active MSAA still prevents neural evaluation until restart.

The unavailable original Upscaler, Upscaler Level and Sharpness rows are hidden.
All other VR rows remain. Navigation uses the game engine's own option handling;
the older X/F10 input reader is inactive while the native menu is installed.
Disable the addon and restart to restore the original VR menu. All neural
features and MSAA default off in the distributed configuration.

See [SR/FG contracts and validation](SR-FRAME-GENERATION.md) for the new controls.

## Compatibility and implementation

The layer and startup plugin share an exact-hash version table:

| Supplied build | TTWVR.dll SHA-256 |
|---|---|
| 1.1.0 | `8a3a71b48d362984fe346c73b68dfbd476d6e138d6831f26f10c6f506cd03880` |
| TTWVR1.1.3.zip | `4c45d6e5adb5da76dd3d08bf0639d360761d3dc50d891ea0bd19b51e4fb006a0` |

The 1.1.3 archive's `meta.ini` still says 1.1.2; its actual DLL hash selects
compatibility. Unknown builds are rejected. No TTWVR/game binary is edited on
disk. See [the upgrade and validation note](../../docs/ttwvr-1.1.3.md).

In 1.1.3, **VR Settings** is a main/pause-menu entry beside Settings. In 1.1.0,
open **Settings → VR**. The addon follows the original version's menu structure.

- The layer wraps TTWVR's native option builder at RVA `0x3f0aa0` / `0x40b730`, using its
  edit-state predicate at `0x3f2120` / `0x40d180` (1.1.0 / 1.1.3). It verifies the game constructor and vector
  append signatures before allocating engine-owned UserOption rows. Both the
  original append routine and the observed utility-optimized version are accepted.
- Original upscaler objects remain allocated for TTWVR ownership. Their names and flags are checked before their menu
  flags are cleared, and our rows are inserted after Resolution. Initialization
  is idempotent. Unknown signatures retain the original menu/fallback.
- The xNVSE plugin intercepts TTWVR's integer-setting override at RVA `0x4f5a00` / `0x510ae0`
  after verifying its hash. It substitutes only the `iMultiSample` value, using
  `Data/NVSE/Plugins/TTWVRRenderingAddon.ini` → `[antialiasing] msaa_samples`.
  Accepted values are 0, 2, 4 and 8. It registers no script opcode or save data.
- The menu persists MSAA in the addon INI and the existing profile INIs, plus
  the runtime INI's requested-mode field. The bootstrap INI is the startup source.
- Actual MSAA is measured on 2944×2208 HDR scene targets. The presentation buffer
  remains single-sampled even when scene MSAA works; neither its sample count
  nor the later game-setting value proves MSAA is inactive.
- All neural work and motion/jitter injection bypass active MSAA. Unknown/missing
  startup plugins leave native menu integration inactive. NVIDIA DLLs remain
  separate dependencies; the addon bundles neither game nor NVIDIA code.

## DLAA correction

The scene jitter code shifts rasterized geometry by **minus** the Halton sample
location. Previously the NGX evaluation received that sample location directly,
with the opposite sign from the actual raster displacement. DLAA therefore
accumulated inconsistent subpixel phases.

The helper now passes the raster displacement to NGX. Background reprojection
still uses the sample location to reconstruct the original unjittered ray;
object motion remains current-to-previous and excludes jitter. NR guide sampling
is unchanged. A shared conversion and a geometric regression test compare actual
perspective-vertex displacement against the value supplied to NGX, across all
eight phases and multiple depths. Depth/W and graphics-state restoration remain
unchanged. No sharpening, resolution reduction or model change is used.

This follows the coordinate contract in NVIDIA's
[DLSS programming guide](https://github.com/NVIDIA/DLSS/blob/main/doc/DLSS_Programming_Guide_Release.pdf),
sections 3.6–3.7. The guide also identifies bad motion/jitter information as a
possible cause of fuzzy detail; it does not establish the cause of every visual
artifact in this game.

## Isolated validation, 2026-11-12

A copied game, prefix, profile and saves were used with Monado's simulated HMD.
The original installation, Titanfall files and WiVRn registration were protected.
The HMD was also temporarily held stationary **in the test process only**, without
editing the shared runtime, to compare motion and settling.

- Native menu: engine gamepad navigation reached VR Settings, scrolled all
  addon rows, saved AA changes and enabled DLAA live. Original built-in upscaler
  rows were absent. AA/NR rows appear directly after Resolution.
- MSAA 4×: frames **810/811**, both final eyes 2944×2208. The D3D9 trace contains
  matching full-size RGBA16F scene color and D24S8 depth with four samples;
  resolved output/presentation surfaces retain one sample. Stereo previews were
  visually checked for normal scene rendering.
- MSAA 8×: frames **1470/1471** in a separate run, both eyes at the same final
  resolution. Scene draws used matching eight-sample color/depth. The native menu
  displayed the active MSAA level, and the scene-target diagnostic confirmed it.
- MSAA 2×: frames **1320/1321** in another run, both final eyes at full size,
  with matching two-sample HDR/depth. The value was selected in the native menu,
  persisted to the addon INI, and used by the startup hook. Switching from active
  8× MSAA to DLAA displayed `(restart)` and kept neural evaluation bypassed.
- Original DLAA motion inputs: frames **510/511**, both eyes. Warping original
  pre-DLAA images by the supplied guides reduced mean byte-domain HDR-proxy error
  from **7.421/7.350** to **0.691/0.810**. Reversing motion made it worse. This
  checks those frames; it is not proof for every game material or animation.
- Corrected moving DLAA: frames **12390/12391**, both eyes, with actual
  `dlaa_evaluate`/`output_applied=1`. Original-image motion validation remained
  accurate. Post-stop captures **12420/12421** also applied both eyes.
- Fixed-camera comparison: original-sign frames **6810/6811**, corrected-sign
  **10020/10021**. Camera matrices/origins were unchanged. On the common masked
  background, consecutive HDR luminance MAE fell from **0.002785/0.002913** to
  **0.000834/0.000717** (left/right). High-frequency energy was higher in the
  corrected pair. These are two-frame diagnostic measurements with different
  jitter phases and moving NPCs excluded, not a general image-quality score.
- Combined NR/DLAA: frames **15900/15901**, both eyes, with both evaluations and
  output application confirmed. Exact-frame HDR previews retained finite output
  and unchanged alpha in the DLAA-only samples.
- Linked-control refresh: enabling NR while 2× MSAA was active immediately
  displayed `Anti-aliasing: Off (restart)` and `Neural Rendering: On (restart)`;
  the runtime still reported active MSAA and did not start a neural helper.
- Native tests cover projection/jitter conventions, live feature rebuilding,
  eye histories and OpenXR image ownership. The 32-bit addon/layer and 64-bit
  helper compile with warnings treated as errors.

Captures and game-specific test artifacts stay local under the rendering-dev
validation workspace. A capture can stall the game; capture intervals are not
used as an FPS benchmark. A rough application-timing check over adjacent 120-frame log intervals gave
median **42.81 ms before / 43.03 ms after** the jitter correction in the simulated
scene. Motion conditions varied and this was not a controlled headset benchmark;
no performance improvement is claimed. This change uses the same NGX model,
full eye resolution and synchronization path as before. A sustained headset test of motion blur,
text/HUD clarity, stereo comfort and performance is still required. The user
reported blur with both head motion and controller movement; this fix addresses
the common jitter error, but does not prove that all perceived blur is eliminated.
