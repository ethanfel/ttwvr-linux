# Diagnostic shared-timeline experiment

This is **not enabled in normal launches** and is not a general D3D12 fence
implementation. See [the original crash](performance.md#confirmed-helper-crash-in-protons-fence-import).

Wine's D3D12 fence implementation emulates rewinding values using a
process-local timeline structure. TTWVR's separate 64-bit helper crashes when
importing that structure's pointer from its 32-bit parent. For a strictly
increasing fence, Vulkan's native timeline already provides the needed
synchronization and can be shared across processes. That special case can be
tested independently of support for rewinding.

`tools/experimental_monotonic_fences.py` creates a private, hash-guarded copy of
the installed `win32u.so`. In `win32u_vkCreateSemaphore`, it changes the branch
over rewind-emulation setup from conditional to unconditional. The structure
therefore retains its zero-initialized metadata, while the Vulkan timeline and
shared-handle operations remain active. It does **not** skip GPU waits/signals
or pretend they completed. Fence values that decrease are outside its scope.

| Item | Value |
|---|---|
| Original SHA-256 | `d2f34ac08f9768a75457957b28a50c4f517cb919489f02f9e07e364c7ec8a75c` |
| Diagnostic copy SHA-256 | `689b18550d5485ea0a21d8bfc0dd886ac8d8f6a21df549d9a6a3b73ae513dd33` |
| Branch file offset | `0x147dde` |
| Before / after | `74 45` / `eb 45` |
| Changed bytes | 1 |

The copy is overlaid with bubblewrap for the test launch only. The installed
Proton file is unchanged. No wrapper setting enables this experiment.

## Standalone result

The same `tests/shared_fence_probe.cpp` program that failed against the original
runtime now passes: the 32-bit parent signals 7, the 64-bit child opens the
shared fence and observes 7, then signals 11, which the parent observes.
Parent and child both exit 0. The original import access violation disappears.
Both architectures compile without warnings. Result logs are written to
`fence-probe-parent.txt` and `fence-probe-child.txt` in the test working directory.

This validates only that monotonic round trip. A real application must also be
checked for resets, resource recreation, helper restarts, and proper lifetime.
## TTWVR result: not usable

An isolated simulated-headset run combined this diagnostic runtime with the
bundled-renderer and `winevulkan.dll` lookup patches, retaining TTWVR's original
2944x2208 defaults. Both process-visible hashes matched the candidates.

The helper stayed alive and the original fence-import access violation did not
recur. However, after loading the save, the desktop rendered **black except for
subtitles and the FPS HUD**. The user reported this and a local capture
confirmed it. GPU shared-image import warnings were present; their causal role
has not been established. The helper's sampled main thread was waiting for
objects. Neither absence of a crash nor a passing fence probe establishes
usable DLSS output.

The run was stopped and the diagnostic overlays removed. Normal launches use
the original `win32u.so` and do not enable either experimental DLSS patch.
Correct image transfer and full fence semantics remain unresolved. No physical
headset result was obtained from this simulated run.

## Updated 1.1.3 helper: stale output

The [1.1.3 retest](bundled-renderer-113.md) reaches successful D3D11 NGX
evaluations and incrementing worker/output counters. At 2944×2208 the images
fill both eyes, but a 0.6 m HMD translation changes zero pixels under DLSS;
the FSR control changes about 85%. An apparent 64 FPS result is therefore
not a usable performance improvement. The diagnostic remains unsuitable for
normal launches; shared-image transfer and complete synchronization semantics
are still unresolved.

## Narrower native-FSR follow-up

The [author-path FSR experiment](native-vulkan-fsr.md) now uses an isolated
32-bit DXVK change to export opaque Vulkan timeline handles. It runs with the
original Wine library and passed 64 parent/child GPU fence round trips. This
still requires increasing fence values and is not a general D3D fence fix.
Native FSR output was repaired separately; the old DLSS freshness failure is
not considered fixed.
