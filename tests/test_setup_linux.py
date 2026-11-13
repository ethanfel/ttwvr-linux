# SPDX-FileCopyrightText: 2026 Ethanfel and contributors
# SPDX-License-Identifier: GPL-3.0-only
"""Installation safety: real filesystem rollback, partial failures, INI preservation."""
import json
from pathlib import Path
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1]/'tools'))
from setup_linux import Transaction, rollback, edit_ini, patched_modlist, read_config


class SetupTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix='TTW Linux test ')
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)

    def test_rollback_restores_symlink_without_touching_proton_and_preserves_saves(self):
        vendor = self.root/'Proton original.dll'; vendor.write_bytes(b'vendor')
        target = self.root/'prefix compiler.dll'; target.symlink_to(vendor)
        config = self.root/'ModOrganizer.ini'; config.write_bytes(b'old config\r\n')
        save = self.root/'Save.fos'; save.write_bytes(b'valuable save')
        new = self.root/'compatibility.ini'
        tx = Transaction(self.root/'fixes/transaction.json')
        tx.replace(target,b'native'); tx.replace(config,b'patched'); tx.replace(new,b'new')
        tx.finish()
        self.assertFalse(target.is_symlink())
        self.assertEqual(vendor.read_bytes(),b'vendor')
        rollback(tx.path)
        self.assertTrue(target.is_symlink())
        self.assertEqual(config.read_bytes(),b'old config\r\n')
        self.assertFalse(new.exists())
        self.assertEqual(save.read_bytes(),b'valuable save')
        rollback(tx.path)  # repeat is harmless

    def test_conflict_rejects_entire_rollback_then_force_preserves_current_edits(self):
        first = self.root/'first'; first.write_bytes(b'original1')
        second = self.root/'second'; second.write_bytes(b'original2')
        tx = Transaction(self.root/'fixes/transaction.json')
        tx.replace(first,b'patched1'); tx.replace(second,b'patched2'); tx.finish()
        first.write_bytes(b'user edits')
        with self.assertRaisesRegex(ValueError,'files changed'):
            rollback(tx.path)
        self.assertEqual(second.read_bytes(),b'patched2')
        rollback(tx.path,force=True)
        self.assertEqual(first.read_bytes(),b'original1')
        backup = next(tx.path.parent.glob('before-forced-rollback-*'))
        self.assertEqual((backup/'0').read_bytes(),b'user edits')

    def test_write_ahead_journal_recovers_interruption_before_replace(self):
        original = self.root/'config'; original.write_bytes(b'unchanged')
        tx = Transaction(self.root/'fixes/transaction.json')
        tx.replace(original,b'new')
        original.write_bytes(b'unchanged')  # model crash after journal, before rename
        rollback(tx.path)
        self.assertEqual(original.read_bytes(),b'unchanged')

    def test_corrupted_backup_does_not_partially_restore(self):
        path = self.root/'settings'; path.write_bytes(b'old')
        tx = Transaction(self.root/'fixes/transaction.json'); tx.replace(path,b'new')
        (tx.path.parent/'backups/0').write_bytes(b'corruption')
        with self.assertRaisesRegex(ValueError,'backup changed'): rollback(tx.path)
        self.assertEqual(path.read_bytes(),b'new')

    def test_qt_ini_preserves_geometry_comments_and_newlines(self):
        original = b'; comment\r\n[Geometry]\r\nstate=@ByteArray(\\x1\\0\\xff)\r\n[customExecutables]\r\n1\\title=My Game\r\n1\\wrapperOptions=old\r\n'
        result = edit_ini(original,{'customExecutables':{'1\\wrapperOptions':'new'}})
        self.assertEqual(result,original.replace(b'wrapperOptions=old',b'wrapperOptions=new'))

    def test_new_mod_wins_priority_and_disables_renamed_addon_only_in_vr(self):
        mods = self.root/'mods'; mods.mkdir()
        for name, dll in [('old VR','TTWVR.dll'),('renamed addon','TTWVRRenderingAddon.dll')]:
            p=mods/name/'NVSE/Plugins'; p.mkdir(parents=True); (p/dll).touch()
        data=b'# MO2\r\n+renamed addon\r\n+old VR\r\n+TTW\r\n'
        result=patched_modlist(data,{'instance':self.root,'new_mod':mods/'TTWVR Linux 1.1.4'})
        self.assertEqual(result,b'# MO2\r\n+TTWVR Linux 1.1.4\r\n-renamed addon\r\n-old VR\r\n+TTW\r\n')

    def test_unsafe_profile_cannot_escape_instance(self):
        c={k:str(self.root/k) for k in ('instance','mod','manager','proton','prefix','steam_runtime','config_home','data_home','fixes')}
        c['profile']='../elsewhere'; path=self.root/'setup.json'; path.write_text(json.dumps(c))
        with self.assertRaisesRegex(ValueError,'single directory'): read_config(path)


class ConfigTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix='TTW config with spaces ')
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name).resolve()
        self.instance = self.root/'instance'
        self.instance.mkdir()
        self.proton = self.root/'apps/Custom Proton'
        self.prefix = self.root/'compat-cachyos/pfx'
        (self.instance/'ModOrganizer.ini').write_text(
            f'[fluorine]\nproton_path="{self.proton}"\nprefix_path="{self.prefix}"\n')
        self.profile = self.instance/'profiles/TTW - VR'
        self.profile.mkdir(parents=True)
        self.add_mod('original VR')
        self.add_mod('disabled VR')
        (self.profile/'modlist.txt').write_text('+original VR\n-disabled VR\n')
        self.config = {
            'workspace': str(self.root),
            'manager': 'apps/fluorine-manager/fluorine-manager',
            'steam_library': str(self.root/'Steam library'),
        }

    def add_mod(self, name):
        dll = self.instance/'mods'/name/'NVSE/Plugins/TTWVR.dll'
        dll.parent.mkdir(parents=True)
        dll.write_bytes(b'original mod fixture')
        return dll.parent.parent.parent

    def read(self, config=None):
        path = self.root/'setup.json'
        path.write_text(json.dumps(self.config if config is None else config))
        return read_config(path)

    def test_three_fields_find_native_settings_and_only_enabled_mod_without_writes(self):
        path = self.root/'setup.json'
        path.write_text(json.dumps(self.config))
        before = {p.relative_to(self.root): p.read_bytes() for p in self.root.rglob('*') if p.is_file()}
        c = read_config(path)
        for key, folder in [('instance','instance'), ('config_home','config'),
                            ('data_home','share'), ('fixes','linux-fixes')]:
            self.assertEqual(c[key], self.root/folder)
        self.assertEqual(c['manager'], self.root/self.config['manager'])
        self.assertEqual(c['mod'], self.instance/'mods/original VR')
        self.assertEqual(c['proton'], self.proton)
        self.assertEqual(c['prefix'], self.prefix)
        self.assertEqual(c['steam_runtime'], self.root/'Steam library/steamapps/common/SteamLinuxRuntime_4')
        self.assertEqual(c['xr_runtime'], Path('/usr/share/openxr/1/openxr_wivrn.json'))
        self.assertEqual(before, {p.relative_to(self.root): p.read_bytes() for p in self.root.rglob('*') if p.is_file()})
        self.assertFalse(c['fixes'].exists())

    def test_explicit_paths_override_inference_and_defaults(self):
        self.config.update({
            'proton': 'different-proton', 'prefix': 'different-prefix/pfx',
            'mod': 'instance/mods/disabled VR', 'fixes': 'different-fixes',
            'config_home': 'different-config', 'data_home': 'different-data',
            'steam_runtime': 'different-runtime',
        })
        (self.instance/'ModOrganizer.ini').unlink()
        c = self.read()
        for key in ('proton','prefix','mod','fixes','config_home','data_home','steam_runtime'):
            self.assertEqual(c[key], self.root/self.config[key])

    def test_legacy_absolute_config_needs_no_discovery(self):
        config = {key: str(self.root/'unprepared'/key) for key in
                  ('instance','mod','manager','proton','prefix','steam_runtime','config_home','data_home','fixes')}
        c = self.read(config)
        for key, value in config.items():
            self.assertEqual(c[key], Path(value))
        self.assertFalse((self.root/'unprepared').exists())

    def test_ambiguous_or_disabled_mods_are_not_guessed(self):
        for listing in ('+original VR\n+disabled VR\n', '-original VR\n-disabled VR\n'):
            with self.subTest(listing=listing):
                (self.profile/'modlist.txt').write_text(listing)
                with self.assertRaisesRegex(ValueError, 'enable exactly one original TTWVR mod'):
                    self.read()

    def test_mod_discovery_rejects_parent_traversal(self):
        (self.profile/'modlist.txt').write_text('+../outside\n')
        with self.assertRaisesRegex(ValueError, 'invalid enabled mod name'):
            self.read()

    def test_missing_native_proton_settings_explain_preparation(self):
        (self.instance/'ModOrganizer.ini').write_text('[fluorine]\n')
        with self.assertRaisesRegex(ValueError, 'select proton_path in the Fluorine instance'):
            self.read()

    def test_relative_paths_need_workspace(self):
        c = {key: str(self.root/key) for key in
             ('instance','mod','manager','proton','prefix','steam_runtime','config_home','data_home','fixes')}
        c['manager'] = 'apps/fluorine-manager'
        with self.assertRaisesRegex(ValueError, 'set workspace for relative paths'):
            self.read(c)

    def test_tilde_workspace_and_custom_instance(self):
        self.config.update({'workspace':'~/Games/ttwvr', 'instance':str(self.instance)})
        c = self.read()
        self.assertEqual(c['manager'], (Path.home()/'Games/ttwvr/apps/fluorine-manager/fluorine-manager').resolve())
        self.assertEqual(c['mod'], self.instance/'mods/original VR')

    def test_blank_profile_is_rejected(self):
        self.config['profile'] = ''
        with self.assertRaisesRegex(ValueError, 'single directory'):
            self.read()
