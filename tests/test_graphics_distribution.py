"""Packaged profile activation refuses stale copies and changed payloads."""
from pathlib import Path
import sys
import unittest
from unittest.mock import patch

sys.path[:0] = [str(Path(__file__).resolve().parents[1] / 'tools/installer'), str(Path(__file__).parent)]
import graphics_distribution as distribution
import graphics_profile as graphics
import compatibility as content
import install
import test_graphics_profile as fixtures


class DistributionTests(unittest.TestCase):
    snapshot = fixtures.GraphicsProfileTests.snapshot

    def setUp(self):
        fixtures.GraphicsProfileTests.setUp(self)
        self.package = self.parent / 'package'
        def write(path,value):
            path.parent.mkdir(parents=True,exist_ok=True); path.write_bytes(value)
        self.write=write
        write(self.package / 'Payload/Graphics/d3d9.dll',self.dll)
        write(self.package / 'Payload/Graphics/LICENSE',b'fixture license')
        write(self.package / 'Runtime/fixture',b'fixture runtime')
        helpers=[*install.GRAPHICS_TOOLS,*install.DISPLAY_TOOLS,
                 ('offline-ui/launch_local.py','launch_local.py'),
                 ('installer/launch_lan.py','launch_lan.py'),
                 ('installer/launch_auto_lan.py','launch_auto_lan.py')]
        for original,target in helpers:
            value=('# fixture '+original).encode()
            write(self.package / 'Tools' / original,value)
            write(self.root / 'Launcher' / target,value)
        (self.root / graphics.COMPAT).write_bytes(graphics.encoded(content.create(self.root)))
        self.before=self.snapshot()
        self.manifest()
        mock=patch.object(distribution,'LICENSE_SHA256',graphics._hash(b'fixture license'))
        mock.start(); self.addCleanup(mock.stop)

    def manifest(self):
        (self.package / 'package.json').write_bytes(graphics.encoded({
            'format':install.FORMAT,'capabilities':{'local_test':True,'lan_host':False,'lan_join':False},
            'graphics_choice':True,'display_selection':True,
            'files':content.inventory(self.package,('Payload','Runtime','Tools'))}))

    def test_install_restore_and_repeat_use_the_existing_transaction_guards(self):
        self.assertEqual(distribution.apply(self.package,self.root,'install'),'enabled')
        self.assertEqual(distribution.apply(self.package,self.root,'install'),'already-enabled')
        self.assertEqual(content.verify(self.root)['graphics_profile'],graphics.PROFILE)
        self.assertEqual(distribution.apply(self.package,self.root,'restore'),'disabled')
        self.assertEqual(self.snapshot(),self.before)
        content.verify(self.root)

    def test_changed_payload_or_license_never_mutates_copy(self):
        for relative in ('Payload/Graphics/d3d9.dll','Payload/Graphics/LICENSE'):
            path=self.package / relative; original=path.read_bytes(); path.write_bytes(b'changed')
            self.manifest()  # Even an updated inventory cannot bypass pinned upstream hashes.
            with self.assertRaises(ValueError): distribution.apply(self.package,self.root,'install')
            self.assertEqual(self.snapshot(),self.before)
            path.write_bytes(original)
        self.manifest()

    def test_stale_launcher_and_unknown_action_are_rejected_before_profile_changes(self):
        path=self.root / 'Launcher/graphics_choice.py'; path.write_bytes(b'old launcher')
        before=self.snapshot()
        with self.assertRaisesRegex(ValueError,'neue getrennte Testkopie'):
            distribution.apply(self.package,self.root,'install')
        self.assertEqual(self.snapshot(),before)
        with self.assertRaises(ValueError): distribution.apply(self.package,self.root,'other')
        self.assertEqual(self.snapshot(),before)


if __name__ == '__main__': unittest.main()
