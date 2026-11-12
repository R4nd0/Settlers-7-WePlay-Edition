"""Check compiled Color/Team guards against owned analysis images offline.

VirtualQuery is modeled. This does not execute a native factory, preparation
projection, event consumer, network, game process or world.
"""
import argparse,gc,hashlib,json
from pathlib import Path
import pefile
import emulate_event_payloads as native
from verify_codec_guards import check_image

EVENTS=(('CPlayerColorChangedEvent',b'WePlayColorMethodsOracle'),
        ('CPlayerTeamChangedEvent',b'WePlayTeamMethodsOracle'))

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    for label in native.PROFILES:parser.add_argument('--'+label.replace('_','-'),type=Path,required=True)
    parser.add_argument('--thunk-executable',type=Path,required=True);parser.add_argument('output',type=Path)
    args=parser.parse_args();root=Path(__file__).resolve().parents[2];out=args.output.resolve()
    if not out.is_relative_to((root/'work').resolve()) or out.exists() or not out.parent.is_dir():parser.error('Use a fresh output under repository work')
    raw=args.thunk_executable.read_bytes();fixture=pefile.PE(data=raw)
    if fixture.FILE_HEADER.Machine!=0x14c or fixture.OPTIONAL_HEADER.ImageBase!=0x24000000 or fixture.OPTIONAL_HEADER.SizeOfImage>4*1024*1024:raise ValueError('Expected bounded fixed x86 fixture')
    results={}
    for edition,label in enumerate(native.PROFILES,1):
        image=native.Image(label,getattr(args,label),types=tuple(name for name,_ in EVENTS));results[label]={}
        for name,oracle in EVENTS:
            table,methods=image.tables[name]
            results[label][name]=check_image(image,fixture,edition,oracle=oracle,methods=((table,24),*((address,32) for address in methods)))
        if label!='gold':
            setter=0x545a80 if label=='steam_analysis' else 0x546430
            results[label]['color_setter']=check_image(image,fixture,edition,oracle=b'WePlayColorSetterMethodsOracle',methods=((setter,13),))
        print(json.dumps({'profile':label,'checks':results[label]}),flush=True);del image;gc.collect()
    receipt={'checks':sum(sum(profile.values()) for profile in results.values()),'profiles':results,
        'fixture_sha256':hashlib.sha256(raw).hexdigest(),'compiled_guard_executed':True,
        'original_game_code_executed':False,'game_process_started':False,'mixed_gameplay_qualified':False}
    with out.open('x') as stream:json.dump(receipt,stream,indent=2)
    print(json.dumps(receipt))
if __name__=='__main__':main()
