"""Exercise the launcher without Wine, Steam, or a headset."""
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest


WRAPPER = Path(__file__).resolve().parents[1] / "tools" / "steam_mo2.py"


class LaunchWrapperTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="ttw wrapper ")
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.mo2 = self.root / "MO2 with spaces" / "ModOrganizer.exe"
        self.mo2.parent.mkdir()
        self.mo2.touch()
        self.runtime = self.root / "runtime.json"
        self.runtime.write_text(json.dumps({"runtime": {"library_path": "unused.so"}}))
        self.xrizer = self.root / "xrizer"
        (self.xrizer / "bin/linux64").mkdir(parents=True)
        (self.xrizer / "bin/linux64/vrclient.so").touch()
        self.state = self.root / "state"
        self.game = str(self.root / "Fallout New Vegas" / "FalloutNVLauncher.exe")
        self.proton = self.script("proton", """
import json, os, sys
print(json.dumps({'args': sys.argv[1:], 'xr': os.environ.get('XR_RUNTIME_JSON'),
                  'no_vr': os.environ.get('DXVK_NO_VR')}))
""")
        self.entry = self.script("_v2-entry-point", """
import os, sys
from pathlib import Path
Path(os.environ['TTW_TEST_IMPORT']).write_text(os.environ['XR_RUNTIME_JSON'])
command = sys.argv[sys.argv.index('--') + 1:]
os.execvpe(command[0], command, os.environ)
""")

    def script(self, name, source):
        path = self.root / name
        path.write_text(f"#!{sys.executable}\n" + source)
        path.chmod(0o700)
        return str(path)

    def launch(self, command, *options):
        env = os.environ.copy()
        env['TTW_TEST_IMPORT'] = str(self.root / "imported-runtime")
        return subprocess.run(
            [sys.executable, str(WRAPPER), '--mo2', str(self.mo2),
             '--state-dir', str(self.state), '--xrizer', str(self.xrizer),
             '--xr-runtime', str(self.runtime), *options, '--', *command],
            env=env, text=True, capture_output=True,
        )

    def test_container_imports_selection_before_clearing_host_override(self):
        literal = 'argument with spaces, $HOME and `literal`'
        result = self.launch([self.entry, '--verb=waitforexitandrun', '--',
                              self.proton, 'waitforexitandrun', self.game, literal])
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual((self.root / 'imported-runtime').read_text(), str(self.runtime))
        child = json.loads(result.stdout)
        self.assertIsNone(child['xr'])
        self.assertEqual(child['args'], ['waitforexitandrun', str(self.mo2), literal])

    def test_direct_proton_keeps_selected_runtime(self):
        result = self.launch([self.proton, 'waitforexitandrun', self.game])
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(json.loads(result.stdout)['xr'], str(self.runtime))

    def test_flat_profile_does_not_require_or_initialize_vr(self):
        (self.mo2.parent / 'ModOrganizer.ini').write_text(
            '[General]\nselected_profile=@ByteArray(TTW - Flat)\n')
        self.runtime.unlink()
        result = self.launch([self.proton, 'waitforexitandrun', self.game])
        self.assertEqual(result.returncode, 0, result.stderr)
        child = json.loads(result.stdout)
        self.assertIsNone(child['xr'])
        self.assertEqual(child['no_vr'], '1')
        registry = json.loads((self.state / 'openvrpaths.vrpath').read_text())
        self.assertEqual(registry['runtime'], [])

    def test_cli_profile_overrides_last_selected_profile(self):
        (self.mo2.parent / 'ModOrganizer.ini').write_text(
            '[General]\nselected_profile=@ByteArray(TTW - VR)\n')
        result = self.launch([self.proton, 'waitforexitandrun', self.game,
                              '-p', 'TTW - Flat'], '--dry-run')
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(json.loads(result.stdout)['mode'], 'flat')

    def test_dry_run_is_read_only_and_ambiguous_command_is_rejected(self):
        result = self.launch([self.proton, 'waitforexitandrun', self.game], '--dry-run')
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertFalse(self.state.exists())
        result = self.launch([self.proton, 'waitforexitandrun', self.game, self.game])
        self.assertNotEqual(result.returncode, 0)
        self.assertFalse(self.state.exists())


if __name__ == '__main__':
    unittest.main()
