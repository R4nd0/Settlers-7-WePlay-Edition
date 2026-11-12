"""Exercise compiled map-reload method guards against fingerprinted images.

Only the compiled guard executes. This does not qualify native restoration,
map templates, world starts, simulation, NEO or save/reload.
"""
import argparse,gc,hashlib,json
from pathlib import Path
import pefile
import emulate_event_payloads as native
from verify_codec_guards import check_image

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    for label in ('ubisoft','steam_analysis'):parser.add_argument('--'+label.replace('_','-'),type=Path,required=True)
    parser.add_argument('--thunk-executable',type=Path,required=True);parser.add_argument('output',type=Path)
    args=parser.parse_args();root=Path(__file__).resolve().parents[2];out=args.output.resolve()
    if not out.is_relative_to((root/'work').resolve()) or out.exists() or not out.parent.is_dir():parser.error('Use a fresh output under repository work')
    raw=args.thunk_executable.read_bytes();fixture=pefile.PE(data=raw)
    if fixture.FILE_HEADER.Machine!=0x14c or fixture.OPTIONAL_HEADER.ImageBase!=0x24000000 or fixture.OPTIONAL_HEADER.SizeOfImage>4*1024*1024:raise ValueError('Expected bounded fixed x86 fixture')
    profiles=(
        (2,'ubisoft',(0x546440,0x546450,0x546480,0x5862f0,0x5862e0,0x5862c0,0x546470,0x52a510,0x6576d0,0xef6870)),
        (3,'steam_analysis',(0x545a90,0x545aa0,0x545ad0,0x585db0,0x585da0,0x585d80,0x545ac0,0x529a30,0x6572b0,0xef5f60)))
    native.EVENTS=();results={}
    for edition,label,addresses in profiles:
        image=native.Image(label,getattr(args,label))
        methods=tuple(zip(addresses,(13,29,13,13,13,13,13,95,104,180)))
        results[label]=check_image(image,fixture,edition,oracle=b'WePlayMapReloadMethodsOracle',methods=methods)
        print(json.dumps({'profile':label,'checks':results[label]}),flush=True);del image;gc.collect()
    receipt={'checks':sum(results.values()),'profiles':results,'fixture_sha256':hashlib.sha256(raw).hexdigest(),'compiled_guard_executed':True,'original_game_code_executed':False,'game_process_started':False,'mixed_gameplay_qualified':False}
    with out.open('x') as stream:json.dump(receipt,stream,indent=2)
    print(json.dumps(receipt))
if __name__=='__main__':main()
