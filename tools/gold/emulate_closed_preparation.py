"""Execute guarded original History closed-preparation setters offline.

Synthetic rows and VirtualQuery are modeled. The compiled adapter calls the
original Name, NetworkSlot, Type and empty-string assignment methods. No allocator,
game process, network, world, turn, NEO or save result is supplied.
"""
import argparse
import gc
import hashlib
import json
from pathlib import Path
import struct
import pefile
from unicorn import Uc, UC_ARCH_X86, UC_MODE_32, UC_HOOK_CODE, UC_HOOK_MEM_WRITE
from unicorn.x86_const import (UC_X86_REG_EAX, UC_X86_REG_EBX, UC_X86_REG_EBP,
    UC_X86_REG_ESI, UC_X86_REG_EDI, UC_X86_REG_ESP, UC_X86_REG_EIP)
from emulate_event_payloads import Image, PROFILES
from verify_codec_guards import check_image

S, STACK = 0x20000000, 0x30000000
STOP, QUERY, ROW, TEXT = S, S+16, S+0x1000, S+0x3000
SIZE = 0x5a8
REGIONS = {'ubisoft': ((0x546450,25), (0x5862e0,13), (0x4e29f0,0x70), (0x546480,13)),
           'steam_analysis': ((0x545aa0,25), (0x585da0,13), (0x4e2880,0x70), (0x545ad0,13))}


def run_case(image, fixture, edition, target, *, name=b'Former AI', assigned=0xffffffff,
             profile=29, capacity=128, null=False, readonly=False, state=1, kind=1, expected=True):
    u = Uc(UC_ARCH_X86, UC_MODE_32)
    u.mem_map(image.base, (image.size+4095)&~4095)
    u.mem_write(image.base, image.mapped)
    base, size = fixture.OPTIONAL_HEADER.ImageBase, fixture.OPTIONAL_HEADER.SizeOfImage
    u.mem_map(base, (size+4095)&~4095)
    u.mem_write(base, fixture.get_memory_mapped_image())
    u.mem_map(S, 0x4000)
    u.mem_map(STACK, 65536)
    queries = [imp.address for library in fixture.DIRECTORY_ENTRY_IMPORT for imp in library.imports if imp.name == b'VirtualQuery']
    if len(queries) != 1:
        raise ValueError('Expected compiled VirtualQuery import')
    u.mem_write(queries[0], struct.pack('<I', QUERY))
    exports = {e.name: base+e.address for e in fixture.DIRECTORY_ENTRY_EXPORT.symbols}
    entry = exports[b'WePlayClosedPreparationApplyOracle']
    row = bytearray(b'\xa5'*SIZE)
    struct.pack_into('<I', row, 0x18, profile)
    struct.pack_into('<4I', row, 0x1c, 0 if null else TEXT, 0 if null else capacity, len(name), 7)
    struct.pack_into('<I', row, 0x5c, assigned)
    struct.pack_into('<2I', row, 0x60, state, kind)
    text = name+b'\0'+b'\x6d'*(127-len(name))
    u.mem_write(ROW, bytes(row));u.mem_write(TEXT, text)
    calls = []

    def word(at):
        return struct.unpack('<I', u.mem_read(at, 4))[0]

    def code(uc, at, length, data):
        if at == STOP:
            uc.emu_stop()
        elif at == QUERY:
            sp = uc.reg_read(UC_X86_REG_ESP)
            pointer, output, requested = (word(sp+4*i) for i in range(1, 4))
            if requested != 28 or not STACK <= output <= STACK+65536-28:
                raise RuntimeError('Unowned VirtualQuery output')
            region = None
            if image.base <= pointer < image.base+image.size:
                region = (image.base, image.size, 0x20)
            elif S <= pointer < S+0x4000:
                region = (S, 0x4000, 2 if readonly else 4)
            elif STACK <= pointer < STACK+65536:
                region = (STACK, 65536, 4)
            if region:
                address, extent, protection = region
                uc.mem_write(output, struct.pack('<7I', address, address, protection, extent, 0x1000, protection, 0x20000))
            uc.reg_write(UC_X86_REG_EAX, 28 if region else 0)
            uc.reg_write(UC_X86_REG_EIP, word(sp));uc.reg_write(UC_X86_REG_ESP, sp+16)
        elif image.base <= at < image.base+image.size:
            if not any(begin <= at and at+length <= begin+count for begin,count in REGIONS[image.label]):
                raise RuntimeError('Unfingerprinted original instruction executed')
            if at in (REGIONS[image.label][0][0], REGIONS[image.label][1][0], REGIONS[image.label][3][0]):
                calls.append(at)
        elif not base <= at < base+size:
            raise RuntimeError('Foreign instruction executed')

    def write(uc, access, at, count, value, data):
        if STACK <= at <= at+count <= STACK+65536:
            return
        if expected and not readonly and (ROW+0x1c <= at <= at+count <= ROW+0x2c
                or ROW+0x5c <= at <= at+count <= ROW+0x60 or ROW+0x64 <= at <= at+count <= ROW+0x68
                or at == TEXT and count == 1):
            return
        raise RuntimeError('Original setter wrote outside its three fields/string terminator')

    u.hook_add(UC_HOOK_CODE, code);u.hook_add(UC_HOOK_MEM_WRITE, write)
    sp = STACK+0xf000
    u.mem_write(sp, struct.pack('<4I', STOP, edition, ROW, target))
    kept = {UC_X86_REG_EBX: 0x12345678, UC_X86_REG_EBP: 0x23456789,
            UC_X86_REG_ESI: 0x34567890, UC_X86_REG_EDI: 0x456789ab}
    for reg,value in {**kept,UC_X86_REG_ESP:sp}.items():
        u.reg_write(reg,value)
    u.emu_start(entry, STOP+1, count=1000000, timeout=5000000)
    if (u.reg_read(UC_X86_REG_EIP) != STOP or u.reg_read(UC_X86_REG_ESP) != sp+4
            or u.reg_read(UC_X86_REG_EAX) != int(expected)
            or any(u.reg_read(reg) != value for reg,value in kept.items())):
        raise RuntimeError('Original setter result/ABI mismatch')
    changed = expected and (assigned != target or name or kind != 4)
    wanted = bytearray(row);wanted_text = bytearray(text)
    if changed:
        struct.pack_into('<I', wanted, 0x24, 0);struct.pack_into('<I', wanted, 0x28, 0)
        struct.pack_into('<I', wanted, 0x5c, target)
        struct.pack_into('<I', wanted, 0x64, 4)
        if not null:
            wanted_text[0] = 0
    expected_calls = [REGIONS[image.label][i][0] for i in (0,1,3)] if changed else []
    if bytes(u.mem_read(ROW,SIZE)) != bytes(wanted) or bytes(u.mem_read(TEXT,128)) != bytes(wanted_text) or calls != expected_calls:
        raise RuntimeError('Original setter changed other state or bypassed native calls')
    return len(calls)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for label in REGIONS:
        parser.add_argument('--'+label.replace('_','-'), type=Path, required=True)
    parser.add_argument('--thunk-executable', type=Path, required=True)
    parser.add_argument('output',type=Path)
    args = parser.parse_args();root = Path(__file__).resolve().parents[2];out=args.output.resolve()
    if not out.is_relative_to((root/'work').resolve()) or out.exists() or not out.parent.is_dir():
        parser.error('Use a fresh output under repository work')
    raw=args.thunk_executable.read_bytes();fixture=pefile.PE(data=raw)
    if (fixture.FILE_HEADER.Machine!=0x14c or fixture.OPTIONAL_HEADER.ImageBase!=0x24000000
            or fixture.OPTIONAL_HEADER.SizeOfImage>4*1024*1024):
        raise ValueError('Expected the bounded fixed x86 fixture')
    profiles={}
    for label in REGIONS:
        edition=2 if label=='ubisoft' else 3
        image=Image(label,getattr(args,label),types=())
        guards=check_image(image,fixture,edition,oracle=b'WePlayClosedPreparationMethodsOracle',methods=REGIONS[label])
        calls=0;cases=0
        for slot in (3,4):
            for options in ({},{'name':b'x'*64},{'name':b'','null':True},{'name':b'','assigned':slot},
                            {'name':b'','assigned':slot,'kind':4}):
                calls+=run_case(image,fixture,edition,slot,**options);cases+=1
        for slot in (0,1,2,5,25,0xffffffff):
            calls+=run_case(image,fixture,edition,slot,expected=False);cases+=1
        for options in ({'assigned':1},{'profile':0},{'profile':0xffffffff},{'name':b'x'*65},
                        {'capacity':1},{'readonly':True},{'null':True},{'state':3},{'kind':2}):
            calls+=run_case(image,fixture,edition,3,expected=False,**options);cases+=1
        profiles[label]={'cases':cases,'integrity_checks':guards,'original_setter_calls':calls,
                         'only_name_network_slot_and_type_changed':True,'original_empty_string_helper_executed':True,
                         'stack_and_nonvolatile_registers_preserved':True,'no_original_allocation_or_game_call':True}
        print(json.dumps({'profile':label,**profiles[label]}),flush=True)
        del image;gc.collect()
    receipt={'format':'closed-preparation-original-setter-audit-v2','profiles':profiles,
             'fixture_sha256':hashlib.sha256(raw).hexdigest(),
             'fingerprints':{label:PROFILES[label][0] for label in REGIONS},
             'virtual_query_and_rows_modeled':True,'live_projection_qualified':False,
             'game_process_started':False,'shared_world_started':False,'gold_neo_qualified':False,
             'mixed_save_coldreload_qualified':False}
    with out.open('x',encoding='utf-8') as stream:
        json.dump(receipt,stream,indent=2)


if __name__=='__main__':
    main()
