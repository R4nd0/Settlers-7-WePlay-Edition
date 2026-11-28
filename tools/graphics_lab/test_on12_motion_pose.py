"""Pose observation controls and ambiguity checks; no process or GPU starts."""
import json
from pathlib import Path
import struct
import tempfile
import unittest

from . import on12_motion_pose as pose


class Tests(unittest.TestCase):
    def setUp(self):
        self.temporary=tempfile.TemporaryDirectory(dir=pose.recover.ROOT/'work');self.addCleanup(self.temporary.cleanup);self.case=Path(self.temporary.name)

    def test_read_only_request_needs_ready_host_and_fresh_sequence(self):
        value=dict(format='s7-on12-spatial-live-v1',initialized=True,stopped=False,scene_only=True,failure=0)
        live=self.case/'on12-spatial-live.private.json';live.write_text(json.dumps(value))
        with self.assertRaises(ValueError):pose.request(self.case,1)
        value['motion_pose_ready']=True;live.write_text(json.dumps(value));pose.request(self.case,1,2)
        self.assertEqual((self.case/'on12-motion-pose.private.command').read_bytes(),struct.pack('<4I',16,1,1,2))
        with self.assertRaises(ValueError):pose.request(self.case,1)
        with self.assertRaises(ValueError):pose.request(self.case,2,1)

    def record(self,frame,source,x):
        floats=[0.0]*1024;floats[68*4]=x
        return pose.RECORD.pack(frame,source,0x1000,0x2000,0x3000,0x4000,0x5000,
            2,4,0,0,3,0,1,0,32,1,0,1024,101,0x20000,0,68,4,*floats,*([0]*64),*([0]*16),0,0,source,0,80,150,0x20000,0)

    def test_shared_staging_source_is_not_promoted_to_identity(self):
        records=[self.record(10,0x1111,1),self.record(10,0x1111,2),self.record(11,0x1111,3),self.record(10,0x2222,4),self.record(11,0x2222,5)]
        data=pose.HEADER.pack(b'S7POSE1\0',1,pose.RECORD.size,1,len(records),2,2)+b''.join(records)
        path=self.case/'on12-motion-pose-1.private.bin';path.write_bytes(data);result=pose.analyze(self.case,1)
        self.assertEqual((result['ambiguous_staging_keys'],result['changing_pose_candidates']),(1,1))
        self.assertFalse(result['identity_verified']);self.assertFalse(result['contains_raw_addresses']);self.assertTrue(result['complete'])
        path.write_bytes(data[:-4])
        with self.assertRaises(ValueError):pose.analyze(self.case,1)


if __name__=='__main__':unittest.main()
