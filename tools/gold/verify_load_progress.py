"""Bounded original load-progress writers/readers and compiled method guards.

The original serialization bodies execute with synthetic object/stream storage.
VirtualQuery and visitor stream I/O are modeled. No game, native local sender,
mounted resource or completion/Ready observation is produced by this fixture.
"""
import argparse
import gc
import hashlib
import json
from pathlib import Path
import struct

import pefile
import emulate_event_payloads as native
from verify_codec_guards import check_image

BITS=(0,0x80000000,0x3e800000,0x3f800000,0x42c80000)


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    for label in native.PROFILES:parser.add_argument('--'+label.replace('_','-'),type=Path,required=True)
    parser.add_argument('--thunk-executable',type=Path,required=True)
    parser.add_argument('output',type=Path)
    args=parser.parse_args();root=Path(__file__).resolve().parents[2];out=args.output.resolve()
    if not out.is_relative_to((root/'work').resolve()) or out.exists() or not out.parent.is_dir():parser.error('Use a fresh output under repository work')
    raw=args.thunk_executable.read_bytes();fixture=pefile.PE(data=raw)
    if fixture.FILE_HEADER.Machine!=0x14c or fixture.OPTIONAL_HEADER.ImageBase!=0x24000000 or fixture.OPTIONAL_HEADER.SizeOfImage>4*1024*1024:raise ValueError('Expected bounded fixed x86 fixture')
    serialized={};guards={};readers=negative=0
    for edition,label in enumerate(native.PROFILES,1):
        image=native.Image(label,getattr(args,label),types=('CLoadProgressEvent',))
        table,methods=image.tables['CLoadProgressEvent']
        lengths=(6,148) if label=='gold' else (16,52)
        regions=((table,24),*((address,8) for address in methods),(methods[1],lengths[0]),(methods[3],lengths[1]))
        guards[label]=check_image(image,fixture,edition,oracle=b'WePlayLoadProgressMethodsOracle',methods=regions)
        cases={}
        for bits in BITS:
            for asynchronous in (0,1):
                wire=native.payload(image,'CLoadProgressEvent',False,(bits-0x10)&0xffffffff,asynchronous)
                expected=struct.pack('<IIBI',0x11112222,0x33334444,asynchronous,bits)
                if wire!=expected:raise RuntimeError('Original load-progress scalar layout differs')
                cases[(bits,asynchronous)]=wire
        serialized[label]=cases
        print(json.dumps({'profile':label,'writer_cases':len(cases),'compiled_guard_checks':guards[label]}),flush=True)
        del image;gc.collect()
    for label in native.PROFILES:
        image=native.Image(label,getattr(args,label),types=('CLoadProgressEvent',))
        for cases in serialized.values():
            for (bits,asynchronous),wire in cases.items():
                fields=native.payload(image,'CLoadProgressEvent',read_wire=wire)
                if fields!=(0x11112222,0x33334444,asynchronous,bits,0,0,0,0):raise RuntimeError('Original reader field/canary differs')
                readers+=1
                for bad in (wire[:-1],wire+b'\0'):
                    try:native.payload(image,'CLoadProgressEvent',read_wire=bad)
                    except native.InputBoundError:negative+=1
                    else:raise RuntimeError('Malformed native stream accepted')
        del image;gc.collect()
    result={'format':'native-load-progress-fixture-v1','original_writer_cases':sum(map(len,serialized.values())),
        'cross_profile_reader_cases':readers,'truncated_or_trailing_rejections':negative,'compiled_guard_checks':guards,
        'guard_fixture_sha256':hashlib.sha256(raw).hexdigest(),'scalar_payload_bytes':13,'native_factory_class_prefix_bytes':4,
        'original_serialization_bodies_executed':True,'native_factory_or_sender_executed':False,
        'game_process_started':False,'load_completion_qualified':False,'mixed_gameplay_qualified':False}
    with out.open('x') as stream:json.dump(result,stream,indent=2)
    print(json.dumps(result),flush=True)


if __name__=='__main__':main()
