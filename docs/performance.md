# Performance investigation — 2026-11-11

Hardware/runtime: RTX 5090, NVIDIA 615.78.08, Quest 3 at 90 Hz through WiVRn
26.9, TTWVR 1.1.0, Proton Experimental 11 WoW64. Both the native D3DCompiler
47 workaround and private OpenXR bridge patch remain enabled throughout.

## Baseline and resolution experiment

The user confirmed visible headset gameplay and requested further performance
tuning. Proton's stock DXVK handles D3D9. The TTWVR log reports no custom worker
handoff and no loaded `vulkan-1.dll`, then disables DLSS/FSR for the session.
Selecting DLSS in the mod's INI therefore does not establish that it is active.

After backing up the VR profile's saves and INI, changed only `[VR] Resolution`
from `150%` to `100%`. Launched with `DXVK_HUD=fps,frametimes`. One desktop
spectator capture in the loaded Vault 101 room shows **23.5 FPS**. This is an
instantaneous game-renderer measurement, not a headset compositor benchmark
or a controlled before/after comparison. GPU samples were about 29–36% and
101–107 W. The game remained visible and the OpenXR pose errors did not recur.

The blood-depth warning still reports `2944x2208` in both sessions. We have
not established that this INI change reduces the active rendering workload.
A brief earlier main-thread stack sample reached an NVIDIA allocation through
`win32u_vkAllocateMemory` and `winevulkan`'s 32-bit thunk. These observations
motivate investigating the rendering/copy path; they do not prove a single
performance bottleneck. No VSync or NVTF change has been made.

## Bundled-renderer experiment

TTWVR's loader queries ordinal `0x6e61` in the already-loaded `d3d9` module
and returns without loading its own renderer when that export exists. Proton's
stock DXVK provides it. The bundled renderer contains the mod's custom worker
interface and loads `vulkan-1.dll`.

`tools/experimental_ttwvr_renderer.py` creates a separate DLL copy that replaces
the six-byte conditional return branch with NOPs. This allows the existing
bundled-DXVK load and hook code to run. It does not change that renderer, its
Vulkan capabilities, or the DLSS helper. This is an **experimental diagnostic**,
not an established performance fix and not part of normal launcher settings.

| Item | Value |
|---|---|
| Original TTWVR.dll SHA-256 | `8a3a71b48d362984fe346c73b68dfbd476d6e138d6831f26f10c6f506cd03880` |
| Experimental copy SHA-256 | `7d02f7ba509ab1b2a31a42f8b36f6c46d8ff17ccffeeebe87d0485c42f6f29a9` |
| File offset / RVA | `0x4f8e4c` / `0x4f9a4c` |
| Before / after | `0f8589010000` / `909090909090` |

The script rejects an unknown input hash and refuses to overwrite any output
file. The local test overlays the copy over the MO2 mod DLL with a launch-scoped
read-only bubblewrap bind. The installed original is unchanged. Omitting that
outer test overlay on the next launch rolls it back; the proven OpenXR bridge
overlay remains independent. No third-party binaries or save files belong in
this repository.

The first test loaded `ttwvr_dxvk_d3d9.dll`, and the missing worker-handoff
warning disappeared. It loaded the save, but DLSS still failed at the Vulkan
module lookup. A spectator capture showed 19.8 FPS, so this change alone has
not established a benefit. The original DLL hash outside the launch namespace
remained unchanged; the game saw the expected experimental hash inside it.

### Wine Vulkan lookup variant

Both DXVK builds actually use `winevulkan.dll`, as confirmed by their runtime
logs. The [upstream DXVK loader](https://github.com/doitsujin/dxvk/blob/master/src/vulkan/vulkan_loader.cpp)
tries that module before `vulkan-1.dll`. TTWVR instead calls `GetModuleHandleW`
with only the latter name, so its lookup fails despite an active Vulkan renderer.

The script's optional `--winevulkan` variant also replaces the UTF-16 string at
file offset `0xa1feb0` (RVA `0xa20eb0`) with `winevulkan.dll`. The verified
32-byte string/padding region accommodates the name and its terminating null;
the adjacent error message starts at `0xa1fed0` and is unchanged. Only one code
reference to this string was found, in the upscaler's Vulkan-loader lookup.

This variant changes 20 bytes total and has SHA-256
`1e7383f6ee4eb8e1ec38a68b53e4b62b5d712c4baa699ac56f99aae4ff9ee246`.
It targets Wine only. The original Windows loader name is not retained as a
fallback, and the unmodified diagnostic text would still mention `vulkan-1` if
the new lookup failed. An upstream source change should support both names.

The lookup variant launched the 64-bit `ttwvr_dlss_host.exe`, loaded NVIDIA's
NGX/DLSS libraries, and then the helper crashed. The parent reported that the
helper exited while starting and disabled the upscaler again. This variant is
not enabled in the normal launcher.

## Confirmed helper crash in Proton's fence import

The helper's log identifies this sequence:

```text
win32u_vkImportSemaphoreWin32HandleKHR d3d12 fence from other process.
vkImportSemaphoreWin32HandleKHR Exception 0xc0000005 in Unix call.
```

The fault is at `win32u.so+0x157974`, the `lock inc [rax+0x14c]` reference-count
increment in `win32u_vkImportSemaphoreWin32HandleKHR`. The referenced pointer
belongs to the exporting process, not the helper. The installed `win32u.so`
hash is `d2f34ac08f9768a75457957b28a50c4f517cb919489f02f9e07e364c7ec8a75c`.

[Valve Wine source](https://github.com/ValveSoftware/wine/blob/e8ca5806eec488f5916e83d655ffc5349b6b1702/dlls/win32u/vulkan.c#L4315)
confirms the problem: the cross-process branch clears `semaphore->info`, but
later adds a reference to the unchanged `fence_info.t` imported from the other
process and copies that metadata back. Merely skipping the invalid increment
would not implement the required cross-process synchronization. Fence values
and lifetime need to remain correct across the two processes.

A [separate application's upstream report](https://github.com/jlrouzies-fr/DLSS5-Feeder/issues/121)
describes the same Wine cross-process fence limitation. TTWVR's local crash was
established independently from its log and the actual installed instruction.

### Standalone reproducer

`tests/shared_fence_probe.cpp` reproduces the failure without game/mod assets
or a headset. A 32-bit D3D11 program creates a shared fence, signals 7, then
launches a 64-bit child. The child duplicates the parent's NT handle and opens
the fence. If successful it observes 7 and signals 11 back; the parent verifies
11. Waits are bounded, and a timeout terminates only the test's own child.

Build both architectures with MinGW:

```sh
i686-w64-mingw32-g++ -O2 -Wall -Wextra -municode -static \
  -o probe32.exe tests/shared_fence_probe.cpp -ld3d11 -ldxgi -lole32 -luuid
x86_64-w64-mingw32-g++ -O2 -Wall -Wextra -municode -static \
  -o probe64.exe tests/shared_fence_probe.cpp -ld3d11 -ldxgi -lole32 -luuid
```

Run `probe32.exe` under the tested Proton WoW64 build, passing the Windows
absolute path to `probe64.exe`. Both compiled without warnings. With the
original `win32u.so`, the probe exits **1** and the child reproduces the same
cross-process warning and access violation in `vkImportSemaphoreWin32HandleKHR`.
This is an expected failing regression test until the runtime bug is fixed.
The installed Proton files remain unchanged.

A later [isolated monotonic-timeline experiment](fence-experiment.md) passed
this small fence test but produced a black game image with TTWVR. It was
reverted and remains outside the normal launcher. DLSS is still not usable.

Both renderer experiments were launch-scoped;
the original TTWVR and installed Proton binaries remain unchanged. The proven
shader compiler and OpenXR bridge fixes stay configured for normal VR launches.

## Fallback rendering dimensions

The original DLL contains fallback defaults of **2944x2208**, independently of
the modern VR menu's resolution percentage. Adding `[Stereo] iRenderWidth=1920`
and `iRenderHeight=1440` to the external INI did not change the observed target
size in this build. That INI experiment was overlaid temporarily and not kept.

`tools/patch_fallback_resolution.py` changes the four constructor defaults for
width/height and their two built-in default strings to **1920x1440**, preserving
the 4:3 aspect ratio. It changes 11 bytes total, does not enable the broken
upscaler experiments, and refuses every other original DLL hash. The source
and output hashes are verified; it never overwrites an existing output.

| Item | Value |
|---|---|
| Output SHA-256 | `12c3edcaa07cef1bd8bcab3a9fefc7bad9b4bcc2e738a34d8043e8ad686f43e5` |
| Constructor immediate file offsets | `0x98ef5`, `0x98eff`, `0x98f09`, `0x98f13` |
| Built-in default string offsets | `0x84f598`, `0x84f5b0` |

Validation uses the user's simulated Monado headset and the same copied save.
The original DLL's loaded scene capture shows 15.4 FPS and `2944x2208` depth
targets. These figures must not be compared directly with physical WiVRn runs:
the simulated runtime's presentation and headset configuration differ.

The smaller-defaults DLL loaded the same save and rendered the room. The log
confirmed **1920x1440** depth targets; spectator captures showed **39.1 FPS**
and later **42.3 FPS**, versus the original's 15.4 FPS capture. These are scene
snapshots, not a timed benchmark: NPC animation and the view differ slightly.
This is a quality/performance tradeoff, not a fix for the original-resolution
performance problem. The user rejected reducing resolution as the solution.

The running process saw the expected patched hash and the host retained the
original DLL. All eight VR save-file hashes still matched the pre-test backup.
The smaller fallback overlay has been disabled and the external INI restored
to its original `Resolution=150%`. All further performance work must preserve
image quality. Neither renderer/upscaler experiment is enabled.

### Applying and rolling back the smaller fallback

```sh
python3 tools/patch_fallback_resolution.py /path/to/original/TTWVR.dll \
  /path/to/private-fallback/TTWVR.dll
```

In `ttwvr-linux.ini` beside MO2, add these keys to the existing `[VR]` section:

```ini
fallback_resolution_source=mods/Tale of Two Realities 1.1.0/NVSE/Plugins/TTWVR.dll
fallback_resolution_patch=../fallback-resolution/TTWVR.dll
```

Use the actual paths for the installation. Relative paths are resolved beside
MO2. The wrapper verifies the original and patched hashes on **every launch**,
then overlays the copy for VR only. Flat ignores these settings. An updated
original mod is rejected before launch instead of silently mixing versions.
Ten launcher tests pass, including unknown-mod rejection, incomplete settings,
and Flat isolation. These optional settings are **disabled locally**.

Rollback: remove **both** `fallback_resolution_*` keys. The installed mod DLL
needs no restoration. Keep the independent shader compiler/OpenXR bridge fixes.
The tradeoff is lower image sharpness. The modified DLL reports build ID
`368A0726`; multiplayer against the original build has not been tested (the mod
requires matching build IDs). Full Quest 3 image quality/performance validation
remains pending.

## Original-resolution allocation investigation

The original `2944x2208` fallback and `Resolution=150%` were restored before
profiling. In the simulated Vault 101 scene, a spectator capture showed 16.0
FPS. This remains a simulator observation, not a physical headset benchmark.

A six-second main-thread syscall trace recorded 540 `mmap` calls, consuming
about 0.638 seconds in the tracer, and 6,302 `ioctl` calls, about 1.516 seconds.
Tracing adds overhead, so these durations are diagnostic rather than a clean
frame-time measurement. A separate mapping trace repeatedly showed two sets
of 26,001,408-byte mappings: exactly `2944 * 2208 * 4` bytes per eye. Each set
includes a driver mapping and a second mapping into the 32-bit address space.
Two of six native main-thread stack samples reached `win32u_vkAllocateMemory`
through NVIDIA's driver; three reached Monado's `xrWaitFrame` pacing wait.

This supports investigating repeated full-eye allocations/mappings and pacing.
It does not establish that raw scene rendering or GPU shader throughput is the
limiting factor. The userspace-only sampling run cannot quantify kernel work,
and unwinding across the WoW64 boundary is incomplete.

DXVK's [texture implementation](https://github.com/doitsujin/dxvk/blob/master/src/d3d11/d3d11_texture.cpp)
selects direct mapping for eligible dynamic images, including video-like 4:3
textures. Its `d3d11.disableDirectImageMapping` option instead uses an upload
buffer path. This motivates a single-option experiment at the original
resolution; it does not change texture detail, shader quality or DLSS.

The single-option run confirmed `d3d11.disableDirectImageMapping=True` in
DXVK's effective configuration and retained `2944x2208` eye targets. A later
spectator snapshot showed 18.6 FPS, but a six-second main-thread trace still
recorded 516 `mmap` calls and 6,048 `ioctl` calls. It did not remove the repeated
allocation pattern or establish a meaningful performance fix. The run was
stopped; the option is not enabled for normal launches.
