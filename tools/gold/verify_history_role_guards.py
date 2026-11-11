"""Execute our compiled History host-role guards; never execute original game code."""
import argparse
import hashlib
import json
from pathlib import Path
import pefile
import emulate_event_payloads as native
from verify_codec_guards import check_image

METHODS=((0xecbb00,23),(0xed2b30,10),(0xed2af0,53),(0xed7550,10),
         (0xed7290,75),(0x65e870,17),(0x65df30,17),(0x65e8b0,22))


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--ubisoft',type=Path,required=True)
    parser.add_argument('--thunk-executable',type=Path,required=True)
    parser.add_argument('output',type=Path)
    args=parser.parse_args()
    root=Path(__file__).resolve().parents[2]
    output=args.output.resolve()
    if not output.is_relative_to((root/'work').resolve()) or output.exists() or not output.parent.is_dir():
        parser.error('Use a new output under an existing repository work directory')
    raw=args.thunk_executable.read_bytes()
    fixture=pefile.PE(data=raw)
    if (fixture.FILE_HEADER.Machine!=0x14c or fixture.OPTIONAL_HEADER.ImageBase!=0x24000000
            or fixture.OPTIONAL_HEADER.SizeOfImage>4*1024*1024):
        raise ValueError('Expected bounded x86 guard fixture')
    native.EVENTS=()
    image=native.Image('ubisoft',args.ubisoft)
    checks=check_image(image,fixture,2,oracle=b'WePlayHistoryRoleMethodsOracle',methods=METHODS)
    receipt={'checks':checks,'native_methods_guarded':len(METHODS),
             'fixture_sha256':hashlib.sha256(raw).hexdigest(),
             'compiled_guard_executed':True,'original_game_code_executed':False,
             'game_process_started':False,'network_used':False,'mixed_gameplay_qualified':False}
    output.write_text(json.dumps(receipt,indent=2)+'\n')
    print(json.dumps(receipt))


if __name__=='__main__':
    main()
