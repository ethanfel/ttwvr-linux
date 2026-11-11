# Full-resolution DXVK performance fix

For setup commands, see [Apply the Linux fixes](linux-fixes.md#3-dxvk-performance-patch).
This page records the reason for the patch and its measurements.

This source-built performance patch has passed an initial physical Quest 3
test through WiVRn at 120 Hz, separately from the unsuccessful DLSS patches.
It preserves the original TTWVR DLL, 2944x2208 eye dimensions, texture detail,
and `Resolution=150%`. Native D3DCompiler 47 and the proven OpenXR bridge patch
remain in use.

## Reason for testing

The [original-resolution profile](performance.md#original-resolution-allocation-investigation)
found repeated 26,001,408-byte memory mappings and substantial main-thread
driver overhead. Each eye buffer occupies about 24.8 MiB. In the exact DXVK
commit bundled with the tested Proton build, the mapped-memory pool limit is
16 MiB for a 32-bit process. Requests larger than the applicable pool limit can
fall through to a dedicated allocation instead of reusing pooled memory.

[`patches/dxvk-32bit-mapped-pool.patch`](../patches/dxvk-32bit-mapped-pool.patch)
raises that limit to 32 MiB by changing its divisor from 16 to 8. It leaves the
64-bit limit, Vulkan synchronization, rendering dimensions, and graphics
settings unchanged. The existing small-heap check still applies. This is not
a general recommendation for all 32-bit games: larger pools can consume more
of their limited address space, especially in a long session.

## Reproducing the builds

The source revision matches the installed DXVK version:
[`cf0a0d90f8965147741ad17dcd0b263817572ab3`](https://github.com/doitsujin/dxvk/commit/cf0a0d90f8965147741ad17dcd0b263817572ab3).

```sh
git clone https://github.com/doitsujin/dxvk.git dxvk
git -C dxvk checkout --detach cf0a0d90f8965147741ad17dcd0b263817572ab3
git -C dxvk submodule update --init --recursive
git -C dxvk apply /path/to/ttwvr-linux/patches/dxvk-32bit-mapped-pool.patch
meson setup build32 dxvk --cross-file dxvk/build-win32.txt \
  --buildtype release -Denable_d3d8=false -Denable_d3d10=false -Dbuild_id=true
ninja -C build32
```

The experiment builds `d3d9.dll`, `d3d11.dll`, and `dxgi.dll`. A control build
uses the same source revision, compiler, and options with the patch reversed.
The direct-image-mapping setting is kept identical between these two builds:
`DXVK_CONFIG='d3d11.disableDirectImageMapping = True'`.

## Launch isolation

Proton replaces the prefix's renderer DLLs at startup. Binding individual
replacement DLLs onto those destinations fails with `EBUSY` when Proton tries
to unlink them. Instead, the diagnostic launch binds a private copy of the
prefix's `windows/syswow64` directory at its original location and overlays the
three candidate DLLs on Proton's `files/lib/wine/dxvk/i386-windows` sources.
Proton can then populate the private directory normally. These mounts exist
only in the diagnostic launch namespace; original prefix and Proton renderer
files stay unchanged. The game-visible DLL hashes are checked after launch.

The simulated headset uses the existing Monado null compositor. Its adaptive
pacing differs from WiVRn, so its FPS captures are not predictions of physical
headset performance. Save-file hashes are checked against the pre-test backup.

## Results and normal launcher

With the same source revision, compiler, settings and saved Vault 101 scene:

| Renderer | Spectator FPS snapshot | Main-thread `mmap` calls in six seconds |
|---|---:|---:|
| Unmodified control build | 18.8 | 599 |
| 32 MiB mapped-pool build | 44.1 | 2 |

The user also observed the FPS dropping when the control was restored. These
are scene snapshots, not formal average FPS measurements; NPC animations and
the simulated view differ slightly. Tracing itself adds overhead. However,
the matching control and disappearance of repeated mappings support a real
allocation bottleneck, rather than a change in rendering quality or compiler
alone. A later launch through the integrated wrapper rendered the scene at a
41.0 FPS snapshot, with all three candidate hashes verified inside the game.
The subsequent physical WiVRn result is recorded below. Long-session
address-space use remains untested.

The locally enabled `[VR]` settings in `ttwvr-linux.ini` are:

```ini
dxvk_renderer=../performance-candidates/dxvk-pool32
dxvk_syswow64=../dxvk-test-syswow64
```

Paths resolve beside MO2. The renderer directory must contain the three built
DLLs and `hashes.json`, a JSON object mapping their filenames to SHA-256 hashes
recorded when preparing the build. This checks file integrity against that
local manifest; it does not authenticate arbitrary third-party builds. The
private Windows directory is a complete copy of this prefix's `syswow64`,
with symlinks preserved and **without hard-linking regular files** to the real
prefix. Prepare it while the game and MO2 are closed.

The wrapper rejects any different installed Proton renderer hashes, changed
candidate files, missing settings, and attempts to use the real prefix as the
private copy. It enables the tested staging-buffer option for this VR launch
only. Flat ignores both settings. Fourteen checks pass, including tampered
files, real-prefix aliases, and Flat isolation.

Rollback: remove both `dxvk_renderer` and `dxvk_syswow64` entries and restart
MO2. Original prefix and Proton DLLs require no restoration. Keep the proven
OpenXR and native compiler fixes. The lower-resolution and DLSS experiments
remain disabled. All eight saves still match their pre-test hashes.

## Further pacing diagnostic

With the integrated pool fix, 80 live samples read from Monado's pacing state
(after debugger attachment effects had settled) gave medians of 0.010 ms
between wait/begin, 12.420 ms between begin/submit, and 11.478 ms between submit
and completion. The display period was 11.111 ms. These are runtime wall-clock
phase estimates, not GPU timestamp-query measurements. The simulator rounds
phases exceeding one display period to a slower application cadence.

Temporarily toggling the running simulated process's cached
`U_PACING_APP_USE_MIN_FRAME_PERIOD` option did not establish an improvement:
a capture showed 30.4 FPS and the view/NPC positions differed. The option was
restored in a `finally` block and the process subsequently stopped. No runtime
file, launch default, or WiVRn server setting was changed. This experiment
does not justify overriding physical headset pacing.

A separate launch added `d3d11.cachedDynamicResources=a` to the proven pool
configuration. The 2944x2208 targets and candidate DLLs stayed the same. It
showed a 41.1 FPS snapshot and a 13.597 ms median begin/submit phase across 80
settled live samples, versus the earlier 12.420 ms sample. No benefit was
established; this option was removed for subsequent launches.

## 120 Hz simulated cadence

Keeping the same patched renderer, staging setting, 2944x2208 targets and saved
scene, the simulated null compositor was launched at 120 Hz instead of 90 Hz.
TTWVR confirmed an 8.33 ms runtime period. The user reported a large jump and a
spectator capture showed **58.9 FPS**, versus the earlier 41–44 FPS snapshots
at 90 Hz. This is consistent with a roughly 60 Hz application cadence instead
of 45 Hz when individual phases exceed one hardware refresh interval. It is
not native 120 FPS rendering. A subsequent non-pausing read of Monado's frame
prediction counter measured 718 OpenXR frame requests over 12.014 seconds:
**59.76 requests/second**. That supports the observed cadence but is not a
physical display or dropped-frame measurement.

Settled runtime phase medians were 12.741 ms begin/submit and 11.516 ms
submit/completion, with an 8.333 ms display period. A GPU sample was 54% usage
and 113 W; an earlier 90 Hz sample was 37% and 104 W. These samples are not
controlled GPU benchmarks. No extra cached-upload or forced-pacing option was
retained, and no image quality setting changed.

Before the physical retest, WiVRn's D-Bus properties listed 72/80/90/120 Hz
for the user's Quest, with its preference at 90 Hz and the headset disconnected. That preference
is client-owned and read-only over this server interface. The headset's
[Video settings](https://github.com/WiVRn/WiVRn/blob/master/client/scenes/gui_settings.cpp)
provide the refresh-rate selector. The simulator result alone cannot establish
headset encoding, latency or smoothness; the physical retest follows below.
The local simulation launcher accepts `TTWVR_SIM_FPS=120`; its default remains
90 Hz for reproducible baseline runs.

## Physical Quest 3 / WiVRn result

The user selected 120 Hz and reconnected the Quest. WiVRn reported the headset
connected, a preferred refresh rate of 120 Hz, and an active session. The normal
VR launcher started the game with all three private renderer hashes verified
inside its namespace; simulated-headset environment variables were absent.

TTWVR identified WiVRn 26.9 and the Quest 3, initialized direct OpenXR, and
reported an 8.33 ms runtime period. Loaded gameplay retained 2944x2208 eye
targets and `Resolution=150%`. The DLSS path remained inactive. A spectator
HUD capture showed **59.8 FPS**; the user reported **“clear and very smooth”**
in the headset. This confirms usable physical headset output and a successful
initial gameplay check. It is not a sustained-session, outdoor-scene, latency
or dropped-frame benchmark, and it does not mean the game renders 120 FPS.

The proven renderer configuration remains enabled and the game was left running.
Logs and screenshots remain local. No further graphics or runtime changes were
made after the successful report. The patched configuration has not been
retested physically at 90 Hz; a return toward 45 FPS there is an expectation
from current frame times and observed pacing, not a measured physical A/B.
