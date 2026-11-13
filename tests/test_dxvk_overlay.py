# SPDX-FileCopyrightText: 2026 Ethanfel and contributors
# SPDX-License-Identifier: GPL-3.0-only
"""Check that renderer experiments cannot replace the real Windows directory."""
import hashlib
import json
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
import dxvk_overlay


class DxvkOverlayTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        root = Path(self.temp.name)
        self.proton = root / "Proton" / "proton"
        self.source = self.proton.parent / "files/lib/wine/dxvk/i386-windows"
        self.source.mkdir(parents=True)
        self.renderer = root / "candidate"
        self.renderer.mkdir()
        self.compat = root / "compatdata"
        self.destination = self.compat / "pfx/drive_c/windows/syswow64"
        self.destination.mkdir(parents=True)
        (self.destination / "kernel32.dll").touch()
        self.windows = root / "private syswow64"
        self.windows.mkdir()
        (self.windows / "kernel32.dll").touch()
        originals, candidates = {}, {}
        for name in dxvk_overlay.ORIGINAL_HASHES:
            (self.source / name).write_bytes(b"original")
            (self.renderer / name).write_bytes(b"candidate")
            originals[name] = hashlib.sha256(b"original").hexdigest()
            candidates[name] = hashlib.sha256(b"candidate").hexdigest()
        (self.renderer / "hashes.json").write_text(json.dumps(candidates))
        selector = patch.object(dxvk_overlay, "identify_proton", return_value={"dxvk_hashes": originals})
        selector.start()
        self.addCleanup(selector.stop)
        mock = patch.object(dxvk_overlay, "ORIGINAL_HASHES", originals)
        mock.start()
        self.addCleanup(mock.stop)

    def build(self, windows=None):
        return dxvk_overlay.private_dxvk_renderer(
            [str(self.proton), "waitforexitandrun", "game.exe"],
            self.renderer, windows or self.windows, str(self.compat))

    def test_namespace_uses_private_writable_directory_and_readonly_sources(self):
        command = self.build()
        self.assertIn(str(self.windows), command)
        self.assertIn(str(self.destination), command)
        self.assertEqual(command.count("--ro-bind"), 3)
        for name in dxvk_overlay.ORIGINAL_HASHES:
            self.assertEqual((self.source / name).read_bytes(), b"original")
        self.assertFalse((self.destination / "d3d9.dll").exists())

    def test_real_prefix_and_alias_are_rejected(self):
        with self.assertRaisesRegex(ValueError, "differ from the real prefix"):
            self.build(self.destination)
        alias = self.windows.parent / "prefix alias"
        alias.symlink_to(self.destination, target_is_directory=True)
        with self.assertRaisesRegex(ValueError, "differ from the real prefix"):
            self.build(alias)

    def test_tampered_candidate_and_changed_proton_are_rejected(self):
        (self.renderer / "d3d9.dll").write_bytes(b"changed")
        with self.assertRaisesRegex(ValueError, "candidate hash mismatch"):
            self.build()
        (self.renderer / "d3d9.dll").write_bytes(b"candidate")
        (self.source / "d3d11.dll").write_bytes(b"updated Proton")
        with self.assertRaisesRegex(ValueError, "original hash mismatch"):
            self.build()
