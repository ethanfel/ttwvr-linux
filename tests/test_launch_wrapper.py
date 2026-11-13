# SPDX-FileCopyrightText: 2026 Ethanfel and contributors
# SPDX-License-Identifier: GPL-3.0-only
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
                  'no_vr': os.environ.get('DXVK_NO_VR'),
                  'wow64': os.environ.get('PROTON_USE_WOW64'),
                  'overrides': os.environ.get('WINEDLLOVERRIDES'),
                  'neural': os.environ.get('ENABLE_TTWVR_NEURAL_RENDERING'),
                  'nvapi': os.environ.get('PROTON_ENABLE_NVAPI'),
                  'hide_nvidia': os.environ.get('PROTON_HIDE_NVIDIA_GPU')}))
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

    def launch(self, command, *options, environment=None, native=False):
        env = os.environ.copy()
        env.pop('PROTON_USE_WOW64', None)
        env.pop('WINEDLLOVERRIDES', None)
        env.update(environment or {})
        env['TTW_TEST_IMPORT'] = str(self.root / "imported-runtime")
        return subprocess.run(
            [sys.executable, str(WRAPPER),
             *(['--native-instance', str(self.mo2.parent)] if native
               else ['--mo2', str(self.mo2)]),
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

    def test_instance_compatibility_is_vr_only_and_preserves_other_overrides(self):
        (self.mo2.parent / 'ttwvr-linux.ini').write_text(
            '[VR]\nwow64=true\nnative_d3dcompiler47=true\n')
        inherited = 'd3d9=n;d3dcompiler_47,d3dx9_43=b;mscoree='
        for mode in ('vr', 'flat'):
            with self.subTest(mode=mode):
                result = self.launch([self.proton, 'waitforexitandrun', self.game],
                                     '--mode', mode,
                                     environment={'WINEDLLOVERRIDES': inherited})
                self.assertEqual(result.returncode, 0, result.stderr)
                child = json.loads(result.stdout)
                self.assertEqual(child['wow64'], '1' if mode == 'vr' else None)
                self.assertEqual(child['overrides'],
                                 'd3d9=n;d3dx9_43=b;mscoree=;d3dcompiler_47=n'
                                 if mode == 'vr' else inherited)

    def test_neural_opt_in_is_vr_only_and_default_off(self):
        for configured, mode in ((False, 'vr'), (True, 'vr'), (True, 'flat')):
            with self.subTest(configured=configured, mode=mode):
                (self.mo2.parent / 'ttwvr-linux.ini').write_text(
                    '[VR]\nneural_rendering=' + str(configured).lower() + '\n')
                result = self.launch([self.proton, 'waitforexitandrun', self.game],
                                     '--mode', mode,
                                     environment={'ENABLE_TTWVR_NEURAL_RENDERING': 'inherited',
                                                  'PROTON_ENABLE_NVAPI': '0',
                                                  'PROTON_HIDE_NVIDIA_GPU': '1'})
                self.assertEqual(result.returncode, 0, result.stderr)
                child = json.loads(result.stdout)
                enabled = configured and mode == 'vr'
                self.assertEqual(child['neural'], '1' if enabled else None)
                self.assertEqual(child['nvapi'], '1' if enabled else '0')
                self.assertEqual(child['hide_nvidia'], '0' if enabled else '1')

    def test_author_path_disables_archived_layer_and_enables_nvapi(self):
        (self.mo2.parent / 'ttwvr-linux.ini').write_text(
            '[VR]\nauthor_renderer=true\nneural_rendering=true\n')
        result = self.launch([self.proton, 'waitforexitandrun', self.game],
                             '--mode', 'vr', '--dry-run',
                             environment={'ENABLE_TTWVR_NEURAL_RENDERING': '1',
                                          'PROTON_ENABLE_NVAPI': '0'})
        self.assertEqual(result.returncode, 0, result.stderr)
        env = json.loads(result.stdout)['environment']
        self.assertIsNone(env['ENABLE_TTWVR_NEURAL_RENDERING'])
        self.assertEqual(env['DISABLE_TTWVR_NEURAL_RENDERING'], '1')
        self.assertEqual(env['PROTON_FSR4_UPGRADE'], '0')
        self.assertEqual(env['PROTON_DLSS_UPGRADE'], '0')
        self.assertEqual(env['PROTON_USE_OPTISCALER'], '0')
        self.assertEqual(env['PROTON_ENABLE_NVAPI'], '1')
        self.assertEqual(env['PROTON_HIDE_NVIDIA_GPU'], '0')

    def test_native_compiler_flag_is_vr_only(self):
        for mode in ('vr', 'flat'):
            result = self.launch([self.proton, 'waitforexitandrun', self.game],
                                 '--mode', mode, '--native-d3dcompiler47')
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertEqual(json.loads(result.stdout)['overrides'],
                             'd3dcompiler_47=n' if mode == 'vr' else None)

    def test_fallback_rejects_changed_mod_and_flat_ignores_setting(self):
        original = self.mo2.parent / 'TTWVR.dll'
        original.write_bytes(b'changed mod version')
        patched = self.mo2.parent / 'fallback.dll'
        patched.touch()
        (self.mo2.parent / 'ttwvr-linux.ini').write_text(
            '[VR]\nfallback_resolution_source=TTWVR.dll\n'
            'fallback_resolution_patch=fallback.dll\n')
        result = self.launch([self.proton, 'waitforexitandrun', self.game], '--mode', 'vr')
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('TTWVR fallback hash mismatch', result.stderr)
        self.assertFalse(self.state.exists())
        result = self.launch([self.proton, 'waitforexitandrun', self.game], '--mode', 'flat')
        self.assertEqual(result.returncode, 0, result.stderr)

    def test_incomplete_fallback_configuration_rejected_in_vr(self):
        (self.mo2.parent / 'ttwvr-linux.ini').write_text(
            '[VR]\nfallback_resolution_patch=fallback.dll\n')
        result = self.launch([self.proton, 'waitforexitandrun', self.game], '--mode', 'vr')
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('set both fallback_resolution', result.stderr)
        self.assertFalse(self.state.exists())

    def test_incomplete_dxvk_configuration_is_rejected_and_flat_ignores_it(self):
        (self.mo2.parent / 'ttwvr-linux.ini').write_text(
            '[VR]\ndxvk_renderer=private-renderer\n')
        result = self.launch([self.proton, 'waitforexitandrun', self.game], '--mode', 'vr')
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('set both dxvk_renderer and dxvk_syswow64', result.stderr)
        self.assertFalse(self.state.exists())
        result = self.launch([self.proton, 'waitforexitandrun', self.game], '--mode', 'flat')
        self.assertEqual(result.returncode, 0, result.stderr)

    def test_bridge_rejects_changed_proton_and_flat_ignores_bridge(self):
        original = self.root / 'files/lib/wine/x86_64-unix/wineopenxr.so'
        original.parent.mkdir(parents=True)
        original.write_bytes(b'a different Proton build')
        (self.mo2.parent / 'ttwvr-linux.ini').write_text(
            '[VR]\nopenxr_bridge=missing-private-bridge.so\n')
        result = self.launch([self.proton, 'waitforexitandrun', self.game],
                             '--mode', 'vr')
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('OpenXR bridge hash mismatch', result.stderr)
        self.assertFalse(self.state.exists())
        self.assertEqual(original.read_bytes(), b'a different Proton build')
        result = self.launch([self.proton, 'waitforexitandrun', self.game],
                             '--mode', 'flat')
        self.assertEqual(result.returncode, 0, result.stderr)


    def test_native_instance_preserves_game_and_arguments(self):
        (self.mo2.parent / 'ModOrganizer.ini').write_text(
            '[General]\nselected_profile=@ByteArray(TTW - VR)\n')
        literal = 'spaces, $HOME and `literal`'
        result = self.launch([self.proton, 'waitforexitandrun', self.game, literal], native=True)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(json.loads(result.stdout)['args'], ['waitforexitandrun', self.game, literal])

    def test_native_flat_profile_keeps_vr_disabled(self):
        (self.mo2.parent / 'ModOrganizer.ini').write_text(
            '[General]\nselected_profile=@ByteArray(TTW - Flat)\n')
        self.runtime.unlink()
        result = self.launch([self.proton, 'waitforexitandrun', self.game], native=True)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(json.loads(result.stdout)['no_vr'], '1')

    def test_fluorine_slr_env_preserves_runtime_import_order(self):
        (self.mo2.parent / 'ModOrganizer.ini').write_text('[General]\n')
        run = self.root / 'SteamLinuxRuntime_4/run'
        run.parent.mkdir()
        run.write_bytes(Path(self.entry).read_bytes())
        run.chmod(0o700)
        result = self.launch([str(run), '--filesystem=/a path', '--', '/usr/bin/env',
                              'SteamAppId=22380', self.proton, 'waitforexitandrun', self.game],
                             native=True)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual((self.root / 'imported-runtime').read_text(), str(self.runtime))
        child = json.loads(result.stdout)
        self.assertIsNone(child['xr'])
        self.assertEqual(child['args'], ['waitforexitandrun', self.game])

    def test_native_rejects_ambiguous_launch_before_writing(self):
        (self.mo2.parent / 'ModOrganizer.ini').write_text('[General]\n')
        result = self.launch([self.proton, 'waitforexitandrun', self.game, self.game], native=True)
        self.assertNotEqual(result.returncode, 0)
        self.assertFalse(self.state.exists())


    def test_explicit_steam_runtime_wraps_native_proton_once(self):
        (self.mo2.parent / 'ModOrganizer.ini').write_text('[General]\n')
        runtime = self.root / 'SteamLinuxRuntime_4'
        runtime.mkdir()
        entry = runtime / '_v2-entry-point'
        entry.write_bytes(Path(self.entry).read_bytes())
        entry.chmod(0o700)
        for wrapped in (False, True):
            command = [self.proton, 'waitforexitandrun', self.game]
            if wrapped:
                command = [str(entry), '--verb=waitforexitandrun', '--', *command]
            result = self.launch(command, '--steam-runtime', str(runtime), native=True)
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertIsNone(json.loads(result.stdout)['xr'])
            self.assertEqual((self.root / 'imported-runtime').read_text(), str(self.runtime))


if __name__ == '__main__':
    unittest.main()
