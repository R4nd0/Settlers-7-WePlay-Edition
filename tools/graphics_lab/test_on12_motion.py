"""Motion runner admission tests; fake executables are never executed."""
from contextlib import redirect_stdout
import io
import json
from pathlib import Path
import tempfile
from types import SimpleNamespace
import unittest
from unittest.mock import patch

from . import on12_motion as motion


class Tests(unittest.TestCase):
    def setUp(self):
        self.temporary=tempfile.TemporaryDirectory(dir=motion.ROOT/'work');self.addCleanup(self.temporary.cleanup)
        self.case=Path(self.temporary.name);self.build=motion.prepare(self.case/'build')
        (self.build/'on12_motion_gpu.exe').write_bytes(b'never execute this fake binary')
        files={}
        for name in motion.SOURCES:
            raw=(motion.ROOT/name).read_bytes();(self.build/Path(name).name).write_bytes(raw);files[name]=motion.on12_spatial.loader.digest(motion.ROOT/name)
        for name in motion.ARTIFACTS:files[name]=motion.on12_spatial.loader.digest(self.build/name)
        value=dict(format='s7-on12-motion-build-v1',gpu_executed=False,
            build_sha256=motion.on12_spatial.loader.digest(motion.ROOT/'native/on12_motion_build.ps1'),files=files)
        (self.build/motion.MANIFEST).write_text(json.dumps(value))

    def test_changed_program_is_rejected_without_starting_a_process(self):
        motion.verify_build(self.build);(self.build/'combined.vs.bin').write_bytes(b'changed')
        with patch.object(motion.subprocess,'run')as run:
            with self.assertRaises(ValueError):motion.verify_build(self.build)
            run.assert_not_called()

    def test_current_image_success_does_not_replace_previous_position_proof(self):
        incomplete=dict(passed=True,synthetic_only=True,current_image_unchanged=True,bone_wind_rigid_perspective=True)
        done=SimpleNamespace(returncode=0,stdout=json.dumps(incomplete).encode(),stderr=b'')
        with patch.object(motion.recover,'_no_games'),patch.object(motion.dxwrapper,'artifacts',return_value={}),patch.object(motion.subprocess,'run',return_value=done),redirect_stdout(io.StringIO()):
            with self.assertRaisesRegex(RuntimeError,'Incomplete'):motion.gpu(self.build,self.case/'package',self.case/'result')


if __name__=='__main__':unittest.main()
