# Apply the Linux fixes

[← Installation](installation.md#4-apply-the-linux-fixes)

For the supported Proton build only. **Close New Vegas and Fluorine first.**
Use Bash and run the sections in order, in the same terminal. Replace these
paths; `TTW_FIXES` should be a new directory outside the game and prefix:

```bash
set -euo pipefail
export TTW_REPO='/path/to/ttwvr-linux'
export TTW_PROTON='/path/to/steamapps/common/Proton - Experimental'
export TTW_PREFIX='/path/to/ttwvr/compat/pfx'
export TTW_FIXES='/path/to/ttwvr-fixes'
mkdir -p "$TTW_FIXES"
```

If a command fails, stop there and resolve it before continuing. These steps
prepare private files; the final INI enables them for the VR launcher.
`TTW_PREFIX` must be the prefix configured in Fluorine, which may differ from
Steam's original `compatdata/22380/pfx`.

## 1. Native shader compiler

Fixes the observed crash when loading a save. Download both DLLs from
[Mozilla's fxc2](https://github.com/mozilla/fxc2/tree/master/dll), verify their
recorded hashes, and back up the prefix originals before replacing them.

<details>
<summary>Show compiler download and install commands</summary>

```bash
curl -fL 'https://raw.githubusercontent.com/mozilla/fxc2/master/dll/d3dcompiler_47_32.dll' \
  -o "$TTW_FIXES/d3dcompiler_47_32.dll"
curl -fL 'https://raw.githubusercontent.com/mozilla/fxc2/master/dll/d3dcompiler_47.dll' \
  -o "$TTW_FIXES/d3dcompiler_47_64.dll"
printf '%s  %s\n' \
  '2ad0d4987fc4624566b190e747c9d95038443956ed816abfd1e2d389b5ec0851' "$TTW_FIXES/d3dcompiler_47_32.dll" \
  '4432bbd1a390874f3f0a503d45cc48d346abc3a8c0213c289f4b615bf0ee84f3' "$TTW_FIXES/d3dcompiler_47_64.dll" \
  | sha256sum -c -
mkdir "$TTW_FIXES/compiler-backup"
cp -L "$TTW_PREFIX/drive_c/windows/syswow64/d3dcompiler_47.dll" \
  "$TTW_FIXES/compiler-backup/d3dcompiler_47_32.dll"
cp -L "$TTW_PREFIX/drive_c/windows/system32/d3dcompiler_47.dll" \
  "$TTW_FIXES/compiler-backup/d3dcompiler_47_64.dll"
cp --remove-destination "$TTW_FIXES/d3dcompiler_47_32.dll" \
  "$TTW_PREFIX/drive_c/windows/syswow64/d3dcompiler_47.dll"
cp --remove-destination "$TTW_FIXES/d3dcompiler_47_64.dll" \
  "$TTW_PREFIX/drive_c/windows/system32/d3dcompiler_47.dll"
```

Both checksum checks must report `OK`. The backup directory intentionally
must not already exist. [Failure evidence and sources](simulated-vr.md).

</details>

## 2. OpenXR bridge

Fixes the headset image going black after loading gameplay:

```bash
python3 "$TTW_REPO/tools/patch_openxr_bridge.py" \
  "$TTW_PROTON/files/lib/wine/x86_64-unix/wineopenxr.so" \
  "$TTW_FIXES/openxr/wineopenxr.so"
```

The command creates a private patched copy and refuses unsupported originals
or existing outputs. [Patch scope and checksums](openxr-layers.md).

## 3. DXVK performance patch

Build the pinned DXVK revision with the larger eye-buffer pool, then prepare
its private Windows directory. This keeps the original rendering resolution.
Install the build prerequisites from [step 1](installation.md#1-get-the-requirements)
before proceeding.

<details>
<summary>Show DXVK build and preparation commands</summary>

```bash
git clone https://github.com/doitsujin/dxvk.git "$TTW_FIXES/dxvk-src"
git -C "$TTW_FIXES/dxvk-src" checkout --detach cf0a0d90f8965147741ad17dcd0b263817572ab3
git -C "$TTW_FIXES/dxvk-src" submodule update --init --recursive
git -C "$TTW_FIXES/dxvk-src" apply "$TTW_REPO/patches/dxvk-32bit-mapped-pool.patch"
meson setup "$TTW_FIXES/build32" "$TTW_FIXES/dxvk-src" \
  --cross-file "$TTW_FIXES/dxvk-src/build-win32.txt" \
  --buildtype release -Denable_d3d8=false -Denable_d3d10=false -Dbuild_id=true
ninja -C "$TTW_FIXES/build32"

python3 - <<'PYCODE'
import hashlib
import json
import os
from pathlib import Path
import shutil

fixes = Path(os.environ['TTW_FIXES'])
prefix = Path(os.environ['TTW_PREFIX'])
renderer = fixes / 'dxvk-pool32'
renderer.mkdir()
hashes = {}
for name in ('d3d9', 'd3d11', 'dxgi'):
    source = fixes / 'build32/src' / name / (name + '.dll')
    target = renderer / source.name
    shutil.copy2(source, target)
    hashes[target.name] = hashlib.sha256(target.read_bytes()).hexdigest()
(renderer / 'hashes.json').write_text(json.dumps(hashes, indent=2) + '\n')
shutil.copytree(prefix / 'drive_c/windows/syswow64', fixes / 'syswow64', symlinks=True)
PYCODE
```

This copies the whole `syswow64` directory after installing the compiler.
Keep this as a separate copy: do not substitute a symlink or hard-linked files.
The local `hashes.json` checks your built files' integrity.

[Build requirements upstream](https://github.com/doitsujin/dxvk/blob/cf0a0d90f8965147741ad17dcd0b263817572ab3/README.md#build-instructions)
· [Why this improves performance](dxvk-pools.md).

</details>

## Enable the fixes

Create **`instance/ttwvr-linux.ini`** beside the native instance's
`ModOrganizer.ini`, using the actual absolute path chosen for `TTW_FIXES`.
This is the directory passed to the wrapper's `--native-instance`, not the
Fluorine application directory. If the file already exists, update its `[VR]`
section. Shell variables such as `$TTW_FIXES` do not expand inside the INI.

```ini
[VR]
wow64=true
native_d3dcompiler47=true
openxr_bridge=/path/to/ttwvr-fixes/openxr/wineopenxr.so
dxvk_renderer=/path/to/ttwvr-fixes/dxvk-pool32
dxvk_syswow64=/path/to/ttwvr-fixes/syswow64
```

Leave reduced-resolution overlays and experimental DLSS/fence patches disabled.
The wrapper supplies the tested DXVK staging-buffer setting automatically.

Continue with **[launching in VR](installation.md#5-launch-in-vr)**.
For rollback, see [Undo a fix](troubleshooting.md#undo-a-fix).
