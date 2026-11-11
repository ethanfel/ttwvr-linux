# Fixing the headset going black after loading a save

On 2026-11-11 the user confirmed that TTWVR's menu was visible on a Quest 3
through WiVRn 26.9, but gameplay appeared briefly and then went black. The
runtime rejected hundreds of frames with `XR_ERROR_POSE_INVALID`, identifying
a corrupted quaternion in composition layer 1. The desktop spectator continued
to render, so the earlier simulated spectator check had not established
successful headset submission during gameplay.

The installed Proton build is `experimental-11.0-20261105c-x86_64`. Its native
OpenXR bridge has the allocation bug described in
[ValveSoftware/Proton#10275](https://github.com/ValveSoftware/Proton/issues/10275).
The 32-bit and 64-bit conversion functions choose the allocation size for every
composition layer using the first layer's type. A projection takes 48 bytes,
but a later quad requires 112. Subsequent allocations overwrite that quad.
The generated source inspected on `experimental_11.0` at commit
`0764d2920a128cee74555afcab0819919ddf416d` still contains this pattern.

## Workaround and scope

`tools/patch_openxr_bridge.py` creates a separate copy of `wineopenxr.so`. It
changes two immediate bytes: the projection-first allocation size becomes 120
instead of 48 in each conversion function. 120 is sufficient for the layer
types supported by this specific bridge build. No instructions are inserted,
and no validation checks or pose values are bypassed.

This is a narrow workaround for projection-first arrays, including TTWVR's
observed projection/quad sequence. It is **not a general fix for every ordering
of heterogeneous layers**. An upstream source fix should calculate each
element's size independently inside the loop. The script refuses all other
input hashes, including newer Proton builds.

| Item | Value |
|---|---|
| Original SHA-256 | `7ee56ba303d02f5e3715b20bdae288c1663e52bfe22262912fba75c8dfac25a5` |
| Patched SHA-256 | `03127b4519cfa35e544b72c0356b7850ce257a842a1107812a756a6dbf2be0b5` |
| Instruction offsets | `0x4e4d4`, `0x52945` |
| Original instruction | `c7 45 cc 30 00 00 00` |
| Patched instruction | `c7 45 cc 78 00 00 00` |
| Total differing bytes | 2 |

The wrapper uses bubblewrap to overlay that one file **inside this launch's
mount namespace**. The installed Proton file remains unchanged and other games
retain the original bridge. `/dev` is passed through with `--dev-bind` so the
nested Steam runtime can access its devices. The wrapper verifies both hashes
before launching; a Steam Proton update requires re-evaluating the patch.

## Applying and rolling back

```sh
proton_dir='/path/to/Proton - Experimental'
python3 tools/patch_openxr_bridge.py \
  "$proton_dir/files/lib/wine/x86_64-unix/wineopenxr.so" \
  /path/to/private-openxr/wineopenxr.so
```

Add this under `[VR]` in the `ttwvr-linux.ini` beside `ModOrganizer.exe`:

```ini
openxr_bridge=/path/to/private-openxr/wineopenxr.so
```

Paths in this setting may also be relative to MO2's directory. The explicit
`--openxr-bridge` option is available too. Flat mode ignores the setting.
The [native shader compiler workaround](simulated-vr.md) is also required for
the tested installation.

Rollback: exit the game and remove the `openxr_bridge` setting or command-line
option. No restoration of Steam's Proton files is necessary. The private
patched file may be kept or deleted after the game exits.

## Verification

`tests/openxr_layer_conversion.c` calls the actual bridge's conversion functions
with a projection followed by two quads. It does not initialize a headset.
Against the original binary, the first quad's position and dimensions are
corrupted in both 32-bit and 64-bit conversions. Against the patched binary,
both quads preserve their complete poses and dimensions in both conversions.

```sh
cc -O2 -Wall -Wextra -rdynamic -o /tmp/ttw-xr-layers \
  tests/openxr_layer_conversion.c -ldl
env LD_LIBRARY_PATH="$proton_dir/files/lib/wine/x86_64-unix" \
  /tmp/ttw-xr-layers /path/to/private-openxr/wineopenxr.so
```

This native regression test requires OpenXR headers/loader and must only be
used with the exact supported original or patched binary: its internal symbol
addresses are specific to that build. The original binary returns 1 with two
`CORRUPTED` results; the patched copy returns 0 with four `PASS` results.

The live Quest 3 retest loaded the patched hash inside the game's namespace,
while a host-side checksum still matched the original. The copied save loaded
with zero invalid-pose errors, and the user confirmed that gameplay remained
visible in the headset. Performance was initially poor and then improved; it
is still being investigated. DLSS fallback and blood-depth-copy warnings remain.
