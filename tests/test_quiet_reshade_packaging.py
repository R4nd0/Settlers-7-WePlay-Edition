"""Gold's separate preparation must receive the shared no-overlay runtime."""
import hashlib
from pathlib import Path
import sys
import tempfile
from types import SimpleNamespace
import unittest
from unittest.mock import Mock,patch

sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools/installer'))
import gold_support
import reshade_runtime


class QuietGoldPreparationTests(unittest.TestCase):
    def test_verified_bundle_reaches_new_gold_copy_and_ownership_tracker(self):
        with tempfile.TemporaryDirectory() as temp:
            root=Path(temp);package=root/'Package';target=root/'Game'
            payload=package/gold_support.FOLDER/reshade_runtime.RUNTIME_BUNDLE
            payload.parent.mkdir(parents=True);payload.write_bytes(b'qualified no-GUI fixture')
            pin=hashlib.sha256(payload.read_bytes()).hexdigest()
            tracker=Mock();native=SimpleNamespace(prepare_copy=Mock(return_value={'verified':True}))
            with patch.object(gold_support,'verify_package'),patch.object(gold_support,'module',return_value=native), \
                    patch.object(reshade_runtime,'SETUP_SHA256',pin):
                self.assertEqual(gold_support.prepare(package,root/'Original',target,tracker=tracker),{'verified':True})
            destination=target/reshade_runtime.RUNTIME_BUNDLE
            self.assertEqual(destination.read_bytes(),payload.read_bytes())
            tracker.assert_called_once_with(destination,pin)
            native.prepare_copy.assert_called_once()

    def test_foreign_bundle_rejected_before_native_preparation(self):
        with tempfile.TemporaryDirectory() as temp:
            root=Path(temp);package=root/'Package';target=root/'Game'
            payload=package/gold_support.FOLDER/reshade_runtime.RUNTIME_BUNDLE
            payload.parent.mkdir(parents=True);payload.write_bytes(b'foreign runtime')
            native=Mock()
            with patch.object(gold_support,'verify_package'),patch.object(gold_support,'module',return_value=native):
                with self.assertRaisesRegex(ValueError,'fingerprint mismatch'):
                    gold_support.prepare(package,root/'Original',target)
            native.prepare_copy.assert_not_called();self.assertFalse(target.exists())


if __name__=='__main__':unittest.main()
