"""Exercise original room-map constructors, copy setter and destructor in x86.

Uses bounded synthetic descriptors, modeled imported heap/memory operations and
a private replacement for the temporary property's default dirty-flag pointer.
No game process, backend connection, map load or world is executed.
"""
import argparse,gc,json,struct
from pathlib import Path
import pefile
import emulate_event_payloads as native
from unicorn import Uc,UC_ARCH_X86,UC_MODE_32,UC_HOOK_CODE,UC_HOOK_MEM_WRITE
from unicorn.x86_const import UC_X86_REG_EAX,UC_X86_REG_EBX,UC_X86_REG_EBP,UC_X86_REG_ECX,UC_X86_REG_ESI,UC_X86_REG_EDI,UC_X86_REG_ESP,UC_X86_REG_EIP

S,STACK=0x20000000,0x30000000
STOP,DESC,SOURCE,TEXT,DIRTY,HEAP=S,S+0x1000,S+0x2000,S+0x3000,S+0x4000,S+0x10000

def execute(image,imports,ctor,setter,property_type,text):
    u=Uc(UC_ARCH_X86,UC_MODE_32);u.mem_map(image.base,(image.size+4095)&~4095);u.mem_write(image.base,image.mapped)
    u.mem_map(0,4096);u.mem_map(S,0x100000);u.mem_map(STACK,0x10000)
    def words(at,*values):u.mem_write(at,struct.pack('<'+'I'*len(values),*values))
    def word(at):return struct.unpack('<I',u.mem_read(at,4))[0]
    words(DESC+0x1c8,property_type,0,DIRTY)
    u.mem_write(TEXT,text+b'\0');u.mem_write(TEXT+512,b'original/map\0')
    allocs={};next_heap=HEAP;stubs={};executed=set();last=[]
    def owned(at,n):
        return (STACK<=at<=at+n<=STACK+0x10000 or DESC<=at<=at+n<=DESC+0x300
            or SOURCE<=at<=at+n<=SOURCE+24 or DIRTY<=at<=at+n<=DIRTY+2
            or any(p<=at<=at+n<=p+size for p,size in allocs.items()))
    for name,address in imports:
        name={'??2@YAPAXI@Z':'malloc','??3@YAXPAX@Z':'free'}.get(name,name)
        if name in ('malloc','free','memcpy','memmove','memset'):
            stub=S+0x5000+len(stubs)*16;stubs[stub]=name;words(address,stub)
    stubs[0x66caa0 if image.label=='ubisoft' else 0x66c680]='malloc'
    stubs[0x66cad0 if image.label=='ubisoft' else 0x66c6b0]='free'
    stubs[0xd350f0 if image.label=='ubisoft' else 0xd34720]='malloc'
    stubs[0xd351d0 if image.label=='ubisoft' else 0xd34800]='free'
    def ret(n,value=0):
        sp=u.reg_read(UC_X86_REG_ESP);u.reg_write(UC_X86_REG_EAX,value);u.reg_write(UC_X86_REG_EIP,word(sp));u.reg_write(UC_X86_REG_ESP,sp+4+n)
    def code(uc,at,size,data):
        nonlocal next_heap
        executed.add(at);last.append(at)
        if len(last)>12:last.pop(0)
        if at==STOP:uc.emu_stop();return
        if at==(0xcb6c60 if image.label=='ubisoft' else 0xcb6c70):ret(0,DIRTY+1);return
        name=stubs.get(at)
        if name:
            sp=uc.reg_read(UC_X86_REG_ESP);p=word(sp+4)
            if name=='malloc':
                if not 0<p<=65536:raise RuntimeError('Unbounded allocation')
                result=next_heap;next_heap+=(p+15)&~15
                if next_heap>HEAP+0x80000:raise RuntimeError('Heap bound')
                allocs[result]=p;ret(0,result)
            elif name=='free':
                if p:del allocs[p]
                ret(0)
            else:
                value,n=word(sp+8),word(sp+12)
                if n>65536 or not owned(p,n):raise RuntimeError('Memory ownership bound')
                uc.mem_write(p,bytes((value&255,))*n if name=='memset' else bytes(uc.mem_read(value,n)));ret(0,p)
        elif not image.base<=at<image.base+image.size:raise RuntimeError('Foreign code '+hex(at))
    def writes(uc,access,at,size,value,data):
        if not (at==0 and size==4 or owned(at,size)):raise RuntimeError('Foreign write '+hex(at)+' via '+str([hex(x) for x in last]))
    u.hook_add(UC_HOOK_CODE,code);u.hook_add(UC_HOOK_MEM_WRITE,writes)
    kept={UC_X86_REG_EBX:0x12345678,UC_X86_REG_EBP:0x23456789,UC_X86_REG_ESI:0x34567890,UC_X86_REG_EDI:0x456789ab}
    def call(at,this,args):
        sp=STACK+0xf000;words(sp,STOP,*args)
        for reg,value in {**kept,UC_X86_REG_ESP:sp,UC_X86_REG_ECX:this}.items():u.reg_write(reg,value)
        u.emu_start(at,STOP+1,count=300000,timeout=2000000)
        if u.reg_read(UC_X86_REG_EIP)!=STOP or u.reg_read(UC_X86_REG_ESP)!=sp+4+4*len(args) or word(0)!=0 or any(u.reg_read(k)!=v for k,v in kept.items()):raise RuntimeError('Original by-value ABI incomplete '+str([hex(x) for x in last]))
    call(ctor,DESC+0x1d4,[TEXT+512]);call(ctor,SOURCE,[TEXT])
    source_pointer=word(SOURCE) if word(SOURCE+20)>=16 else SOURCE
    before=bytes(u.mem_read(DESC,0x300))
    call(setter,DESC,list(struct.unpack('<6I',u.mem_read(SOURCE,24))))
    result=DESC+0x1d4;length,capacity=word(result+16),word(result+20)
    pointer=word(result) if capacity>=16 else result
    if length!=len(text) or bytes(u.mem_read(pointer,length+1))!=text+b'\0':raise RuntimeError('Original room map differs')
    if pointer==source_pointer or (len(text)>=16 and source_pointer in allocs):raise RuntimeError('Original setter did not transfer/destroy its by-value source')
    expected=bytearray(before);expected[0x1cc]=1;expected[0x1d4:0x1ec]=u.mem_read(result,24)
    if bytes(u.mem_read(DESC,0x300))!=expected or u.mem_read(DIRTY,1)!=b'\1' or setter not in executed or ctor not in executed:raise RuntimeError('Unrelated descriptor state changed')
    if len(allocs)!=(1 if len(text)>=16 else 0):raise RuntimeError('Unexpected native allocation lifetime')

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    for label in ('ubisoft','steam_analysis'):parser.add_argument('--'+label.replace('_','-'),type=Path,required=True)
    parser.add_argument('output',type=Path);args=parser.parse_args();root=Path(__file__).resolve().parents[2];out=args.output.resolve()
    if not out.is_relative_to((root/'work').resolve()) or out.exists() or not out.parent.is_dir():parser.error('Use a fresh output under repository work')
    native.EVENTS=();results={}
    for label,ctor,setter,property_type in (('ubisoft',0x6576d0,0xef6870,0x1086d3c),('steam_analysis',0x6572b0,0xef5f60,0x1084d6c)):
        path=getattr(args,label);image=native.Image(label,path);pe=pefile.PE(str(path))
        imports=[(imp.name.decode() if imp.name else '',imp.address) for lib in pe.DIRECTORY_ENTRY_IMPORT for imp in lib.imports];del pe
        values=(b'',b'a',b'Maps/Short',b'Maps/Skirmish/Synthetic',b'Maps/'+b'x'*250)
        for value in values:execute(image,imports,ctor,setter,property_type,value)
        results[label]=len(values);print(json.dumps({'profile':label,'original_room_map_setter_cases':len(values)}),flush=True);del image;gc.collect()
    receipt={'cases':sum(results.values()),'profiles':results,'original_constructor_setter_and_destructor_executed':True,'imported_memory_and_native_heap_modeled':True,'temporary_default_dirty_flag_lookup_modeled':True,'game_process_started':False,'mixed_gameplay_qualified':False}
    with out.open('x') as stream:json.dump(receipt,stream,indent=2)
    print(json.dumps(receipt))
if __name__=='__main__':main()
