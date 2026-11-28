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
        (self.build/'camera-result.private.json').write_text(json.dumps(dict(passed=True,cpu_only=True,gpu_device_created=False,camera_bytes=96)))
        (self.build/'jitter-result.private.json').write_text(json.dumps(dict(passed=True,cpu_only=True,gpu_device_created=False,bone_palette_preserved=True)))
        (self.build/'native-jitter-result.private.json').write_text(json.dumps(dict(passed=True,cpu_only=True,gpu_device_created=False,control_version=2,control_bytes=40,query_bytes=96)))
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

    def test_depth_pixels_require_explicit_v2_request(self):
        self.input_host()
        temporal.request_inputs(self.case,1,1,depth_pixels=True)
        self.assertEqual((self.case/'on12-temporal-inputs.private.command').read_bytes(),struct.pack('<6I',24,2,1,1,1,0))
        temporal.request_inputs(self.case,2,2)
        self.assertEqual((self.case/'on12-temporal-inputs.private.command').read_bytes(),struct.pack('<4I',16,1,2,2))
        with self.assertRaises(ValueError):temporal.request_inputs(self.case,3,depth_pixels=1)

    def captured_depth(self):
        values=(0.1,0.25,0.75,1.0)
        pixels=struct.pack('<4f',*values)
        actual=struct.unpack('<4f',pixels)
        physical=dict(matrix_hr=0,viewport=[0,0,2,2],textures=[dict(hr=0,format=114,width=2,height=2)],candidate_c17=[0,0,1,100])
        info=dict(requested=True,attempted=True,written=True,hr=0,frame=10,width=2,height=2,finite=4,interior=3,
                  minimum=float(f'{actual[0]:.9g}'),maximum=1,candidate_scale=100)
        report=dict(format='s7-on12-temporal-inputs-v1',sequence=1,game_taa_enabled=False,depth_capture=info,
                    pairs=[dict(frame=10,composite=dict(physical=physical),ui={})])
        (self.case/'on12-temporal-inputs.private.json').write_text(json.dumps(report))
        path=self.case/'on12-temporal-depth-1.private.bin'
        path.write_bytes(struct.pack('<8s4IQfI',b'S7D32V1\0',1,2,2,114,10,100,0)+pixels)
        return report,path

    def test_same_frame_depth_analysis_preserves_unproven_motion_and_jitter(self):
        self.captured_depth()
        with patch.object(temporal.subprocess,'run') as run:
            result=temporal.analyze_depth(self.case,1)
            self.assertEqual(result['scene_pixels'],3)
            self.assertAlmostEqual(result['linear_depth_range'][0],10,places=5)
            self.assertFalse(result['game_taa_enabled'])
            self.assertFalse(result['dynamic_motion_proven'])
            self.assertFalse(result['jitter_proven'])
            run.assert_not_called()

    def test_depth_analysis_rejects_frame_mismatch_or_partial_pixels(self):
        report,path=self.captured_depth()
        raw=path.read_bytes()
        path.write_bytes(raw[:-1])
        with self.assertRaises(ValueError):temporal.analyze_depth(self.case,1)
        path.write_bytes(raw)
        report['pairs'][0]['frame']=11
        (self.case/'on12-temporal-inputs.private.json').write_text(json.dumps(report))
        with self.assertRaises(ValueError):temporal.analyze_depth(self.case,1)

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
