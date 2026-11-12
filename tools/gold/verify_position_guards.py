"""Check compiled History position guards and original setters offline.

Owned pinned analysis images only. Native helper lists and preparation rows are
synthetic; VirtualQuery is modeled. No game, event factory, binding or world runs.
"""
import argparse,gc,hashlib,json,struct
from pathlib import Path
import pefile
from emulate_event_payloads import Image
from verify_codec_guards import check_image

ENTRIES={'ubisoft':(2,0x5863e0,0x585c20),'steam_analysis':(3,0x585ea0,0x5856e0)}

def setters(image,entry):
    from unicorn import Uc,UC_ARCH_X86,UC_MODE_32,UC_HOOK_MEM_WRITE
    from unicorn.x86_const import (UC_X86_REG_EBX,UC_X86_REG_ECX,UC_X86_REG_ESI,
        UC_X86_REG_EDI,UC_X86_REG_EBP,UC_X86_REG_ESP)
    base=0x20000000;rows=base+0x1000;config=base+0xb000;stack=base+0xf000;stop=base+0xe000;count=0
    for target,value,human,ai in [(1,2,[1,2],[]),(2,1,[1,2],[]),(1,3,[1,2],[3]),
                                  (1,4,[1,2],[3]),(1,1,[1,2],[]),(25,2,[1,2],[])]:
        u=Uc(UC_ARCH_X86,UC_MODE_32);u.mem_map(image.base,(image.size+4095)&~4095);u.mem_write(image.base,image.mapped);u.mem_map(base,0x10000)
        before=bytearray([0x37])*0x8dd0
        for index in range(25):before[index*0x5a8+0x58]=index
        u.mem_write(rows,bytes(before));u.mem_write(config,struct.pack('<II',rows,rows))
        for off,indices,head in [(0x8dbc,human,base+0xc000),(0x8dc4,ai,base+0xc400)]:
            nodes=[head+0x20+i*0x10 for i in range(len(indices))]
            u.mem_write(rows+off,struct.pack('<I',head));u.mem_write(head,struct.pack('<II',nodes[0] if nodes else head,nodes[-1] if nodes else head))
            for i,(node,index) in enumerate(zip(nodes,indices)):
                u.mem_write(node,struct.pack('<IIB',nodes[i+1] if i+1<len(nodes) else head,nodes[i-1] if i else head,index))
        initial=bytes(u.mem_read(rows,len(before)));expected=bytearray(initial)
        if target<25:
            occupant=next((i for i in human+ai if initial[i*0x5a8+0x58]==value),None)
            if occupant!=target:
                if occupant is not None:expected[occupant*0x5a8+0x58]=initial[target*0x5a8+0x58]
                expected[target*0x5a8+0x58]=value
        u.mem_write(stack,struct.pack('<III',stop,target,value))
        kept={UC_X86_REG_EBX:0x11111111,UC_X86_REG_ESI:0x22222222,UC_X86_REG_EDI:0x33333333,UC_X86_REG_EBP:0x44444444}
        for reg,value_ in {**kept,UC_X86_REG_ECX:config,UC_X86_REG_ESP:stack}.items():u.reg_write(reg,value_)
        allowed={rows+index*0x5a8+0x58 for index in range(25)}
        def write(uc,access,address,size,value_,data):
            if not ((address in allowed and size==1) or stack-128<=address<=address+size<=stack):
                raise RuntimeError('Original setter wrote outside position fields/stack')
        u.hook_add(UC_HOOK_MEM_WRITE,write);u.emu_start(entry,stop,count=2000,timeout=100000)
        if (bytes(u.mem_read(rows,len(before)))!=bytes(expected) or bytes(u.mem_read(config,8))!=struct.pack('<II',rows,rows)
                or u.reg_read(UC_X86_REG_ESP)!=stack+12 or any(u.reg_read(reg)!=value_ for reg,value_ in kept.items())):
            raise RuntimeError('Original position setter changed its bounded ABI/expected fields')
        count+=1
    return count

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    for label in ENTRIES:parser.add_argument('--'+label.replace('_','-'),type=Path,required=True)
    parser.add_argument('--thunk-executable',type=Path,required=True);parser.add_argument('output',type=Path)
    args=parser.parse_args();root=Path(__file__).resolve().parents[2];out=args.output.resolve()
    if not out.is_relative_to((root/'work').resolve()) or out.exists() or not out.parent.is_dir():parser.error('Use a fresh output under repository work')
    raw=args.thunk_executable.read_bytes();fixture=pefile.PE(data=raw)
    if fixture.FILE_HEADER.Machine!=0x14c or fixture.OPTIONAL_HEADER.ImageBase!=0x24000000 or fixture.OPTIONAL_HEADER.SizeOfImage>4*1024*1024:raise ValueError('Expected bounded fixed x86 fixture')
    results={}
    for label,(edition,setter,helper) in ENTRIES.items():
        image=Image(label,getattr(args,label),types=())
        guards=check_image(image,fixture,edition,oracle=b'WePlayPositionSetterMethodsOracle',methods=((setter,0x57),(helper,0x79)))
        results[label]={'compiled_guards':guards,'original_setter_calls':setters(image,setter)}
        print(json.dumps({'profile':label,**results[label]}),flush=True);del image;gc.collect()
    receipt={'profiles':results,'compiled_guards':sum(p['compiled_guards'] for p in results.values()),
        'original_setter_calls':sum(p['original_setter_calls'] for p in results.values()),
        'fixture_sha256':hashlib.sha256(raw).hexdigest(),'synthetic_lists':True,'virtual_query_modeled':True,
        'high_level_projection_executed':False,'game_process_started':False,'mixed_gameplay_qualified':False}
    with out.open('x') as stream:json.dump(receipt,stream,indent=2)
    print(json.dumps(receipt))
if __name__=='__main__':main()
