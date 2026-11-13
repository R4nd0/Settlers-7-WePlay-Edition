"""Execute the compiled passive bridge and original Open methods in a bounded CPU.

Synthetic file-system children and Win32 error APIs are modeled. No game starts,
no resource is read, and no observer is installed. This qualifies forwarding ABI,
not mounted-content agreement, actual file observation or a mixed world.
"""
import argparse
import gc
import hashlib
import json
from pathlib import Path
import struct

import pefile
from unicorn import Uc, UC_ARCH_X86, UC_MODE_32, UC_HOOK_CODE, UC_HOOK_MEM_WRITE
import unicorn.x86_const as x
from emulate_event_payloads import Image, PROFILES

PINS = {
    'gold': (0x9bb470,292,'21babca003fd5e3bed5664b7951991d5d24bc39626eac9734f7237d261790c65',0xf51450),
    'ubisoft': (0x56be10,363,'570ea214d23b35e175caf2ac7647a91e289744e719e6e03c734c7abbe3d1860c',0x11365e4),
    'steam_analysis': (0x56b900,363,'83dfe220ce47b0856efd54ee7e142ea32c27534202b9d35091a0b9c319a0ff3c',0x1134adc),
}
BASE, STACK = 0x20000000, 0x30000000
COLLECTION, VECTOR, CHILD, TABLE, PATH = (BASE+n for n in (0x1000,0x2000,0x3000,0x4000,0x7000))
INFO, OPEN, GET_ERROR, SET_ERROR, STOP, INIT = (BASE+n for n in (0x8100,0x8110,0x8120,0x8130,0x8200,0x8300))
CASES = {'normal':(0xffffffff,True,False,BASE+0x6000),
         'fallback':(0x113,False,False,BASE+0x6000),
         'missing':(0x10113,False,False,0),
         'directory':(0x10113,True,True,0)}


def audit(image, fixture, kind, *, wrapped, observe):
    entry,length,pin,table=PINS[image.label]
    if hashlib.sha256(image.mapped[entry-image.base:entry-image.base+length]).hexdigest()!=pin:
        raise ValueError('Original Open method differs')
    if struct.unpack_from('<I',image.mapped,table-image.base+20)[0]!=entry:
        raise ValueError('Original Open vtable slot differs')
    mode,exists,directory,file_result=CASES[kind]
    u=Uc(UC_ARCH_X86,UC_MODE_32)
    u.mem_map(image.base,(image.size+4095)&~4095);u.mem_write(image.base,image.mapped)
    base,size=fixture.OPTIONAL_HEADER.ImageBase,fixture.OPTIONAL_HEADER.SizeOfImage
    u.mem_map(base,(size+4095)&~4095);u.mem_write(base,fixture.get_memory_mapped_image())
    u.mem_map(0,4096);u.mem_map(BASE,0x10000);u.mem_map(STACK,0x10000)
    exports={e.name:base+e.address for e in fixture.DIRECTORY_ENTRY_EXPORT.symbols}
    for tag,address in ((b'GetLastError',GET_ERROR),(b'SetLastError',SET_ERROR)):
        imports=[i.address for lib in fixture.DIRECTORY_ENTRY_IMPORT for i in lib.imports if i.name==tag]
        if len(imports)!=1:raise ValueError('Expected one modeled error import')
        u.mem_write(imports[0],struct.pack('<I',address))
    mutable=[(base+s.VirtualAddress,base+s.VirtualAddress+s.Misc_VirtualSize)
             for s in fixture.sections if s.Characteristics&0x80000000]
    gold=image.label=='gold';calls=[];last_error=0x7777;incoming=[];phase='bind'

    def words(at,*values):u.mem_write(at,struct.pack('<'+'I'*len(values),*values))
    def word(at):return struct.unpack('<I',u.mem_read(at,4))[0]
    words(COLLECTION,table);words(COLLECTION+(12 if gold else 8),VECTOR,VECTOR+4,VECTOR+4)
    words(COLLECTION+(24 if gold else 20),0x1f,0)
    words(VECTOR,CHILD);words(CHILD,TABLE,1);words(TABLE+16,INFO,OPEN)
    u.mem_write(PATH,b'Script/global/start.lua\0')
    registers=[getattr(x,'UC_X86_REG_'+n) for n in ('EAX','ECX','EDX','EBX','ESI','EDI','EBP','EFLAGS')]
    fp_registers=[getattr(x,'UC_X86_REG_'+n) for n in ('FPCW','FPSW','FPTAG','MXCSR')]
    fp_registers += [getattr(x,'UC_X86_REG_FP'+str(i)) for i in range(8)]
    fp_registers += [getattr(x,'UC_X86_REG_XMM'+str(i)) for i in range(8)]
    def machine():return [u.reg_read(k) for k in registers+fp_registers]
    def returned(count,value):
        sp=u.reg_read(x.UC_X86_REG_ESP)
        u.reg_write(x.UC_X86_REG_EAX,value);u.reg_write(x.UC_X86_REG_EIP,word(sp));u.reg_write(x.UC_X86_REG_ESP,sp+4+count)
    def code(uc,at,ignored,data):
        nonlocal last_error
        if at==STOP:uc.emu_stop()
        elif at==GET_ERROR:returned(0,last_error)
        elif at==SET_ERROR:
            last_error=word(uc.reg_read(x.UC_X86_REG_ESP)+4);returned(4,0)
        elif at==entry:
            sp=uc.reg_read(x.UC_X86_REG_ESP)
            if (phase!='native' or uc.reg_read(x.UC_X86_REG_ECX)!=COLLECTION or word(sp)!=STOP
                    or word(sp+4)!=PATH or word(sp+8)!=mode or word(0)):
                raise RuntimeError('Original entry ABI/SEH differs')
            incoming.append({'machine':machine(),'error':last_error})
        elif at in (INFO,OPEN):
            sp=uc.reg_read(x.UC_X86_REG_ESP)
            if uc.reg_read(x.UC_X86_REG_ECX)!=CHILD:raise RuntimeError('Wrong child filesystem')
            if at==INFO:
                output,path,flags=word(sp+4),word(sp+8),word(sp+12)
                if path!=PATH or flags or not STACK<=output<=STACK+0x10000-16:
                    raise RuntimeError('Original file-info ABI differs')
                uc.mem_write(output,bytes(12)+bytes((int(exists),int(directory),0,0)))
                calls.append(('info',flags));returned(12,output)
            else:
                path,flags=word(sp+4),word(sp+8)
                if path!=PATH or flags not in (0x113,0x10113):raise RuntimeError('Original open argument differs')
                calls.append(('open',flags));last_error=0x4444;returned(8,file_result)
        elif not (entry<=at<entry+length or base<=at<base+size or INIT<=at<INIT+6):
            raise RuntimeError('Executed foreign code or an unmodeled original error path')
    def write(uc,access,at,count,value,data):
        if not (at==0 and count==4 or STACK<=at<=at+count<=STACK+0x10000
                or any(lo<=at<=at+count<=hi for lo,hi in mutable)):
            raise RuntimeError('Write outside owned stack/fixture state')
    u.hook_add(UC_HOOK_CODE,code);u.hook_add(UC_HOOK_MEM_WRITE,write)
    sp=STACK+0xf000
    def fixture_call(name,args):
        words(sp,STOP,*args);u.reg_write(x.UC_X86_REG_ESP,sp)
        u.emu_start(exports[name],STOP+1,count=100000,timeout=5000000)
        if u.reg_read(x.UC_X86_REG_EIP)!=STOP or u.reg_read(x.UC_X86_REG_ESP)!=sp+4 or word(0):
            raise RuntimeError('Compiled fixture cdecl ABI/SEH differs')
        return u.reg_read(x.UC_X86_REG_EAX)
    fixture_call(b'WePlayResourceBridgeBind',(entry,int(observe)))
    # Native x87 values and distinct SSE lanes expose observer clobbers.
    u.mem_write(INIT,bytes.fromhex('dbe3d9e8d9eb'))
    words(sp,STOP);u.reg_write(x.UC_X86_REG_ESP,sp)
    u.emu_start(INIT,INIT+6,count=3)
    u.reg_write(x.UC_X86_REG_MXCSR,0x5f80)
    for i in range(8):u.reg_write(getattr(x,'UC_X86_REG_XMM'+str(i)),int.from_bytes(bytes([i+1])*16,'little'))
    phase='native';last_error=0x7777;words(sp,STOP,PATH,mode)
    initial={'EAX':0x11223344,'ECX':COLLECTION,'EDX':0x22334455,'EBX':0x33445566,
             'ESI':0x44556677,'EDI':0x55667788,'EBP':0x66778899,'EFLAGS':0x247,'ESP':sp}
    for name,value in initial.items():u.reg_write(getattr(x,'UC_X86_REG_'+name),value)
    expected_incoming={'machine':machine(),'error':last_error}
    target=exports[b'WePlayResourceOpenForward'] if wrapped else entry
    u.emu_start(target,STOP+1,count=100000,timeout=5000000)
    if (u.reg_read(x.UC_X86_REG_EIP)!=STOP or u.reg_read(x.UC_X86_REG_ESP)!=sp+12 or word(0)
            or incoming!=[expected_incoming] or u.reg_read(x.UC_X86_REG_EAX)!=file_result):
        raise RuntimeError('Native forwarding entry/result/stack/FP differs')
    if any(u.reg_read(getattr(x,'UC_X86_REG_'+k))!=initial[k] for k in ('EBX','ESI','EDI','EBP')):
        raise RuntimeError('Native nonvolatile registers differ')
    result={'calls':calls,'machine':machine(),'error':last_error,'result':file_result}
    if fixture_call(b'WePlayResourceBridgeObserved',(COLLECTION,PATH,mode,int(observe)))!=1:
        raise RuntimeError('Compiled observer argument/count/fault check differs')
    return result


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    for label in PROFILES:parser.add_argument('--'+label.replace('_','-'),type=Path,required=True)
    parser.add_argument('--thunk-executable',type=Path,required=True);parser.add_argument('output',type=Path)
    args=parser.parse_args();root=Path(__file__).resolve().parents[2];out=args.output.resolve()
    if not out.is_relative_to((root/'work').resolve()) or out.exists() or not out.parent.is_dir():
        parser.error('Use a new output under repository work')
    raw=args.thunk_executable.read_bytes();fixture=pefile.PE(data=raw)
    if (fixture.FILE_HEADER.Machine!=0x14c or fixture.OPTIONAL_HEADER.ImageBase!=0x24000000
            or fixture.OPTIONAL_HEADER.SizeOfImage>4*1024*1024):
        raise ValueError('Expected bounded fixed x86 bridge fixture')
    result={}
    for label in PROFILES:
        image=Image(label,getattr(args,label),types=());cases={}
        for kind in CASES:
            baseline=audit(image,fixture,kind,wrapped=False,observe=False)
            for observe in (False,True):
                if audit(image,fixture,kind,wrapped=True,observe=observe)!=baseline:
                    raise RuntimeError('Observation changed original behavior')
            cases[kind]={'direct_original_baseline':True,'observed_and_unobserved_wrappers_equal':True}
        result[label]=cases;print(json.dumps({'profile':label,'original_and_compiled_cases':12,'passed':True}),flush=True)
        del image;gc.collect()
    report={'format':'native-resource-open-forwarding-v1','profiles':result,'cases':36,
            'compiled_fixture_sha256':hashlib.sha256(raw).hexdigest(),'child_filesystems_and_error_apis_modeled':True,
            'observer_installed_in_game':False,'resources_read':False,'world_permission':False}
    out.write_text(json.dumps(report,indent=2)+'\n');print('PASS original Open forwarding in all three images; no game launched.')


if __name__=='__main__':main()
