# SPDX-FileCopyrightText: 2026 Ethanfel and contributors
# SPDX-License-Identifier: GPL-3.0-only
"""Reject mixed Proton components and skip the obsolete bridge patch safely."""
import copy
import hashlib
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch
sys.path.insert(0, str(Path(__file__).resolve().parents[1]/"tools"))
import proton_builds
import setup_linux

class ProtonBuildTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.profiles = copy.deepcopy(proton_builds.BUILDS)
        self.roots = []
        for i, profile in enumerate(self.profiles):
            root = self.root/str(i); root.mkdir(); self.roots.append(root)
            (root/"version").write_text("123 " + profile["version"] + "\n")
            for key, directory in (("openxr_hashes", "files/lib/wine"), ("dxvk_hashes", "files/lib/wine/dxvk/i386-windows")):
                for name in profile[key]:
                    data = (str(i) + name).encode(); p = root/directory/name
                    p.parent.mkdir(parents=True, exist_ok=True); p.write_bytes(data)
                    profile[key][name] = hashlib.sha256(data).hexdigest()
        mock = patch.object(proton_builds, "BUILDS", self.profiles)
        mock.start(); self.addCleanup(mock.stop)

    def test_both_complete_builds_are_recognized(self):
        for root, profile in zip(self.roots, self.profiles):
            self.assertEqual(proton_builds.identify_proton(root), profile)

    def test_same_version_with_a_mixed_windows_bridge_is_rejected(self):
        relative = "files/lib/wine/i386-windows/wineopenxr.dll"
        (self.roots[1]/relative).write_bytes((self.roots[0]/relative).read_bytes())
        with self.assertRaisesRegex(ValueError, "OpenXR pairing"):
            proton_builds.identify_proton(self.roots[1])

    def test_changed_dxvk_is_rejected(self):
        (self.roots[1]/"files/lib/wine/dxvk/i386-windows/d3d11.dll").write_bytes(b"updated")
        with self.assertRaisesRegex(ValueError, "original hash mismatch"):
            proton_builds.identify_proton(self.roots[1])

    def test_unknown_version_is_not_accepted_by_directory_name(self):
        (self.roots[1]/"version").write_text("123 future-build")
        with self.assertRaisesRegex(ValueError, "unsupported Proton version"):
            proton_builds.identify_proton(self.roots[1])

    def test_builtin_fix_does_not_create_or_patch_a_bridge(self):
        fixes = self.root/"fixes"
        with patch.object(setup_linux, "patch_bytes", side_effect=AssertionError("must not patch")):
            self.assertIsNone(setup_linux.prepare_openxr(self.roots[1], fixes))
        self.assertFalse(fixes.exists())

    def test_experimental_keeps_its_private_workaround(self):
        source = self.roots[0]/"files/lib/wine/x86_64-unix/wineopenxr.so"
        original = source.read_bytes()
        (self.root/"fixes").mkdir()
        with patch.object(setup_linux, "patch_bytes", return_value=b"patched"):
            bridge = setup_linux.prepare_openxr(self.roots[0], self.root/"fixes")
        self.assertEqual(bridge.read_bytes(), b"patched")
        self.assertEqual(source.read_bytes(), original)
