"""Authored catalog/content fixtures; approval is patched only inside tests."""
from dataclasses import FrozenInstanceError
import copy
import hashlib
import json
from pathlib import Path
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
            for index, name in enumerate((*content.REQUIRED[edition], 'Base/BBA/reviewed-gameplay.bba')):
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

    def test_approved_catalog_does_not_grant_old_adapter_a_mixed_capability(self):
        catalog = self.catalog()
        with patch.object(content, 'APPROVED_DIGESTS', frozenset((catalog.digest.hex(),))):
            with self.assertRaisesRegex(ValueError, 'adapter_unavailable'): catalog.verify(self.roots[1], 1)

    def test_changed_or_missing_owned_gameplay_input_is_rejected(self):
        catalog = self.catalog(); self.approve(catalog)
        path = self.roots[1] / 'Base/BBA/reviewed-gameplay.bba'
        path.write_bytes(b'foreign gameplay')
        with self.assertRaisesRegex(ValueError, 'content_changed'): catalog.verify(self.roots[1], 1)
        path.unlink()
        with self.assertRaisesRegex(ValueError, 'content_changed'): catalog.verify(self.roots[1], 1)

    def test_catalog_cannot_verify_another_editions_root_or_invalid_role(self):
        catalog = self.catalog(); self.approve(catalog)
        with self.assertRaises(ValueError): catalog.verify(self.roots[2], 1)
        for edition in (True, 0, 4, '1'):
            with self.assertRaisesRegex(ValueError, 'edition_mismatch'): catalog.verify(self.roots[1], edition)

    def test_linked_inputs_never_escape_the_own_profile(self):
        catalog = self.catalog(); self.approve(catalog)
        path = self.roots[1] / 'Base/BBA/reviewed-gameplay.bba'
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
