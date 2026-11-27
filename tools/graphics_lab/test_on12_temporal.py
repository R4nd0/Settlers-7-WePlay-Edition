"""Temporal admission checks; never starts graphics or a game."""
from contextlib import redirect_stdout
import io
import json
import struct
from pathlib import Path
from tempfile import TemporaryDirectory
import unittest
from unittest.mock import patch

from . import on12_temporal as temporal


class Tests(unittest.TestCase):
    def setUp(self):
        self.temporary=TemporaryDirectory(dir=temporal.ROOT/'work')
        self.addCleanup(self.temporary.cleanup)
        self.case=Path(self.temporary.name)
        self.build=self.case/'build'
        self.build.mkdir()
        for name in temporal.SOURCES:
            (self.build/name).write_bytes((temporal.ROOT/'native'/name).read_bytes())
        for name in temporal.ARTIFACTS:
            (self.build/name).write_bytes(b'never execute this synthetic artifact')
        self.cpu=dict(passed=True,cpu_only=True,gpu_device_created=False,abi_bytes=64,
                      shader_compiled=True,sharp_edge_preserved=True,reactive_rejection=True)
        self.write_cpu()
        self.manifest=dict(format='s7-on12-temporal-build-v1',architecture='x86',cpu_only=True,
                           parameter_bytes=64,build_sha256=temporal.loader.digest(temporal.ROOT/'native/on12_temporal_build.ps1'),
                           files={name:temporal.loader.digest(self.build/name) for name in (*temporal.SOURCES,*temporal.ARTIFACTS)})
        self.write_manifest()

    def write_cpu(self):
        (self.build/'cpu-result.private.json').write_text(json.dumps(self.cpu))

    def write_manifest(self):
        (self.build/temporal.MANIFEST).write_text(json.dumps(self.manifest))

    def test_default_cli_only_verifies(self):
        with patch('sys.argv',['temporal','--build',str(self.build)]), patch.object(temporal.subprocess,'run') as run:
            output=io.StringIO()
            with redirect_stdout(output):temporal.main()
            result=json.loads(output.getvalue())
            self.assertFalse(result['gpu_launched'])
            self.assertFalse(result['game_taa_enabled'])
            run.assert_not_called()

    def test_tampered_artifact_and_source_rejected(self):
        for name in ('on12_temporal_gpu.exe','on12_temporal.hlsl'):
            with self.subTest(name=name):
                original=(self.build/name).read_bytes()
                (self.build/name).write_bytes(b'changed')
                with self.assertRaises(ValueError):temporal.verify_build(self.build)
                (self.build/name).write_bytes(original)

    def test_stale_builder_and_unknown_schema_rejected(self):
        original=self.manifest.copy()
        for change in ({'build_sha256':'0'*64},{'format':'unknown'},{'architecture':'arm64'},{'cpu_only':False},{'extra':True}):
            with self.subTest(change=change):
                self.manifest=original|change
                self.write_manifest()
                with self.assertRaises(ValueError):temporal.verify_build(self.build)

    def test_cpu_proofs_required_even_with_matching_hash(self):
        for key in ('passed','cpu_only','gpu_device_created','shader_compiled','sharp_edge_preserved','reactive_rejection'):
            with self.subTest(key=key):
                original=self.cpu[key]
                self.cpu[key]=not original
                self.write_cpu()
                self.manifest['files']['cpu-result.private.json']=temporal.loader.digest(self.build/'cpu-result.private.json')
                self.write_manifest()
                with self.assertRaises(ValueError):temporal.verify_build(self.build)
                self.cpu[key]=original

    def test_game_blocks_explicit_gpu_before_copy_or_launch(self):
        output=self.case/'gpu'
        with patch.object(temporal.recover,'_no_games',side_effect=RuntimeError('KI owns GPU')), patch.object(temporal.subprocess,'run') as run:
            with self.assertRaisesRegex(RuntimeError,'KI owns GPU'):temporal.gpu(self.build,output)
            self.assertFalse(output.exists())
            run.assert_not_called()

    def test_second_process_check_blocks_race_before_gpu_launch(self):
        with patch.object(temporal.recover,'_no_games',side_effect=[None,RuntimeError('game appeared')]), patch.object(temporal.subprocess,'run') as run:
            with self.assertRaisesRegex(RuntimeError,'game appeared'):temporal.gpu(self.build,self.case/'gpu')
            run.assert_not_called()

    def test_output_must_be_fresh_and_separate(self):
        for output in (self.build,self.build/'child',self.case):
            with self.subTest(output=output), patch.object(temporal.subprocess,'run') as run:
                with self.assertRaises(ValueError):temporal.gpu(self.build,output)
                run.assert_not_called()

    def input_host(self,**changes):
        value=dict(format='s7-on12-spatial-live-v1',initialized=True,scene_only=True,stopped=False,failure=0)
        value.update(changes)
        (self.case/'on12-spatial-live.private.json').write_text(json.dumps(value))

    def test_input_request_has_no_gpu_launch_or_taa_enable(self):
        self.input_host()
        with patch.object(temporal.subprocess,'run') as run:
            temporal.request_inputs(self.case,1,2)
            self.assertEqual((self.case/'on12-temporal-inputs.private.command').read_bytes(),struct.pack('<4I',16,1,1,2))
            with self.assertRaises(ValueError):temporal.request_inputs(self.case,1,4)
            temporal.request_inputs(self.case,2,4)
            run.assert_not_called()

    def test_input_request_requires_scene_host_and_unfinished_case(self):
        for change in ({'initialized':False},{'scene_only':False},{'stopped':True},{'failure':1},{'format':'unknown'}):
            with self.subTest(change=change):
                self.input_host(**change)
                with self.assertRaises(ValueError):temporal.request_inputs(self.case,1)
                self.assertFalse((self.case/'on12-temporal-inputs.private.command').exists())
        self.input_host()
        (self.case/'result.private.json').write_text('{}')
        with self.assertRaises(ValueError):temporal.request_inputs(self.case,1)

    def test_input_request_limits_and_explicit_integer_fields(self):
        for sequence,frames in ((True,2),(0,2),(0x100000000,2),(1,True),(1,0),(1,5)):
            with self.subTest(sequence=sequence,frames=frames), self.assertRaises(ValueError):
                temporal.request_inputs(self.case,sequence,frames)


if __name__=='__main__':unittest.main()
