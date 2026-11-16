"""Bounded x86 execution of start-component fixtures, never original game code."""
import struct

from unicorn import Uc,UC_ARCH_X86,UC_MODE_32,UC_HOOK_CODE
from unicorn.x86_const import (UC_X86_REG_EAX,UC_X86_REG_EBX,UC_X86_REG_ESI,
    UC_X86_REG_EDI,UC_X86_REG_EBP,UC_X86_REG_ESP,UC_X86_REG_EIP)

STACK=0x30000000
STUBS=0x41000000
STOP=STUBS+0xf000
CONTINUATIONS=(0xa4e4d9,0xa35f6a)


def run(fixture,export,arguments,*,image=None):
    base,size=fixture.OPTIONAL_HEADER.ImageBase,fixture.OPTIONAL_HEADER.SizeOfImage
    if fixture.FILE_HEADER.Machine!=0x14c or base!=0x24000000 or not 0<size<=4*1024*1024:
        raise ValueError('Expected bounded fixed x86 fixture')
    entries=[base+item.address for item in fixture.DIRECTORY_ENTRY_EXPORT.symbols if item.name==export]
    if len(entries)!=1:raise ValueError('Missing or ambiguous fixture export: '+export.decode('ascii'))
    executable=[]
    for section in fixture.sections:
        if not section.Characteristics&0x20000000:continue
        if not 0<=section.VirtualAddress<size or not 0<section.Misc_VirtualSize<=size-section.VirtualAddress:
            raise ValueError('Executable section outside bounded fixture image')
        executable.append((base+section.VirtualAddress,base+section.VirtualAddress+section.Misc_VirtualSize))
    def own_code(address):return any(first<=address<last for first,last in executable)
    if not own_code(entries[0]):raise ValueError('Fixture export outside executable sections')
    u=Uc(UC_ARCH_X86,UC_MODE_32)
    u.mem_map(base,(size+4095)&~4095);u.mem_write(base,fixture.get_memory_mapped_image())
    for address in (STACK,STUBS):u.mem_map(address,0x10000)
    if image is not None:
        u.mem_map(image.base,(image.size+4095)&~4095);u.mem_write(image.base,image.mapped)
    else:
        for address in (0xa30000,0xa40000,0xeb0000,0x10f0000):u.mem_map(address,0x10000)
    u.mem_map(0,0x1000)
    stubs={};last_error=0
    for library in getattr(fixture,'DIRECTORY_ENTRY_IMPORT',()):
        for item in library.imports:
            address=STUBS+0x100+16*len(stubs)
            if address>=STOP:raise ValueError('Too many modeled imports')
            stubs[address]=item.name.decode('ascii') if item.name else str(item.ordinal)
            u.mem_write(item.address,struct.pack('<I',address))
    def word(address):return struct.unpack('<I',u.mem_read(address,4))[0]
    def mapped(address,length):
        return 0<length<=1024*1024 and any(first<=address<=address+length-1<=last
                                          for first,last,_ in u.mem_regions())
    def code(uc,address,length,data):
        nonlocal last_error
        if address==STOP:uc.emu_stop();return
        if address not in stubs:
            if own_code(address):return
            if image is None and address in CONTINUATIONS:
                # ABI fixtures replace this exact continuation with a jump back
                # into their own compiled code; no original instruction runs.
                jump=bytes(uc.mem_read(address,5))
                target=(address+5+struct.unpack('<i',jump[1:])[0])&0xffffffff
                if jump[0]==0xe9 and own_code(target):return
            raise RuntimeError('Original game or foreign code execution excluded')
        name=stubs[address];sp=uc.reg_read(UC_X86_REG_ESP);value=0;pop=0
        if name=='GetLastError':value=last_error
        elif name=='SetLastError':last_error=word(sp+4);pop=4
        elif name=='GetCurrentThreadId':value=0x4042
        elif name=='GetCurrentProcess':value=0xffffffff
        elif name=='FlushInstructionCache':
            if word(sp+4)!=0xffffffff or not mapped(word(sp+8),word(sp+12)):
                raise RuntimeError('Invalid modeled instruction-cache range')
            value=1;pop=12
        elif name=='VirtualProtect':
            address,count,protection,target=(word(sp+4*i) for i in range(1,5))
            if not mapped(address,count) or protection not in (4,0x40) or not STACK<=target<=STACK+0x10000-4:
                raise RuntimeError('Invalid modeled protection range')
            uc.mem_write(target,struct.pack('<I',4));value=1;pop=16
        elif name=='VirtualQuery':
            address,target,count=(word(sp+4*i) for i in range(1,4))
            if count!=28 or not STACK<=target<=STACK+0x10000-28:
                raise RuntimeError('Invalid modeled memory-query output')
            for first,last,_ in uc.mem_regions():
                if first<=address<=last:
                    uc.mem_write(target,struct.pack('<7I',first,first,4,last-first+1,0x1000,4,0x20000))
                    value=28;break
            if not value:last_error=87
            pop=12
        else:raise RuntimeError('Unexpected emulated import '+name)
        uc.reg_write(UC_X86_REG_EAX,value);uc.reg_write(UC_X86_REG_ESP,sp+4+pop)
        uc.reg_write(UC_X86_REG_EIP,word(sp))
    u.hook_add(UC_HOOK_CODE,code)
    stack=STACK+0xf000
    u.mem_write(stack,struct.pack('<'+'I'*(1+len(arguments)),STOP,*arguments))
    kept={UC_X86_REG_EBX:0x11111111,UC_X86_REG_ESI:0x22222222,
          UC_X86_REG_EDI:0x33333333,UC_X86_REG_EBP:0x44444444}
    for register,value in {**kept,UC_X86_REG_ESP:stack}.items():u.reg_write(register,value)
    u.emu_start(entries[0],STOP,count=1000000,timeout=3000000)
    if (u.reg_read(UC_X86_REG_EIP)!=STOP or u.reg_read(UC_X86_REG_EAX)!=1
            or u.reg_read(UC_X86_REG_ESP)!=stack+4
            or any(u.reg_read(register)!=value for register,value in kept.items())):
        raise RuntimeError('Compiled fixture result/ABI mismatch: '+export.decode('ascii')+' '+str(arguments))
    print('PASS',export.decode('ascii'),arguments,flush=True)
