"""Verify compiled native Ready guards using owned fingerprinted analysis images.

Only the compiled guard executes. Original game bytes are read; VirtualQuery
is modeled. No factory, game process, Ready result or world start is executed.
"""
import argparse
import gc
import hashlib
import json
from pathlib import Path
import pefile
import emulate_event_payloads as native
from verify_codec_guards import check_image


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    labels=('gold','ubisoft','steam_analysis')
    for label in labels:parser.add_argument('--'+label.replace('_','-'),type=Path,required=True)
    parser.add_argument('--thunk-executable',type=Path,required=True)
    parser.add_argument('output',type=Path)
    args=parser.parse_args();root=Path(__file__).resolve().parents[2];output=args.output.resolve()
    if not output.is_relative_to((root/'work').resolve()) or output.exists() or not output.parent.is_dir():
        parser.error('Use a fresh output under an existing repository work directory')
    raw=args.thunk_executable.read_bytes();fixture=pefile.PE(data=raw)
    if fixture.FILE_HEADER.Machine!=0x14c or fixture.OPTIONAL_HEADER.ImageBase!=0x24000000 or fixture.OPTIONAL_HEADER.SizeOfImage>4*1024*1024:
        raise ValueError('Expected the bounded fixed x86 codec analysis fixture')
    native.EVENTS=('CToggleReadyStateEvent',)
    visitors=(((0xa49590,0x94),(0xa494f0,0x94)),((0xee4d90,0x34),(0x69c440,0xc7)),((0xee4480,0x34),(0x69c020,0xc7)))
    results={}
    for edition,label in enumerate(labels,1):
        image=native.Image(label,getattr(args,label));table,methods=image.tables[native.EVENTS[0]]
        regions=((table,24),*((entry,8) for entry in methods),*visitors[edition-1])
        if label=='gold':regions+=((0x98e303,0xaf),(0xa51980,0x105))
        results[label]=check_image(image,fixture,edition,oracle=b'WePlayReadyMethodsOracle',methods=regions)
        print(json.dumps({'profile':label,'checks':results[label]}),flush=True)
        del image;gc.collect()
    receipt={'checks':sum(results.values()),'per_profile':results,'fixture_sha256':hashlib.sha256(raw).hexdigest(),
             'compiled_guard_executed':True,'original_game_code_executed':False,'game_process_started':False,
             'native_ready_result_qualified':False,'mixed_gameplay_qualified':False}
    with output.open('x') as stream:json.dump(receipt,stream,indent=2)
    print(json.dumps(receipt))


if __name__=='__main__':main()
