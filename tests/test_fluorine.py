"""Native launcher command safety and real unprivileged save-mount regression."""
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
LAUNCHER = ROOT / "tools/launch_fluorine.py"
SHIM = ROOT / "tools/fluorine-bin/unshare"


class FluorineTests(unittest.TestCase):
    def test_launcher_preserves_profile_and_does_not_execute_steam_tail(self):
        with tempfile.TemporaryDirectory(prefix="fluorine launch ") as tmp:
            base = Path(tmp)
            instance = base / "instance"; instance.mkdir()
            (instance / "ModOrganizer.ini").touch()
            manager = base / "fake manager"
            manager.write_text("#!/usr/bin/python3\nimport os,sys,json\n"
                               "print(json.dumps({'args':sys.argv[1:], 'config':os.environ['XDG_CONFIG_HOME'], 'data':os.environ['XDG_DATA_HOME'], 'path':os.environ['PATH']}))\n")
            manager.chmod(0o755)
            marker = base / "must not execute"
            args = [sys.executable, str(LAUNCHER), "--manager", str(manager),
                    "--instance", str(instance), "--config-home", str(base / "config"),
                    "--data-home", str(base / "share"), "--profile", "TTW - Flat"]
            for only_manager in (False, True):
                r = subprocess.run(args + (["--manager-only"] if only_manager else []) +
                                   ["--", "/usr/bin/touch", str(marker)], text=True, capture_output=True)
                self.assertEqual(r.returncode, 0, r.stderr)
                result = json.loads(r.stdout)
                expected = ["--instance", str(instance), "-p", "TTW - Flat"]
                if not only_manager: expected += ["run", "-e", "Tale of Two Wastelands"]
                self.assertEqual(result["args"], expected)
                self.assertEqual(result["config"], str(base / "config"))
                self.assertEqual(result["data"], str(base / "share"))
                self.assertEqual(result["path"].split(os.pathsep)[0], str(SHIM.parent))
                self.assertFalse(marker.exists())

    @unittest.skipUnless(os.environ.get("TTWVR_TEST_USERNS") == "1", "opt-in real mount integration")
    def test_profile_save_mount_preserves_user_and_writes_through(self):
        self.assertNotEqual(os.getuid(), 0, "run this integration test as the desktop user")
        with tempfile.TemporaryDirectory(prefix="fluorine mount ") as tmp:
            source, target = Path(tmp) / "source", Path(tmp) / "target"
            source.mkdir(); target.mkdir()
            (source / "existing save").write_text("original")
            script = ('import os,pathlib,sys; p=pathlib.Path(sys.argv[1]); '
                      'assert os.getuid()==int(sys.argv[2]); '
                      'assert (p/"existing save").read_text()=="original"; '
                      '(p/"new save").write_text("new")')
            r = subprocess.run([str(SHIM), "--user", "--mount", "-r", "--", "/bin/sh", "-c",
                                'mount --bind "$1" "$2" && shift 2 && exec "$@"', "_mo2bind",
                                str(source), str(target), "/usr/bin/bwrap", "--bind", "/", "/", "--", sys.executable, "-c", script,
                                str(target), str(os.getuid())], text=True, capture_output=True)
            self.assertEqual(r.returncode, 0, r.stderr)
            self.assertEqual((source / "new save").read_text(), "new")
            self.assertEqual(list(target.iterdir()), [], "mount must not leak into host namespace")
            # An unrelated unshare call must retain its requested root mapping.
            r = subprocess.run([str(SHIM), "--user", "--map-root-user", "--", "/usr/bin/id", "-u"],
                               text=True, capture_output=True)
            self.assertEqual(r.returncode, 0, r.stderr)
            self.assertEqual(r.stdout.strip(), "0")
