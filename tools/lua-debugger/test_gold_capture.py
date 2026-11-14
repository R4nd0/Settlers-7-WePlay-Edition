"""Gold observer guards and rejection before any attachment or launch."""
from pathlib import Path
import hashlib
import sys
import types
import tempfile
import unittest
from unittest.mock import patch
import gold_capture as capture
import gold_marker_test as staging


class CaptureTests(unittest.TestCase):
    def test_explicit_marker_list_is_bounded_and_unique(self):
        self.assertEqual(capture.markers(capture.TEST_MARKERS),list(capture.TEST_MARKERS))
        for value in ([],[None],[[]],[capture.PREFIX],['private text'],[capture.PREFIX+'a']*2,
                      [capture.PREFIX+'x'*129],[capture.PREFIX+'x\n']):
            with self.assertRaises(ValueError):capture.markers(value)

    def test_full_regions_are_required_and_relocations_are_retained(self):
        data=bytes(range(65));address=0x401000
        region=(address,len(data),hashlib.sha256(data).hexdigest())
        entry=types.SimpleNamespace(rva=address-0x400000+3,type=3)
        pe=types.SimpleNamespace(DIRECTORY_ENTRY_BASERELOC=[types.SimpleNamespace(entries=[entry])],
            get_data=lambda rva,size:data[rva-0x1000:rva-0x1000+size])
        with patch.object(capture,'REGIONS',(region,)):
            guard=capture.guards(pe)[0]
            self.assertEqual(guard['bytes'],data.hex());self.assertEqual(guard['relocations'][0]['offset'],3)
            for index in (0,32,64):
                changed=bytearray(data);changed[index]^=1
                pe.get_data=lambda rva,size,b=changed:bytes(b[rva-0x1000:rva-0x1000+size])
                with self.assertRaises(ValueError):capture.guards(pe)

    def test_capture_rejects_other_editions_before_attachment(self):
        selected=(Path('unused.exe'),[],dict(version='history-ubisoft'),{})
        with patch.object(capture.debugger,'validate_target',return_value=selected),patch.dict(sys.modules,frida=types.SimpleNamespace()):
            with self.assertRaisesRegex(ValueError,'reviewed Gold'):
                capture.GoldCaptureAdapter('unused.exe',1,'unused.jsonl',allowed=capture.TEST_MARKERS)

    def test_selftest_cannot_supply_an_arbitrary_fixture_or_markers(self):
        with patch.object(capture.debugger,'validate_target') as target:
            with self.assertRaises(ValueError):capture.GoldCaptureAdapter('unused',1,'unused',allowed=[capture.PREFIX+'other'],selftest=True)
            target.assert_not_called()

    def fixture(self,folder):
        root=Path(folder)/'Game';output=Path(folder)/'Output';output.mkdir()
        archive=root/'Data/Base/BBA/Interface.bba';archive.parent.mkdir(parents=True);archive.write_bytes(b'original')
        with patch.object(staging,'build',return_value=b'test fixture'):
            value=staging.ArchiveFixture(root,output)
        return value,archive

    def test_test_archive_restores_exact_original_bytes(self):
        with tempfile.TemporaryDirectory(dir=Path(__file__).resolve().parents[2]/'work') as folder:
            value,archive=self.fixture(folder);value.install();self.assertEqual(archive.read_bytes(),b'test fixture')
            self.assertTrue(value.restore());self.assertEqual(archive.read_bytes(),b'original')

    def test_newer_archive_is_retained_instead_of_overwritten(self):
        with tempfile.TemporaryDirectory(dir=Path(__file__).resolve().parents[2]/'work') as folder:
            value,archive=self.fixture(folder);value.install();archive.write_bytes(b'newer user change')
            with self.assertRaisesRegex(ValueError,'newer'):value.restore()
            self.assertEqual(archive.read_bytes(),b'newer user change');self.assertEqual(value.backup.read_bytes(),b'original')

    def test_changed_source_is_rejected_before_install(self):
        with tempfile.TemporaryDirectory(dir=Path(__file__).resolve().parents[2]/'work') as folder:
            value,archive=self.fixture(folder);archive.write_bytes(b'changed before start')
            with self.assertRaises(ValueError):value.install()
            self.assertEqual(archive.read_bytes(),b'changed before start')

    def test_unknown_archive_is_rejected(self):
        with self.assertRaises(ValueError):staging.build(b'unknown archive')

    def test_channel_selection_is_explicit_and_selftest_requires_both(self):
        for value in ([],['unknown'],['log','log']):
            with self.assertRaises(ValueError):capture.channels(value)
        with patch.object(capture.debugger,'validate_target') as target:
            with self.assertRaises(ValueError):capture.GoldCaptureAdapter('unused',1,'unused',allowed=capture.TEST_MARKERS,selftest=True,selected=['log'])
            target.assert_not_called()


if __name__=='__main__':unittest.main()
