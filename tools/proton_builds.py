# SPDX-FileCopyrightText: 2026 Ethanfel and contributors
# SPDX-License-Identifier: GPL-3.0-only
"""Exact tested Proton builds; keep the Windows/Unix OpenXR pair together."""
import hashlib
from pathlib import Path

DISCORD_SOURCE = "https://discord.com/channels/1570233209462194208/1570481280825561098"
CACHYOS_ARCHIVE_SHA256 = "0e6f57024d5577fcb70bf973f0416a876e0a6215247de7c905be35e3761e3642"
# Discord source supplied by the user; no source commit/release provenance inferred.
BUILDS = [{'version': 'experimental-11.0-20261105c-x86_64',
  'openxr_builtin': False,
  'openxr_hashes': {'x86_64-unix/wineopenxr.so': '7ee56ba303d02f5e3715b20bdae288c1663e52bfe22262912fba75c8dfac25a5',
                    'i386-windows/wineopenxr.dll': 'dc4a20b398c4b601de5ef0fb041ff1b9ac9fc7b65e385c958ef73fb18d4b84f3',
                    'x86_64-windows/wineopenxr.dll': '44c611b2b46b971e6298b3894ebf4e13bf51cbc339468af2a3f79e7f70af155a'},
  'dxvk_hashes': {'d3d11.dll': '60a5274a29fcb4dcc1938e5ff9ca89be64bfcbdd45e5a9ed98a5279f3283d877',
                  'd3d9.dll': '905fc2bf411d9acced7b1c558f7798bb6a2338b81c7055425c3e5cf0e4b71d96',
                  'dxgi.dll': '2c430f5241fb7162154f01397e5a915db779c67adca76c92e5828b655d870f78'}},
 {'version': 'cachyos-11.0-20261015-slr',
  'openxr_builtin': True,
  'openxr_hashes': {'x86_64-unix/wineopenxr.so': 'df84ffb28339dc1737fc08158f9f22e0e94b6161cec892cc1bdfaba98c21cc13',
                    'i386-windows/wineopenxr.dll': 'feb6e5527f1ba79dfe71293679cd4af0404ebe729e36a807b56d5ea535633322',
                    'x86_64-windows/wineopenxr.dll': '661a8e96b08aa60e2c0abaab939331504234a560f1838545d6848f80e043206d'},
  'dxvk_hashes': {'d3d11.dll': '5d477458c5e0390450be1ee04ddd6f96b07e846abcba043a0434032c3e171464',
                  'd3d9.dll': 'a24ee55f5cef14e3faf91e05f30f9ea658b10bc571be560625bebbf670af7904',
                  'dxgi.dll': '9a38c6dc3933f856efc5744c6c57185b1233ae0dc44355831c7c989d271710e1'}}]


def identify_proton(root):
    """Reject rolling updates or mixed runtime files before creating overlays."""
    root = Path(root)
    version = (root / "version").read_text().strip().split(" ", 1)[-1]
    build = next((b for b in BUILDS if b["version"] == version), None)
    if build is None:
        raise ValueError(f"unsupported Proton version: {version}; use an exact tested build")
    files = {"files/lib/wine/" + n: h for n, h in build["openxr_hashes"].items()}
    files.update({"files/lib/wine/dxvk/i386-windows/" + n: h for n, h in build["dxvk_hashes"].items()})
    for name, expected in files.items():
        with (root / name).open("rb") as f:
            actual = hashlib.file_digest(f, "sha256").hexdigest()
        if actual != expected:
            raise ValueError(f"Proton original hash mismatch: {name}; check the Proton version and OpenXR pairing")
    return build
