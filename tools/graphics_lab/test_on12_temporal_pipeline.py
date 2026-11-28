"""Pipeline admission tests; fake artifacts are never executed."""
from contextlib import redirect_stdout
import io
import json
from types import SimpleNamespace
import unittest
from unittest.mock import patch

from . import on12_temporal_pipeline as pipeline
from . import test_on12_temporal as fixtures


class Tests(unittest.TestCase):
    def setUp(self):
        self.fixture=fixtures.Tests(methodName='runTest');self.fixture.setUp()
        self.addCleanup(self.fixture.doCleanups)
        self.build=self.fixture.build
        for name in pipeline.SOURCES:(self.build/name).write_bytes((pipeline.ROOT/'native'/name).read_bytes())
        (self.build/pipeline.EXECUTABLE).write_bytes(b'never execute this fake fixture')
        self.manifest=dict(format='s7-on12-temporal-pipeline-build-v1',architecture='x86',gpu_executed=False,
            build_sha256=pipeline.temporal.loader.digest(pipeline.ROOT/'native/on12_temporal_pipeline_build.ps1'),
            temporal_build_sha256=pipeline.temporal.loader.digest(self.build/pipeline.temporal.MANIFEST),
            files={**self.fixture.manifest['files'],**{name:pipeline.temporal.loader.digest(self.build/name) for name in (*pipeline.SOURCES,pipeline.EXECUTABLE)}})
        self.save()

    def save(self):(self.build/pipeline.MANIFEST).write_text(json.dumps(self.manifest))

    def test_source_and_base_identity_are_both_required(self):
        with patch.object(pipeline.subprocess,'run')as run:
            self.assertEqual(pipeline.verify_build(self.build)[1]['architecture'],'x86');run.assert_not_called()
            (self.build/pipeline.SOURCES[0]).write_bytes(b'changed source')
            with self.assertRaises(ValueError):pipeline.verify_build(self.build)

    def test_base_manifest_and_architecture_cannot_be_substituted(self):
        self.manifest['architecture']='x64';self.save()
        with self.assertRaises(ValueError):pipeline.verify_build(self.build)
        self.manifest['architecture']='x86';self.manifest['temporal_build_sha256']='0'*64;self.save()
        with self.assertRaises(ValueError):pipeline.verify_build(self.build)

    def test_missing_pipeline_result_is_rejected_after_successful_core(self):
        core=dict(passed=True,synthetic_only=True,temporal_graph_matched=True)
        incomplete=dict(passed=True,synthetic_only=True,pipeline_reference_matched=True,unclassified_rejected=True)
        done=SimpleNamespace(returncode=0,stdout=(json.dumps(core)+'\n'+json.dumps(incomplete)+'\n').encode(),stderr=b'')
        with patch.object(pipeline.recover,'_no_games'),patch.object(pipeline.subprocess,'run',return_value=done),redirect_stdout(io.StringIO()):
            with self.assertRaisesRegex(RuntimeError,'Unexpected'):pipeline.gpu(self.build,self.fixture.case/'gpu-result')


if __name__=='__main__':unittest.main()
