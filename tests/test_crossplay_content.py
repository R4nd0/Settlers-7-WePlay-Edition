"""Authored catalog/content fixtures; approval is patched only inside tests."""
from dataclasses import FrozenInstanceError
import copy
import hashlib
import json
import os
from pathlib import Path
from types import SimpleNamespace
import sys
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools/installer'))
import crossplay_content as content
import crossplay_session as session


def sha(raw): return hashlib.sha256(raw).hexdigest()
def encode(value): return json.dumps(value, ensure_ascii=True, sort_keys=True, separators=(',', ':')).encode('utf-8')


class CrossplayContentTests(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory(dir=ROOT / 'work')
        self.addCleanup(temporary.cleanup); self.root = Path(temporary.name)
        self.data = {'format': content.FORMAT, 'native_protocol': 1, 'neo_features': 19, 'editions': []}
        self.roots, self.exe_pins, self.adapter_pins = {}, {}, {}
        for edition in (1, 2, 3):
            root = self.root / str(edition); self.roots[edition] = root
            files = []
            gameplay=('Data/' if edition==1 else '')+'Base/BBA/reviewed-gameplay.bba'
            for index, name in enumerate((*content.REQUIRED[edition], gameplay)):
                raw = f'authored edition {edition} file {index}'.encode('ascii')
                path = root / name; path.parent.mkdir(parents=True, exist_ok=True); path.write_bytes(raw)
                files.append({'id': name, 'sha256': sha(raw)})
                if index == 0: self.exe_pins[sha(raw)] = edition
                if index == 1: self.adapter_pins[edition] = (sha(raw),)
            self.data['editions'].append({'edition': edition, 'files': sorted(files, key=lambda row: row['id'])})
        guard = patch.dict(session.EXECUTABLE_EDITIONS, self.exe_pins)
        guard.start(); self.addCleanup(guard.stop)

    def catalog(self): return content.Catalog(encode(self.data))

    def approve(self, catalog):
        for guard in (patch.object(content, 'APPROVED_DIGESTS', frozenset((catalog.digest.hex(),))),
                      patch.dict(session.ADAPTER_PINS, self.adapter_pins)):
            guard.start(); self.addCleanup(guard.stop)

    def test_one_canonical_digest_binds_all_three_profiles_and_original_inputs(self):
        catalog = self.catalog(); self.approve(catalog)
        self.assertEqual(catalog.digest.hex(), sha(encode(self.data)))
        for edition in (1, 2, 3):
            self.assertEqual(catalog.verify(self.roots[edition], edition), catalog.digest)
        with self.assertRaises(FrozenInstanceError): catalog.digest = bytes(32)
        self.assertIsInstance(catalog.profiles[0], tuple)

    def test_a_complete_self_generated_catalog_has_no_release_approval(self):
        catalog = self.catalog()
        with self.assertRaisesRegex(ValueError, 'catalog_unapproved'): catalog.verify(self.roots[1], 1)

    def test_package_flag_requires_approved_catalog_and_both_shipped_adapters(self):
        catalog=self.catalog();path=self.root/content.PACKAGE_CATALOG
        path.parent.mkdir(parents=True);path.write_bytes(catalog.canonical)
        manifest={name:True for name in ('platform_independent','internet_easytier','gold_standalone',
            'steam_history','windows_helpers','game_directory','neo_logistics','multiplayer_resume')}
        manifest['files']={'payload/gold/payload/weplay_gold_startup.dll':
            dict(catalog.profiles[0])[content.REQUIRED[1][1]],
            'payload/uplay_r1_loader.dll':dict(catalog.profiles[1])[content.REQUIRED[2][1]]}
        with self.assertRaisesRegex(ValueError,'catalog_unapproved'):content.package_catalog(self.root,manifest)
        self.approve(catalog)
        # The two fixture History DLLs intentionally differ; one shipped DLL
        # must satisfy both original edition profiles before package admission.
        with self.assertRaisesRegex(ValueError,'adapter_unavailable'):content.package_catalog(self.root,manifest)
        manifest['internet_easytier']=False
        with self.assertRaisesRegex(ValueError,'package_invalid'):content.package_catalog(self.root,manifest)

    def test_one_admitted_shipped_history_adapter_covers_both_original_executables(self):
        adapter2=self.roots[2]/content.REQUIRED[2][1]
        adapter3=self.roots[3]/content.REQUIRED[3][1]
        adapter3.write_bytes(adapter2.read_bytes());digest=sha(adapter2.read_bytes())
        for row in self.data['editions'][2]['files']:
            if row['id']==content.REQUIRED[3][1]:row['sha256']=digest
        self.adapter_pins[3]=(digest,)
        catalog=self.catalog();self.approve(catalog)
        path=self.root/content.PACKAGE_CATALOG;path.parent.mkdir(parents=True);path.write_bytes(catalog.canonical)
        manifest={name:True for name in ('platform_independent','internet_easytier','gold_standalone',
            'steam_history','windows_helpers','game_directory','neo_logistics','multiplayer_resume')}
        manifest['files']={'payload/gold/payload/weplay_gold_startup.dll':
            dict(catalog.profiles[0])[content.REQUIRED[1][1]],'payload/uplay_r1_loader.dll':digest}
        self.assertEqual(content.package_catalog(self.root,manifest),catalog)
        for edition in (1,2,3):self.assertEqual(catalog.verify(self.roots[edition],edition),catalog.digest)

    def test_approved_catalog_does_not_grant_old_adapter_a_mixed_capability(self):
        catalog = self.catalog()
        with patch.object(content, 'APPROVED_DIGESTS', frozenset((catalog.digest.hex(),))):
            with self.assertRaisesRegex(ValueError, 'adapter_unavailable'): catalog.verify(self.roots[1], 1)

    def test_changed_or_missing_owned_gameplay_input_is_rejected(self):
        catalog = self.catalog(); self.approve(catalog)
        path = self.roots[1] / 'Data/Base/BBA/reviewed-gameplay.bba'
        path.write_bytes(b'foreign gameplay')
        with self.assertRaisesRegex(ValueError, 'content_changed'): catalog.verify(self.roots[1], 1)
        path.unlink()
        with self.assertRaisesRegex(ValueError, 'content_changed'): catalog.verify(self.roots[1], 1)

    def test_extra_game_files_are_rejected_before_hashing_with_no_suffix_exemptions(self):
        catalog=self.catalog();self.approve(catalog)
        for edition in (1,2,3):
            folder=self.roots[edition]/content.GAME_SCOPE[edition]/'Overrides'
            folder.mkdir()
            for name in ('loose.lua','asset.bba','runtime.log','crash.dmp','renderer.dxvk-cache'):
                path=folder/name;path.write_bytes(b'unlisted file, never hash')
                with self.subTest(edition=edition,name=name),patch.object(content.compatibility,'digest') as digest:
                    with self.assertRaisesRegex(ValueError,'content_changed'):catalog.verify(self.roots[edition],edition)
                    digest.assert_not_called()
                path.unlink()

    def test_explicitly_listed_suffix_file_is_hashed_and_cannot_change(self):
        for edition in (1,2,3):
            name=content.GAME_SCOPE[edition]+'/static.log'
            path=self.roots[edition]/name;path.write_bytes(b'reviewed immutable input')
            rows=self.data['editions'][edition-1]['files']
            rows.append({'id':name,'sha256':sha(path.read_bytes())});rows.sort(key=lambda row:row['id'])
        catalog=self.catalog();self.approve(catalog)
        for edition in (1,2,3):
            catalog.verify(self.roots[edition],edition)
            (self.roots[edition]/content.GAME_SCOPE[edition]/'static.log').write_bytes(b'changed suffix file')
            with self.assertRaisesRegex(ValueError,'content_changed'):catalog.verify(self.roots[edition],edition)

    def test_file_added_during_hashing_is_detected_by_the_final_set_check(self):
        catalog=self.catalog();self.approve(catalog)
        original=content.compatibility.digest
        added=self.roots[1]/'Data/late-override.lua'
        def hash_and_add(path):
            digest=original(path)
            if not added.exists():added.write_bytes(b'late addition')
            return digest
        with patch.object(content.compatibility,'digest',side_effect=hash_and_add):
            with self.assertRaisesRegex(ValueError,'content_changed'):catalog.verify(self.roots[1],1)

    def test_file_modified_while_hashing_cannot_pass_with_the_previous_digest(self):
        catalog=self.catalog();self.approve(catalog)
        original=content.compatibility.digest
        def hash_and_touch(path):
            digest=original(path);info=path.stat()
            os.utime(path,ns=(info.st_atime_ns,info.st_mtime_ns+10000000))
            return digest
        with patch.object(content.compatibility,'digest',side_effect=hash_and_touch):
            with self.assertRaisesRegex(ValueError,'content_changed'):catalog.verify(self.roots[1],1)

    def test_private_directory_is_not_traversed_and_external_profile_stays_outside_assets(self):
        catalog=self.catalog();self.approve(catalog)
        private=self.roots[2]/'Base/Accounts';private.mkdir()
        outside=self.roots[1]/'LocalData/Profiles';outside.mkdir(parents=True)
        (outside/'fixture.json').write_bytes(b'private fixture')
        original=Path.iterdir
        def guarded(path):
            if path==private or path==outside:raise AssertionError('Private directory traversed')
            return original(path)
        with patch.object(Path,'iterdir',guarded):
            catalog.verify(self.roots[1],1)
            with self.assertRaisesRegex(ValueError,'content_changed'):catalog.verify(self.roots[2],2)

    def test_non_junction_reparse_attributes_are_rejected_before_traversal(self):
        catalog=self.catalog();self.approve(catalog)
        folder=self.roots[2]/'Base/Bin';original=Path.lstat
        def reparse(path):
            info=original(path)
            return SimpleNamespace(st_mode=info.st_mode,st_file_attributes=0x400) if path==folder else info
        with patch.object(Path,'lstat',reparse):
            with self.assertRaisesRegex(ValueError,'content_changed'):catalog.verify(self.roots[2],2)

    def test_catalog_cannot_declare_other_scopes_as_game_assets(self):
        for edition,name in ((1,'Base/unused.bba'),(2,'Launcher/extra.dll'),(3,'Runtime/extra.dll')):
            changed=copy.deepcopy(self.data);rows=changed['editions'][edition-1]['files']
            rows.append({'id':name,'sha256':'a'*64});rows.sort(key=lambda row:row['id'])
            with self.subTest(edition=edition),self.assertRaisesRegex(ValueError,'catalog_invalid'):
                content.Catalog(encode(changed))
    def test_catalog_cannot_verify_another_editions_root_or_invalid_role(self):
        catalog = self.catalog(); self.approve(catalog)
        with self.assertRaises(ValueError): catalog.verify(self.roots[2], 1)
        for edition in (True, 0, 4, '1'):
            with self.assertRaisesRegex(ValueError, 'edition_mismatch'): catalog.verify(self.roots[1], edition)

    def test_linked_inputs_never_escape_the_own_profile(self):
        catalog = self.catalog(); self.approve(catalog)
        path = self.roots[1] / 'Data/Base/BBA/reviewed-gameplay.bba'
        other = self.root / 'outside.bba'; other.write_bytes(path.read_bytes())
        path.unlink(); path.symlink_to(other)
        with self.assertRaises(ValueError): catalog.verify(self.roots[1], 1)

    def test_exact_schema_versions_types_and_complete_profile_order_are_required(self):
        changes = []
        for key, values in (('format', ('unknown', 1)), ('native_protocol', (True, '1', 2)),
                            ('neo_features', (True, 31, '19')), ('editions', ([], {}, None))):
            for value in values:
                changed = copy.deepcopy(self.data); changed[key] = value; changes.append(changed)
        changed = copy.deepcopy(self.data); changed['extra'] = 1; changes.append(changed)
        changed = copy.deepcopy(self.data); changed['editions'].reverse(); changes.append(changed)
        for field in ('edition', 'files'):
            changed = copy.deepcopy(self.data); del changed['editions'][0][field]; changes.append(changed)
        for changed in changes:
            with self.subTest(keys=list(changed)), self.assertRaisesRegex(ValueError, 'catalog_invalid'):
                content.Catalog(encode(changed))

    def test_paths_hashes_order_duplicates_and_private_inputs_are_rejected(self):
        bad_paths = ('../secret', '/absolute', 'C:/private', 'Base\\foreign', 'Base//file',
                     'Base/./file', 'Base/../file', 'Base/file.', 'Base/file ', 'Base/CON.txt',
                     'LocalData/file', 'Base/Profiles/Options.ini', 'Base/Savegames/world',
                     'Base/tab\tfile', 'Base/Ü', '', 'x' * 241)
        for name in bad_paths:
            changed = copy.deepcopy(self.data)
            changed['editions'][0]['files'].append({'id': name, 'sha256': '1' * 64})
            changed['editions'][0]['files'].sort(key=lambda row: row['id'])
            with self.subTest(path=name), self.assertRaisesRegex(ValueError, 'catalog_invalid'):
                content.Catalog(encode(changed))
        for digest in ('0' * 64, 'a' * 63, 'A' * 64, None, 123):
            changed = copy.deepcopy(self.data); changed['editions'][0]['files'][0]['sha256'] = digest
            with self.assertRaisesRegex(ValueError, 'catalog_invalid'): content.Catalog(encode(changed))
        for variant in ('duplicate', 'case', 'order', 'extra'):
            changed = copy.deepcopy(self.data); rows = changed['editions'][0]['files']
            if variant == 'duplicate': rows.insert(0, dict(rows[0]))
            elif variant == 'case': rows.insert(1, {**rows[0], 'id': rows[0]['id'].lower()}); rows.sort(key=lambda row: row['id'])
            elif variant == 'order': rows.reverse()
            else: rows[0]['ignored'] = True
            with self.assertRaisesRegex(ValueError, 'catalog_invalid'): content.Catalog(encode(changed))

    def test_executable_adapter_and_known_executable_identity_are_mandatory(self):
        for edition in (1, 2, 3):
            for name in content.REQUIRED[edition]:
                changed = copy.deepcopy(self.data)
                changed['editions'][edition - 1]['files'] = [row for row in changed['editions'][edition - 1]['files'] if row['id'] != name]
                with self.assertRaisesRegex(ValueError, 'catalog_invalid'): content.Catalog(encode(changed))
            changed = copy.deepcopy(self.data)
            next(row for row in changed['editions'][edition - 1]['files'] if row['id'] == content.REQUIRED[edition][0])['sha256'] = 'f' * 64
            with self.assertRaisesRegex(ValueError, 'catalog_invalid'): content.Catalog(encode(changed))

    def test_bounded_reader_and_canonical_encoding_reject_ambiguous_json(self):
        canonical = encode(self.data); path = self.root / 'catalog.json'; path.write_bytes(canonical)
        self.assertEqual(content.read(path).digest, self.catalog().digest)
        with patch.object(content, 'MAX_BYTES', len(canonical) - 1):
            with self.assertRaises(ValueError): content.read(path)
        with patch.object(content, 'MAX_FILES', 2):
            with self.assertRaises(ValueError): content.Catalog(canonical)
        for raw in (b'', b'{}', b'[]', b'\xff', b'\xef\xbb\xbf' + canonical,
                    canonical + b'\n', json.dumps(self.data, indent=2).encode(),
                    canonical.replace(b'"neo_features":19', b'"neo_features":19,"neo_features":19')):
            with self.assertRaisesRegex(ValueError, 'catalog_invalid'): content.Catalog(raw)


if __name__ == '__main__': unittest.main()
