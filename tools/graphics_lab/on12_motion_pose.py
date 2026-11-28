"""Bounded read-only observation of actual skinned draw parameter sources.

Raw addresses and pose banks stay private below work/. Candidates never grant
identity or TAA proof: a reused staging address is not an object identifier.
"""
import argparse
from collections import Counter,defaultdict
import hashlib
import json
from pathlib import Path
import struct

from . import recover

HEADER=struct.Struct('<8s6I')
RECORD=struct.Struct('<7Q2Ii4I10I1024f64i16i2Q2Q4I')
assert HEADER.size==32 and RECORD.size==4588


def request(case,sequence,frames=2):
    if type(sequence)is not int or not 1<=sequence<=0xffffffff or type(frames)is not int or not 2<=frames<=4:raise ValueError('Use two to four frames and a fresh positive sequence.')
    case=recover.lab(case)
    if(case/'result.private.json').exists():raise ValueError('The diagnostic game is already finished.')
    value=json.loads(recover._read(case/'on12-spatial-live.private.json',32768)[0])
    if (value.get('format')!='s7-on12-spatial-live-v1'or value.get('initialized')is not True or value.get('stopped')is not False
            or value.get('scene_only')is not True or value.get('failure')!=0 or value.get('motion_pose_ready')is not True):
        raise ValueError('An initialized scene-only pose observer is required.')
    command=case/'on12-motion-pose.private.command'
    if command.exists():
        data=recover._read(command,16)[0]
        if len(data)!=16:raise ValueError('Malformed previous pose command.')
        size,version,previous,count=struct.unpack('<4I',data)
        if(size,version)!=(16,1)or not 2<=count<=4 or previous>=sequence:raise ValueError('Use a fresh pose sequence.')
    recover._atomic(command,struct.pack('<4I',16,1,sequence,frames))


def analyze(case,sequence):
    if type(sequence)is not int or not 1<=sequence<=0xffffffff:raise ValueError('Invalid pose sequence.')
    case=recover.lab(case);data,pin=recover._read(case/f'on12-motion-pose-{sequence}.private.bin',HEADER.size+4*128*RECORD.size)
    if len(data)<HEADER.size:raise ValueError('Truncated pose header.')
    magic,version,size,number,count,frames,requested=HEADER.unpack_from(data)
    if (magic!=b'S7POSE1\0'or version!=1 or size!=RECORD.size or number!=sequence or not 2<=requested<=4 or not 0<frames<=requested
            or count>frames*128 or len(data)!=HEADER.size+count*size):raise ValueError('Invalid pose observation layout.')
    by_source=defaultdict(lambda:defaultdict(set));by_bone_source=defaultdict(lambda:defaultdict(set));frames_seen=Counter();stack=Counter();types=Counter();callers=Counter();sources=set();bone_sources=set();dynamic=0
    for n in range(count):
        offset=HEADER.size+n*RECORD.size;record=RECORD.unpack_from(data,offset);frame,source,caller=record[:3]
        if not frame:raise ValueError('Pose record lacks a frame.')
        frames_seen[frame]+=1
        if frames_seen[frame]>128:raise ValueError('Pose frame exceeds record bound.')
        stack[record[21]]+=1;types[record[20]]+=1;callers[caller]+=1
        if record[17]&0x200:dynamic+=1
        geometry=record[3:7]+record[9:17]
        world=hashlib.sha256(data[offset+124+68*16:offset+124+72*16]).digest()
        if source:sources.add(source);by_source[(source,geometry)][frame].add(world)
        if record[1130]:bone_sources.add(record[1130]);by_bone_source[(record[1130],geometry)][frame].add(world)
    ambiguous=sum(any(len(worlds)>1 for worlds in observed.values())for observed in by_source.values())
    changing=sum(len(observed)>1 and len(set().union(*observed.values()))>1 and all(len(worlds)==1 for worlds in observed.values())for observed in by_source.values())
    bone_ambiguous=sum(any(len(worlds)>1 for worlds in observed.values())for observed in by_bone_source.values())
    return dict(format='s7-on12-motion-pose-analysis-v1',sequence=sequence,sha256=pin['sha256'],captured_frames=frames,requested_frames=requested,complete=frames==requested,
                records=count,distinct_record_frames=len(frames_seen),source_addresses=len(sources),source_geometry_keys=len(by_source),ambiguous_staging_keys=ambiguous,
                changing_pose_candidates=changing,stack_source_records=stack[1],dynamic_vertex_records=dynamic,source_memory_types=dict(types),caller_sites=len(callers),
                bone_source_addresses=len(bone_sources),ambiguous_bone_staging_keys=bone_ambiguous,
                identity_verified=False,game_taa_enabled=False,contains_raw_addresses=False)


def main():
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--case',type=Path,required=True);parser.add_argument('--sequence',type=int,default=1)
    parser.add_argument('--frames',type=int,default=2);parser.add_argument('--analyze',action='store_true');args=parser.parse_args()
    if args.analyze:print(json.dumps(analyze(args.case,args.sequence)))
    else:request(args.case,args.sequence,args.frames);print(json.dumps(dict(requested_frames=args.frames,read_only=True,identity_verified=False,game_taa_enabled=False)))


if __name__=='__main__':main()
