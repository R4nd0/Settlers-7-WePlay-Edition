"""Execute original fresh-start construction, visitors, clone/queue and teardown.

Bounded offline x86 emulation, never a game/world start. Heap/lock/stream imports
and reflection names are modeled; native constructors, event visitors, string
operations, clone, local-only send and destructors execute. This does not run a
native factory/queue consumer or prove setup agreement, turns or crossplay.
"""
import argparse,gc,hashlib,json,struct
from pathlib import Path
import pefile
from unicorn import Uc,UC_ARCH_X86,UC_MODE_32,UC_HOOK_CODE,UC_HOOK_MEM_WRITE
from unicorn.x86_const import (UC_X86_REG_EAX,UC_X86_REG_EBX,UC_X86_REG_EBP,UC_X86_REG_ESI,
    UC_X86_REG_EDI,UC_X86_REG_ECX,UC_X86_REG_ESP,UC_X86_REG_EIP)
import emulate_event_payloads as native

S,STACK=0x20000000,0x30000000
O,D,N,T,MAP,STOP,H=(S+0x1000,S+0x2000,S+0x3000,S+0x8000,S+0x9000,S+0x5000,S+0x10000)
VISITOR,STREAM,VT,POSITION,WRITE,READ,NAME,REFLECTION=(S+0xa000,S+0xa400,S+0xa800,S+0xb000,S+0xb010,S+0xb020,S+0xc000,S+0xd000)
CONSTRUCTORS={'gold':0xa33d20,'ubisoft':0x657250,'steam_analysis':0x656e30}
SEND={'gold':0xa4e430,'ubisoft':0xedb300,'steam_analysis':0xeda9f0}

def execute(image,imports,fixture,cid):
    u=Uc(UC_ARCH_X86,UC_MODE_32);u.mem_map(image.base,(image.size+4095)&~4095);u.mem_write(image.base,image.mapped)
    u.mem_map(0,4096);u.mem_map(S,0x100000);u.mem_map(STACK,0x10000)
    base=fixture.OPTIONAL_HEADER.ImageBase;u.mem_map(base,(fixture.OPTIONAL_HEADER.SizeOfImage+4095)&~4095);u.mem_write(base,fixture.get_memory_mapped_image())
    send_thunk=next(base+x.address for x in fixture.DIRECTORY_ENTRY_EXPORT.symbols if x.name==b'WePlayLocalSendOracle')
    def words(a,*v):u.mem_write(a,struct.pack('<'+'I'*len(v),*v))
    def word(a):return struct.unpack('<I',u.mem_read(a,4))[0]
    table,methods=image.tables['CGameStartEvent'];allocs={};stubs={};next_heap=H;locks=[];output=bytearray();reading=False;position=0;input_wire=b'';last=[]
    def owned(a,n):
        return (STACK<=a<=a+n<=STACK+0x10000 or O<=a<=a+n<=O+0x20 or D<=a<=a+n<=D+0x20
            or T<=a<=a+n<=T+0x100 or MAP<=a<=a+n<=MAP+0x100
            or VISITOR<=a<=a+n<=VISITOR+0x214 or any(p<=a<=a+n<=p+size for p,size in allocs.items()))
    for name,address in imports:
        name={'??2@YAPAXI@Z':'malloc','??3@YAXPAX@Z':'free'}.get(name,name)
        if name in ('malloc','free','memcpy','memmove','memset','EnterCriticalSection','LeaveCriticalSection'):
            stub=S+0x6000+len(stubs)*16;stubs[stub]=name;words(address,stub)
    def returned(n,value=0):
        sp=u.reg_read(UC_X86_REG_ESP);u.reg_write(UC_X86_REG_EAX,value);u.reg_write(UC_X86_REG_EIP,word(sp));u.reg_write(UC_X86_REG_ESP,sp+4+n)
    def code(uc,a,size,data):
        nonlocal next_heap,position
        last.append(a)
        if len(last)>12:last.pop(0)
        if a==STOP:uc.emu_stop();return
        name='memcpy' if image.label=='gold' and a==0xbaee6c else stubs.get(a)
        if name:
            sp=uc.reg_read(UC_X86_REG_ESP);p=word(sp+4)
            if name in ('EnterCriticalSection','LeaveCriticalSection'):
                if p!=T+0x80:raise RuntimeError('foreign queue lock')
                locks.append(name);returned(4)
            elif name=='malloc':
                if not 0<p<=65536:raise RuntimeError('allocation size')
                ptr=next_heap;next_heap+=(p+15)&~15
                if next_heap>H+0x80000:raise RuntimeError('heap bound')
                allocs[ptr]=p;returned(0,ptr)
            elif name=='free':
                if p:del allocs[p]
                returned(0)
            else:
                value,n=word(sp+8),word(sp+12)
                if n>65536 or not owned(p,n):raise RuntimeError('copy ownership')
                uc.mem_write(p,bytes((value&255,))*n if name=='memset' else bytes(uc.mem_read(value,n)));returned(0,p)
        elif a==REFLECTION or a==image.name_helper:returned(4 if image.label=='gold' else 0,NAME)
        elif a==image.compare_helper:returned(0,1)
        elif a==POSITION:
            if uc.reg_read(UC_X86_REG_ECX)!=STREAM:raise RuntimeError('foreign position stream')
            returned(0,position if reading else len(output))
        elif a in (WRITE,READ):
            sp=uc.reg_read(UC_X86_REG_ESP);pointer,n=word(sp+4),word(sp+8)
            if uc.reg_read(UC_X86_REG_ECX)!=STREAM or n>64:raise RuntimeError('stream ownership/bound')
            if a==WRITE:
                if reading or len(output)+n>64:raise RuntimeError('writer mode/bound')
                output.extend(uc.mem_read(pointer,n));returned(8)
            else:
                if not reading or word(sp+12)!=0 or not owned(pointer,n) or position+n>len(input_wire):raise RuntimeError('reader ownership/bound')
                uc.mem_write(pointer,input_wire[position:position+n]);position+=n;returned(12,n)
        elif not (image.base<=a<image.base+image.size or base<=a<base+fixture.OPTIONAL_HEADER.SizeOfImage):raise RuntimeError('foreign execution')
    def writes(uc,access,a,n,value,data):
        if not (a==0 and n==4 or owned(a,n)):raise RuntimeError('write outside owned state '+hex(a))
    u.hook_add(UC_HOOK_CODE,code);u.hook_add(UC_HOOK_MEM_WRITE,writes)
    preserved={UC_X86_REG_EBX:0x12345678,UC_X86_REG_EBP:0x23456789,UC_X86_REG_ESI:0x34567890,UC_X86_REG_EDI:0x456789ab}
    def call(entry,args=(),this=0,cdecl=False):
        sp=STACK+0xf000;words(sp,STOP,*args)
        for k,v in {**preserved,UC_X86_REG_ESP:sp,UC_X86_REG_ECX:this}.items():u.reg_write(k,v)
        u.emu_start(entry,STOP+1,count=300000,timeout=2000000)
        if (u.reg_read(UC_X86_REG_EIP)!=STOP or u.reg_read(UC_X86_REG_ESP)!=sp+4+(0 if cdecl else len(args)*4)
            or word(0)!=0 or any(u.reg_read(k)!=v for k,v in preserved.items())):raise RuntimeError('native call ABI/SEH '+str([hex(x) for x in last]))
        return u.reg_read(UC_X86_REG_EAX)
    def construct(address,host):
        if image.label=='gold':result=call(CONSTRUCTORS[image.label],(address,host),N)
        else:result=call(CONSTRUCTORS[image.label],(7,host,N),address)
        if result!=address or word(address)!=table or word(address+4)!=0x70007 or word(address+8)!=host or u.mem_read(address+12,1)!=b'\x01' or word(address+0x18)!=0:raise RuntimeError('original constructor result')
    try:
        construct(O,cid);original=bytes(u.mem_read(O,0x20))
        klass=call(methods[1],this=O)
        if klass!=0x8bf00ae9:raise RuntimeError('original class identifier')
        words(STREAM,VT);words(VT+0x14,POSITION);words(VT+0x28,WRITE);words(VT+0x20,READ)
        u.mem_write(NAME,b'class SyntheticEvent\0')
        if image.label=='gold':words(0xebf574,REFLECTION)
        words(VISITOR,image.tables[native.WRITER][0],0,STREAM,0)
        call(methods[3],(VISITOR,0),O)
        expected=struct.pack('<IIB',0x70007,cid,1)+b'Str_'+struct.pack('<I',0)
        if bytes(output)!=expected or bytes(u.mem_read(O,0x20))!=original or word(VISITOR+12):raise RuntimeError('original writer payload or source ownership')
        # A second independently constructed object receives the original bytes.
        construct(D,0);reading=True;input_wire=bytes(output)
        words(VISITOR,image.tables[native.READER][0],1,STREAM,0)
        call(methods[3],(VISITOR,0),D)
        if position!=len(input_wire) or word(D+4)!=0x70007 or word(D+8)!=cid or u.mem_read(D+12,1)!=b'\x01' or word(D+0x18)!=0 or word(VISITOR+12):raise RuntimeError('original reader fields/boundary')
        queues=(T+0x1c,T+0x3c) if image.label=='gold' else (T+0x18,T+0x2c)
        map_offset,count_offset=(0x10,0x1c) if image.label=='gold' else (4,0x10)
        words(T+(0x18 if image.label=='gold' else 0x14),T+0x80)
        for i,queue in enumerate(queues):words(queue+map_offset,MAP+i*0x40,4,0,0)
        call(send_thunk,(SEND[image.label],T,D),cdecl=True)
        if locks!=['EnterCriticalSection','LeaveCriticalSection'] or word(queues[1]+count_offset)!=1 or word(queues[0]+count_offset)!=0:raise RuntimeError('original asynchronous local queue')
        block=word(MAP+0x40);clone=word(block)
        if clone in (O,D) or clone not in allocs or word(clone)!=table or bytes(u.mem_read(clone+4,0x1c))!=bytes(u.mem_read(D+4,0x1c)):raise RuntimeError('original owned start clone')
        call(methods[0],(1,),clone);del allocs[block]
        call(methods[0],(0,),D);call(methods[0],(0,),O)
        if allocs:raise RuntimeError('owned allocations leaked')
        return struct.pack('<I',klass)+expected
    except Exception as exc:raise RuntimeError(image.label+' '+str(exc)+' near '+str([hex(x) for x in last])) from exc

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    for label in native.PROFILES:parser.add_argument('--'+label.replace('_','-'),type=Path,required=True)
    parser.add_argument('--thunk-executable',type=Path,required=True);parser.add_argument('output',type=Path)
    args=parser.parse_args();root=Path(__file__).resolve().parents[2];out=args.output.resolve()
    if not out.is_relative_to((root/'work').resolve()) or out.exists() or not out.parent.is_dir():parser.error('Use a fresh output under repository work')
    raw=args.thunk_executable.read_bytes();fixture=pefile.PE(data=raw)
    if fixture.FILE_HEADER.Machine!=0x14c or fixture.OPTIONAL_HEADER.ImageBase!=0x18000000 or fixture.OPTIONAL_HEADER.SizeOfImage>16*1024*1024:raise ValueError('Expected bounded fixed x86 participant thunk fixture')
    results={};wires={}
    for label in native.PROFILES:
        path=getattr(args,label);image=native.Image(label,path,types=('CGameStartEvent',));pe=pefile.PE(str(path))
        imports=[(x.name.decode() if x.name else '',x.address) for library in pe.DIRECTORY_ENTRY_IMPORT for x in library.imports];del pe
        results[label]=0
        for cid in (0,42,0x7fffffff):
            wire=execute(image,imports,fixture,cid)
            if cid in wires and wires[cid]!=wire:raise RuntimeError('cross-edition original wire differs')
            wires[cid]=wire;results[label]+=1
        print(json.dumps({'profile':label,'successful_native_cases':results[label]}),flush=True);del image;gc.collect()
    result={'format':'native-fresh-start-oracle-v1','profiles':results,'successful_native_cases':sum(results.values()),
        'fingerprints':{label:value[0] for label,value in native.PROFILES.items()},
        'synthetic_wire_fixtures':{str(cid):wire.hex() for cid,wire in wires.items()},
        'native_construction_visitors_clone_local_enqueue_destructors':True,'synthetic_heap_locks_stream_and_reflection':True,
        'compiled_local_send_thunk_sha256':hashlib.sha256(raw).hexdigest(),'native_factory_executed':False,
        'native_queue_consumer_executed':False,'game_process_started':False,'crossplay_qualified':False}
    with out.open('x') as stream:json.dump(result,stream,indent=2)
    print(json.dumps(result))
if __name__=='__main__':main()
