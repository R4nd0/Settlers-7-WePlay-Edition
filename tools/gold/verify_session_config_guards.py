"""Exercise the compiled config guard against owned, pinned analysis images.

Only the compiled guard runs. Original game code is read, not executed;
VirtualQuery is modeled. No game, factory, queue consumer or network runs.
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
    native.EVENTS=('CSessionConfigEvent@ENetwork',)
    results={}
    for edition,label in enumerate(labels,1):
        image=native.Image(label,getattr(args,label));table,methods=image.tables[native.EVENTS[0]]
        slot,size=((0x98d690,0x8e),(0x8405a0,0x86),(0x840370,0x86))[edition-1]
        constructor,constructor_size=((0x98d0a0,0xae),(0x83ffc0,0xc0),(0x83fd90,0xc0))[edition-1]
        regions=((table,24),*((entry,8) for entry in methods),(slot,size),(constructor,constructor_size))
        results[label]=check_image(image,fixture,edition,oracle=b'WePlaySessionConfigMethodsOracle',methods=regions)
        print(json.dumps({'profile':label,'checks':results[label]}),flush=True)
        del image;gc.collect()
    receipt={'checks':sum(results.values()),'per_profile':results,'fixture_sha256':hashlib.sha256(raw).hexdigest(),
             'compiled_guard_executed':True,'original_game_code_executed':False,'game_process_started':False,
             'network_used':False,'native_factory_qualified':False,'mixed_gameplay_qualified':False}
    with output.open('x') as stream:json.dump(receipt,stream,indent=2)
    print(json.dumps(receipt))


if __name__=='__main__':main()
