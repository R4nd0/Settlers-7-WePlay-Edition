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
from . import on12_motion_program as vertex,on12_motion_pixel as pixel


class Tests(unittest.TestCase):
    def setUp(self):
        self.temporary=tempfile.TemporaryDirectory(dir=motion.ROOT/'work');self.addCleanup(self.temporary.cleanup)
        self.case=Path(self.temporary.name);self.build=motion.prepare(self.case/'build')
        (self.build/'on12_motion_gpu.exe').write_bytes(b'never execute this fake binary')
        files={}
        for name in motion.SOURCES:
            raw=(motion.ROOT/name).read_bytes();(self.build/Path(name).name).write_bytes(raw);files[name]=motion.on12_spatial.loader.digest(motion.ROOT/name)
        for name in motion.ARTIFACTS:files[name]=motion.on12_spatial.loader.digest(self.build/name)
        value=dict(format='s7-on12-motion-build-v1',gpu_executed=False,packed=False,
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

    def test_packed_case_fits_original_output_budget_and_ten_pixel_inputs(self):
        programs=motion.authored_programs(packed=True)
        original_vertex=vertex.checked(programs['current.vs.bin']);combined_vertex=vertex.checked(programs['combined.vs.bin'])
        self.assertEqual(len(vertex.declarations(original_vertex)[1]),11)
        self.assertEqual(len(vertex.declarations(combined_vertex)[1]),11)
        old=pixel.checked(programs['color.ps.bin']);new=pixel.checked(programs['combined.ps.bin'])
        old_inputs={i.words[2]&vertex.INDEX:(i.words[1],i.words[2]&vertex.MASK)for i in old if i.opcode==vertex.DCL and vertex.register_type(i.words[2])==vertex.INPUT}
        new_inputs={i.words[2]&vertex.INDEX:(i.words[1],i.words[2]&vertex.MASK)for i in new if i.opcode==vertex.DCL and vertex.register_type(i.words[2])==vertex.INPUT}
        self.assertEqual((len(old_inputs),len(new_inputs)),(9,10))
        for index,(semantic,mask)in old_inputs.items():
            self.assertEqual(new_inputs[index][0],semantic);self.assertEqual(new_inputs[index][1]&mask,mask)
        expected=[i.words for i in old if i.opcode not in vertex.SETUP];observed=[i.words for i in new if i.opcode not in vertex.SETUP]
        self.assertEqual(observed[:len(expected)],expected)


if __name__=='__main__':unittest.main()
