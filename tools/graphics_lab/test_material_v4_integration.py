"""Palette command, wire and live-monitor boundaries; no game/GPU launch."""
from contextlib import nullcontext
from copy import deepcopy
from pathlib import Path
import json
import struct
import tempfile
import unittest
from unittest.mock import patch

from tools.graphics_lab import game,load_hook,qualification as q
from tools.supersampling import hook_probe
from tools.d3d11 import memory_guard


class PaletteIntegrationTests(unittest.TestCase):
    def config(self,variant=0,flags=4):
        raw=bytearray(struct.pack('<6I',4120,4,flags,3840,2160,variant)+bytes(4096))
        for offset,name in zip((24,1048,2072,3096),('a.ps.bin','b.ps.bin','c.ps.bin','effects.private.json')):
            text=('C:/case/'+name).encode('utf-16le');raw[offset:offset+len(text)]=text
        return bytes(raw)

    def test_explicit_variant_command_excludes_arbitrary_programs_and_flags(self):
        request=dict(operation='variant',sequence=3,variant=2,enabled=4)
        self.assertEqual(game.native_command(json.dumps(request).encode(),2,palette_allowed=True),request)
        self.assertIsNone(game.native_command(json.dumps(request).encode(),3,palette_allowed=True))
        for update in ({'variant':True},{'variant':3},{'enabled':True},{'enabled':1},{'path':'arbitrary.ps.bin'}):
            with self.subTest(update=update),self.assertRaises(ValueError):
                game.native_command(json.dumps({**request,**update}).encode(),0,palette_allowed=True)
        with self.assertRaises(ValueError):game.native_command(json.dumps(request).encode(),0,4)
        with self.assertRaises(ValueError):
            game.native_command(b'{"operation":"effects","sequence":4,"enabled":4}',0,4,palette_allowed=True)

    def test_palette_startup_is_isolated_before_any_game_files_are_read(self):
        valid=dict(hook=Path('not-opened.dll'),observer_catalog=Path('not-read.bin'),high_quality=True,
                   material_config_version=4,material_effects=('improved_vegetation',),leaf_palette=True)
        for update in ({'leaf_palette':False},{'leaf_palette':1},{'hook':None},{'observer_catalog':None},
                       {'high_quality':False},{'material_config_version':3},{'leaf_wrapped':True},
                       {'water_program':True},{'shadow_filter':True},{'ambient_occlusion':True},
                       {'comparison_uv':True},{'crash_watch':True},{'baseline_crash_watch':True},
                       {'material_effects':('improved_vegetation','improved_terrain')}):
            with self.subTest(update=update),patch.object(game.platform,'load')as load:
                with self.assertRaisesRegex(ValueError,'Leaf palette requires explicit'):
                    game.run(Path('not-read'),Path('not-created'),**{**valid,**update})
                load.assert_not_called()

    def test_v4_wire_never_uses_legacy_builds_or_headers(self):
        dll=Path('not-opened.dll');raw=self.config()
        with patch.object(load_hook,'verify_build',return_value=(dll,{'abi':hook_probe.BUILD_ABI_V3})):
            with self.assertRaisesRegex(ValueError,'material V4'):load_hook.invoke(None,dll,'effects',raw)
        with patch.object(load_hook,'verify_build',return_value=(dll,{'abi':hook_probe.BUILD_ABI_V4})),\
             patch.object(load_hook,'file_lease',return_value=nullcontext()),\
             patch.object(load_hook,'_invoke_pinned',return_value={'accepted':True})as invoke:
            self.assertEqual(load_hook.invoke(None,dll,'effects',raw),{'accepted':True})
            invoke.assert_called_once_with(None,dll,raw,b'WePlayNativeGraphicsConfigureEffects\0')
        for size,version in ((4120,1),(4120,3),(4132,4),(7204,4)):
            with self.subTest(size=size,version=version),patch.object(load_hook,'verify_build')as verify:
                with self.assertRaises(ValueError):
                    load_hook.invoke(None,dll,'effects',struct.pack('<2I',size,version)+raw[8:])
                verify.assert_not_called()

    def test_v4_record_checks_selection_and_all_four_paths(self):
        for variant in (0,1,2):
            value=q._material_configuration(self.config(variant))
            self.assertEqual((value['version'],value['selected_variant']),(4,variant))
        for raw in (self.config(3),self.config(flags=1),self.config()[:-1]):
            with self.assertRaises(ValueError):q._material_configuration(raw)
        for offset in (24,1048,2072,3096):
            raw=bytearray(self.config());raw[offset:offset+1024]=bytes(1024)
            with self.subTest(offset=offset),self.assertRaises(ValueError):q._material_configuration(bytes(raw))

    def test_exact_v10_schema_preserves_old_source_and_abi_contracts(self):
        build=dict(format='s7-native-graphics-build-v2',source_set='effects-v10',compiled_capabilities=15,
                   abi=deepcopy(hook_probe.BUILD_ABI_V4),diagnostic_abi={'comparison':12,'shadow':12,'ambient':12,'ambient_v2':12},
                   build_sha256='a'*64,files={})
        sources,_,valid=q._build_contract(build)
        self.assertTrue(valid);self.assertEqual(len(sources),40);self.assertEqual(sources,set(hook_probe.PALETTE_SOURCES))
        old=deepcopy(build);old['source_set']='effects-v9'
        self.assertFalse(q._build_contract(old)[2]);old['abi']=hook_probe.BUILD_ABI_V3
        self.assertTrue(q._build_contract(old)[2])


class MemoryPublicationTests(unittest.TestCase):
    def test_completed_snapshot_never_starts_a_scan_or_returns_mutable_state(self):
        class Probe:
            def sample(self):raise AssertionError('No additional scan permitted')
        sentinel=memory_guard.Sentinel(None,Path('not-written'),Probe())
        value=sentinel.snapshot();value['samples']=999
        self.assertEqual(sentinel.summary['samples'],0)

    def test_no_fresh_timestamp_for_a_stale_guardian_and_immediate_failure_publication(self):
        sentinel=memory_guard.Sentinel(None,Path('not-written'),None)
        sentinel.summary['samples']=1
        with tempfile.TemporaryDirectory()as directory:
            path=Path(directory)/'memory-live.private.json'
            publisher=game.LiveMemoryReport(path,123,'a'*64)
            with patch.object(game.time,'monotonic_ns',return_value=100):self.assertTrue(publisher.publish(sentinel))
            original=path.read_bytes()
            with patch.object(game.time,'monotonic_ns',return_value=999):self.assertFalse(publisher.publish(sentinel))
            self.assertEqual(path.read_bytes(),original)
            sentinel.summary['monitor_failed']=True
            with patch.object(game.time,'monotonic_ns',return_value=1000):self.assertTrue(publisher.publish(sentinel))
            value=json.loads(path.read_text())
            self.assertTrue(value['guardian']['monitor_failed']);self.assertEqual(value['owned_record_sha256'],'a'*64)
            self.assertEqual(value['pid'],123);self.assertEqual(value['sampled_monotonic_ns'],1000)


if __name__=='__main__':unittest.main()
