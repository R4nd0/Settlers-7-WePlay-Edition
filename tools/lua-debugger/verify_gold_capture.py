"""Read-only Gold capture guard audit; no game launch or process attachment."""
import argparse
import hashlib
import json
from pathlib import Path
import gold_capture

PINS={'ec27c7fab2c83a1444f043eb4730f0065393ca2e91607f71e5fd78ee5a5c7c94',
      '76ae5c8a9f74d997a662d347803398cf39ea034e5344725c0ccb0a1ea94bf5ab'}


def audit(path):
    import pefile
    path=Path(path)
    if path.stat().st_size>64*1024*1024:raise ValueError('Gold image exceeds size bound')
    data=path.read_bytes();pin=hashlib.sha256(data).hexdigest()
    if pin not in PINS:raise ValueError('Unreviewed Gold image')
    pe=pefile.PE(data=data)
    try:
        if pe.FILE_HEADER.Machine!=0x14c or pe.OPTIONAL_HEADER.ImageBase!=0x400000:raise ValueError('Gold image layout')
        guards=gold_capture.guards(pe);checks=0
        class Corrupt:
            DIRECTORY_ENTRY_BASERELOC=pe.DIRECTORY_ENTRY_BASERELOC
            def __init__(self,address,offset):self.address,self.offset=address,offset
            def get_data(self,rva,size):
                value=bytearray(pe.get_data(rva,size))
                if rva==self.address-0x400000:value[self.offset]^=1
                return bytes(value)
        for address,size,_ in gold_capture.REGIONS:
            for offset in (0,size-1):
                try:gold_capture.guards(Corrupt(address,offset))
                except ValueError:checks+=1
                else:raise AssertionError('Modified native region accepted')
        expected={'log':{0x984210,0x10abdd8,0xf4d2e0,0xbe4c90},
                  'console':{0x9842a0,0x10abde8,0xf4d2ec,0xbe4c90}}
        for name,addresses in expected.items():
            if {g['address'] for g in gold_capture.guards(pe,[name])}!=addresses:raise AssertionError('Channel guard scope')
        return dict(image_sha256=pin,original_regions=len(guards),mutations_rejected=checks,
            relocation_entries=sum(len(g['relocations']) for g in guards),selective_channel_scopes_verified=True,
            game_started=False,process_attached=False)
    finally:pe.close()


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--image',type=Path,required=True);parser.add_argument('--output',type=Path,required=True)
    args=parser.parse_args();root=Path(__file__).resolve().parents[2]/'work';output=args.output.resolve()
    if not output.is_relative_to(root) or output.exists() or not output.parent.is_dir():
        parser.error('Use a fresh output file beneath repository work/')
    report=audit(args.image);output.write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8');print(json.dumps(report))


if __name__=='__main__':main()
