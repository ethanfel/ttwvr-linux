# Bundled DXVK / DLSS retest — TTWVR 1.1.3

**Follow-up:** the [native Vulkan FSR repair](native-vulkan-fsr.md) now produces
complete, moving stereo output with stock Wine and a scoped DXVK fence change.
The stale-DLSS findings below remain unresolved.

**Not ready for use under Proton. No performance improvement is validated.**
The author's worker path loads after two guarded compatibility edits, and its
64-bit helper can evaluate DLSS through D3D11 in an isolated fence experiment.
However, the displayed DLSS eye images remain stale when the simulated headset
moves. The apparently higher frame rate must not be presented as a working gain.
The normal Fluorine launcher and rendering addon remain unchanged.

## Why investigate this path?

Proton already translates the game's D3D9 rendering to Vulkan. The author's
bundled `ttwvr_dxvk_d3d9.dll` adds a custom worker handoff: it can post upscaler
work early rather than waiting for the game thread to submit it later. The
original status counters confirm worker posts, with no legacy posts in this
experiment. This scheduling is a plausible advantage, not a measured cause
of improved usable performance.

The supplied helper evaluates **D3D11 NGX**, unlike our D3D12 neural backend.
Hardware-breakpoint tracing of the actual `nvngx_dlss.dll` entry point observed
four separate `NVSDK_NGX_D3D11_EvaluateFeature` calls, alternating feature IDs
3, 4, 3, 4, all returning `0x1` (success). This is consistent with per-eye
processing, but does not prove that fresh output reaches the headset.
[D3D11 DLSS is supported by DXVK-NVAPI](https://github.com/jp7677/dxvk-nvapi#readme);
there is no need to infer that D3D12 is mandatory for SR.

## Exact supplied build and diagnostic edits

| Component | SHA-256 |
|---|---|
| `TTWVR1.1.3.zip` | `b58b07aadc4bd1076242baa5f80d17109db026cd80f236f6f1ccc437d42bc665` |
| Original `TTWVR.dll` | `4c45d6e5adb5da76dd3d08bf0639d360761d3dc50d891ea0bd19b51e4fb006a0` |
| Bundled `ttwvr_dxvk_d3d9.dll` | `c7f64a18da8fd5f455e9453ade4abcfaef9dec63cc1cb80a89215a12b6131cf2` |
| Updated `ttwvr_dlss_host.exe` | `c9122a1b6464f04ecdcd9760f1c7a8593ae69ea9e6814ad6e82a3f9af2b73b10` |
| Diagnostic `TTWVR.dll` | `b82d341690cf0068b561803dca22a4ac07ea8042e6437c68f3ab9a85fcd16530` |

The archive's DLL metadata still identifies itself as 1.1.2; hashes identify
this supplied 1.1.3 build. The bundled DXVK DLL matches the older archive,
while the helper changed.

`tools/experimental_ttwvr_renderer.py` now supports the exact original 1.1.3
hash as well as 1.1.0. For 1.1.3 it changes:

- File offset `0x513f2c`, RVA `0x514b2c`: the six-byte early return branch
  `0f8589010000` becomes six NOPs. This allows TTWVR to load its bundled DXVK
  despite Proton's existing D3D9 module exposing ordinal `0x6e61`.
- With `--winevulkan`, the UTF-16 loader name/padding at `0xa3df70` changes
  from `vulkan-1.dll` to `winevulkan.dll`, the module DXVK actually uses here.

The combined variant changes 20 bytes. The tool rejects unknown input hashes
and existing output files. Both supported versions were checked with and
without the Wine lookup edit; original files were preserved. Do not deploy
these edits as a performance fix or redistribute patched mod binaries.

## Runtime results

With ordinary Proton Experimental, the updated helper still crashes importing
its shared D3D12-style fence across processes. The log records
`d3d12 fence from other process` followed by `Exception 0xc0000005` in
`vkImportSemaphoreWin32HandleKHR`. The game disables the upscaler and falls back.

The private [monotonic-fence diagnostic](fence-experiment.md), overlaid only
inside the test launch, avoids that startup crash. It is **not a general fence
fix**. GPU waits/signals remain enabled, but rewind emulation is bypassed;
correct full-application synchronization is not established.

At the original menu's 150% resolution, status reports 2944×2208 input and
4480×3360 output. Captures of both eyes contain a 2944×2208 image in the upper
left, with the remainder black. At 100%, status reports 1984×1488 input and
2944×2208 output; the capture fills each eye, but subsequently fails freshness
validation. The latter output size matches our current addon's output size.

Legacy INI keys were also checked rather than assumed effective. This build
retained live defaults (`iLogLevel=2`, `iDumpFrame=0`, lens margin 4 degrees,
foveation 0.7) despite diagnostic values added to `[General]` and `[DLSS]`.
A temporary change to those verified settings in the isolated process did not
establish fresh DLSS output. Defaults were restored before timing.

## The decisive movement check

Same loaded corridor, stationary pose initially, then a 0.6 m horizontal
translation of the simulated HMD. OpenXR capture metadata confirms the changed
pose in both eyes.

| Path | Left-eye changed pixels | Right-eye changed pixels | Result |
|---|---:|---:|---|
| Original DLSS + fence diagnostic | 0% | 0% | Stale images, byte-identical DDS hashes |
| Original FSR, reverse translation | 84.97% | 84.98% | Images respond to movement |

During the DLSS movement check, posted/shown counters advanced from
1317/1316 to 1347/1346, with no missing output, slot-busy event or refused-copy
flag. These counters still did not identify stale pixels. In the earlier fixed
pose run, captures at frames 1740 and 12600 were also identical per eye.
Successful NGX calls, a live helper and incrementing presentation counters are
therefore insufficient validation of this transport.

The test localizes a problem to the original DLSS route relative to the
FSR control. It does not yet establish whether input sharing, output sharing,
synchronization or a combination is responsible. Shared-image import warnings
are present; their causal role is unproven. FSR responding to movement is not
full validation of its rendering quality or headset performance.

## Timing — rejected as a usable DLSS benchmark

RTX 5090, isolated Monado simulated HMD at 90 Hz, same corridor and pose,
2944×2208 output per eye. Each window used 5 seconds settling and about 30
seconds sampling. Captures, debugger probes and image processing ran outside
the timing windows. Our layer was passive, with NR/DLAA/SR/FG disabled.

| Path | App submissions/s | GPU utilization | GPU power |
|---|---:|---:|---:|
| Native fallback, first run | 37.96 | 35.1% | 98.5 W |
| Original DLSS, **stale output** | 64.00 | 14.8% | 87.5 W |
| Native fallback, repeat | 38.63 | 35.1% | 95.7 W |

The apparent 66% gain is **invalid as a gameplay performance claim** because
DLSS did not deliver updated eye images. Earlier measurements of our working
Quality path were about 39 FPS at this runtime frequency; they are recorded
separately in [DLSS presets](dlss-presets.md). No WiVRn streaming or physical
headset result is claimed by this retest.

## Reproduction and next work

`python3 tools/inspect_ttwvr_upscaler.py <FalloutNV-Linux-PID>` reads the original
status counters without changing memory. It checks the process name and mapped
DLL hash, including launch-scoped overlays, before using verified 1.1.3 offsets.
It requires access to the user's process memory. Its snapshot is non-atomic;
use it for diagnostics, never as proof of fresh pixels. `helper_mode_label`
is the helper's label, not a guarantee that native FSR is inactive.

[Machine-readable evidence](evidence/bundled-renderer-113-20261112.json) records
builds, timings, successful evaluations and the failed movement test. Captures
and full logs remain local; no game, mod or NVIDIA binaries are distributed.

The useful directions are the author's early DXVK worker handoff and D3D11 SR
backend. A source-level port needs correct shared-image ownership, lifetimes
and cross-process fence semantics, followed by a movement test before any
benchmark. The author's renderer/helper source would help distinguish these
issues; it was not included in the supplied archive. The bundled NVIDIA SR
DLL reports version 310.9.1.0; our separate addon uses 310.9.16.0. A controlled
runtime-version comparison remains untested and is another useful follow-up.

All five modified private test files were restored and their hashes checked.
All 64 monitored regular-install files and the installed Proton `win32u.so`
were unchanged. WiVRn's default registration was not modified. No diagnostic
renderer or fence overlay was added to normal launches.
