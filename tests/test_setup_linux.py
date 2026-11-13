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
