"""Check compiled History observed-Ready guards against owned analysis images.

Only guard code executes; original setter/consumer bytes are never executed.
This does not establish a Ready response or playable crossplay.
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
    native.EVENTS=();results={}
    for edition,label,setter,consumer,constructor in ((2,'ubisoft',0x5862c0,0x841c70,0x690c20),(3,'steam_analysis',0x585d80,0x841a40,0x690800)):
        image=native.Image(label,getattr(args,label))
        results[label]=check_image(image,fixture,edition,oracle=b'WePlayReadyStateMethodsOracle',methods=((setter,13),(consumer,0x181),(constructor,0xa0)))
        print(json.dumps({'profile':label,'checks':results[label]}),flush=True);del image;gc.collect()
    receipt={'checks':sum(results.values()),'profiles':results,'fixture_sha256':hashlib.sha256(raw).hexdigest(),'compiled_guard_executed':True,'original_game_code_executed':False,'game_process_started':False,'mixed_gameplay_qualified':False}
    with out.open('x') as stream:json.dump(receipt,stream,indent=2)
    print(json.dumps(receipt))
if __name__=='__main__':main()
