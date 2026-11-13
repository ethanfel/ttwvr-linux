# Supplied CachyOS OpenXR build — 2026-11-13

The supplied `proton-cachyos-wineopenxr-fix.tar.gz` passed isolated TTWVR 1.1.4
DLSS and native Vulkan FSR gameplay checks with **its own OpenXR bridge**.
Our separate two-byte bridge workaround was omitted. The TTWVR renderer,
shared-output, FSR handle and DXVK pool/opaque-fence patches were retained.

This is validation of one supplied archive, not general CachyOS support.
The setup tool now supports this exact archive alongside Experimental and
combines its built-in OpenXR fix with the existing renderer patches. The initial
comparison below left the regular installation unchanged. WiVRn default runtime
registration is never changed by this integration.

## What the build fixes

The archive identifies itself as `cachyos-11.0-20261015-slr`, built November 12.
Its exact custom source commit and official-release provenance are not known.
Community invite: [TTWVR Discord](https://discord.com/invite/ttwvr).

Archive source supplied by the user: [Discord channel](https://discord.com/channels/1570233209462194208/1570481280825561098)
(access required; channel contents could not be independently retrieved).
Hashes are recorded in the [evidence JSON](evidence/cachyos-openxr-20261113.json).

Disassembly shows that both composition-layer converters size each element
using its own type, and skip null elements. The supplied x86_64 Unix bridge
passed **28 quad pose/size checks across 14 conversions**, covering win32 and
win64 inputs, three projection/quad orderings and four null placements.
This is more general than our projection-first allocation workaround.
The native i386 Unix bridge was not executed: these game tests used WoW64.

## Loaded-save and stereo checks

A separately copied prefix, game and existing automation harness used the full
extracted Proton build. A test-only copy of `dxvk_overlay.py` checked the
archive's original DLL hashes before applying the proven private renderer.
No installed Proton DLL was overwritten. Process maps confirmed CachyOS's
Windows/Unix OpenXR pair and the simulated Monado runtime were loaded.

| Mode | Input per eye | Output per eye | Captured frame pairs |
|---|---|---|---|
| DLSS Quality, 200% | 2944×2208 | 5888×4416 | 2850/2851, then 3090/3091 |
| Native FSR Quality, 100% | 1984×1488 | 2944×2208 | 2700/2701, then 2910/2911 |

Both eyes had changing consecutive images. A controlled head translation
changed over 99.9% of the reconstructed center in each eye. Four actual DLSS
NGX evaluations alternated eye features 3/4/3/4 and returned `0x1` each time.
FSR was enabled, initialized and active with no failure or output-copy refusal.
The passively captured stereo previews show the loaded corridor, not the menu.
No archived rendering effect was active.

[DLSS stereo preview](evidence/cachyos-dlss-stereo.png) ·
[FSR stereo preview](evidence/cachyos-fsr-stereo.png)

## Short timing comparison

| Mode | Experimental FPS | CachyOS FPS | Experimental app ms | CachyOS app ms |
|---|---:|---:|---:|---:|
| DLSS Quality, 200% | 58.2 | 55.6 | 10.09 | 10.35 |
| FSR Quality, 100% | 52.2 | 52.7 | 10.82 | 10.75 |

Each setting used the same saved scene, fixed pose, 90 Hz simulated runtime,
resolution, quality, patched renderer and passive timing layer. Each timing
window lasted approximately 30 seconds, before image readback and NGX
instrumentation. Experimental used a second independent copy of the original
prefix, not a downgrade of the CachyOS prefix. Order was CachyOS DLSS/FSR,
then Experimental DLSS/FSR; the comparison was not repeated or randomized.
The packages also differ beyond OpenXR, including their 64-bit dependencies.

These measurements do not establish a performance improvement or isolate the
cost of the OpenXR change. GPU use/power are whole-device samples, and the
logged application span is CPU wall time, not a GPU execution-time query.
Simulated-headset FPS is not a WiVRn streaming or physical-headset benchmark.

## Limits and preservation

- Menu startup, the saved scene, stereo freshness, translation and graceful
  shutdown passed; long sessions and physical-headset comfort remain untested.
- The initial stereo/timing tests used the private capture harness. See the
  subsequent combined-installer/native-Fluorine validation below.
- The tests retained the other Linux patches. They do not show those patches
  can be removed, or that stock CachyOS DXVK alone is sufficient.
- This package automatically provisioned an FSR4 DLL in its disposable prefix.
  TTWVR's tested FSR path remained its embedded Vulkan FSR 3.1.4; this was not
  an FSR4 integration test.
- Both completed batches restored 13 staged files each, closed their game
  processes and left all 65 checked regular-install files unchanged.
- The first Experimental comparison attempt stopped before gameplay because
  its protected installation could not open `dist.lock`. The retry mounted a
  private writable lock file while keeping installed Proton read-only.

The Steam entry point stays the same when Fluorine selects the new Proton and
prefix. See the current [installation guide](installation.md).


## Combined installer and native Fluorine integration

The setup tool now recognizes both exact Proton builds. It verifies the
Windows/Unix OpenXR pair and stock DXVK files, keeps the renderer fixes, and
writes an empty `openxr_bridge` setting for CachyOS to clear stale overrides.
Experimental retains its private bridge workaround. Automatic Proton upscaler
replacement is disabled for the author renderer.

**36 tests passed**, including mixed-runtime rejection, both bridge-selection
paths, rollback and real native save mounts. The updated tool was then applied
to a separate copy of the native Fluorine installation using the original
1.1.4 mod and verified dependencies. Fluorine mounted its FUSE view, launched
the generated game wrapper with CachyOS, and loaded the Springvale save.
Output advanced at 2944×2208 input / 5888×4416 output, with no copy refusal;
four actual NGX evaluations alternated features 3/4/3/4 and returned `0x1`.
The game exited through its own quit command. Installer rollback passed on
that copy, preserving the settings changed during the test in a conflict backup.

The initial automation timeout came from writing through the old MO2 backing
path; sending test commands through the native game's FUSE view resolved it.
The native desktop capture included full-body overlap with simulated tracking,
so it is not used to claim image quality. The earlier controlled stereo images
remain the pixel evidence; physical WiVRn confirmation is still pending.

The regular Fluorine instance was switched using a copied prefix and private
Windows directory, with a transaction recording the three changed configuration
files. The old prefix was retained, all 30 checked save files remained unchanged,
and the existing Steam entry point was retained. Its complete game-wrapper
dry run selects CachyOS, WiVRn and patched DXVK without a bridge overlay.

[Native integration record](evidence/cachyos-native-integration-20261113.json).
