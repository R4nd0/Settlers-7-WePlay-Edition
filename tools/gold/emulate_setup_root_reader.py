"""Bounded original setup write/read/write audit on synthetic baseline objects.

No game is launched. Original root, row and binary visitor methods execute;
CRT memory and stream boundaries are modeled. Empty victory/sector vectors,
empty player containers and existing nonempty colour symbols are deliberate.
This is not a live setup receiver, content permission or save-load test.
"""
import argparse
import gc
import hashlib
import json
from pathlib import Path
import struct

from emulate_event_payloads import Image, PROFILES, WRITER, READER
from emulate_active_players import LAYOUTS, PREPARATION_WRITERS, SETUP_ROOT_WRITERS
from setup_rule_vectors import METHODS

BASE, STACK = 0x20000000, 0x30000000
VISITOR, STREAM, STREAM_TABLE, STOP = BASE+0x2000, BASE+0x6000, BASE+0x6100, BASE+0x4000
POSITION, WRITE, READ, COPY, SAFE_COPY, FILL = (BASE+x for x in (0x6200,0x6210,0x6220,0x6230,0x6240,0x6250))
ROOT, CONFIG, ROWS, ARENA, HEAP = (BASE+x for x in (0x8000,0x9000,0x10000,0x20000,0x50000))


class InputRejected(RuntimeError):
    pass


def audit(image, version, input_case='complete'):
    from unicorn import Uc, UC_ARCH_X86, UC_MODE_32, UC_HOOK_CODE, UC_HOOK_MEM_WRITE, UC_HOOK_MEM_READ
    from unicorn.x86_const import (UC_X86_REG_EAX, UC_X86_REG_EBX, UC_X86_REG_ECX,
        UC_X86_REG_ESI, UC_X86_REG_EDI, UC_X86_REG_EBP, UC_X86_REG_ESP, UC_X86_REG_EIP)
    if version not in (1,2) or input_case not in ('complete','truncated','trailing'):
        raise ValueError('Unsupported reader fixture')
    gold = image.label == 'gold'
    row_entry, stride, allocate, free = LAYOUTS[image.label]
    u = Uc(UC_ARCH_X86, UC_MODE_32)
    u.mem_map(image.base,(image.size+4095)&~4095);u.mem_write(image.base,image.mapped)
    u.mem_map(0,4096);u.mem_map(BASE,0x80000);u.mem_map(STACK,0x10000)
    cursor, allocation_count, phase = ARENA, 0, 'hash'
    allocations = {};output = bytearray();input_wire = b'';position = 0
    row_slots, preparation_versions = [], []
    completed = False

    def words(at,*values):
        u.mem_write(at,struct.pack('<'+'I'*len(values),*values))

    def word(at):
        return struct.unpack('<I',u.mem_read(at,4))[0]

    def owned(data):
        nonlocal cursor
        if not data or cursor+len(data)>HEAP:
            raise RuntimeError('Synthetic arena bound')
        at=cursor;u.mem_write(at,data);cursor+=(len(data)+15)&~15;return at

    def string(at,value):
        data=value.encode('ascii')+b'\0';pointer=owned(data)
        words(at,pointer,len(data),len(data)-1,0)

    def empty_list(at):
        head=owned(bytes(12));words(head,head,head);words(at,head,0)

    for number in range(25):
        at=ROWS+number*stride
        for off in ((0x4e8,0x4f4,0x56c) if gold else (0x4e0,0x4e8,0x554)):
            empty_list(at+off)
        words(at+0x2c,1)
    for off in ((0x9080,0x908c) if gold else (0x8dbc,0x8dc4)):
        empty_list(ROWS+off)
    symbols=owned(bytes(32));string(symbols,'');string(symbols+16,'Synthetic Color')
    array=owned(struct.pack('<2I',symbols,symbols+16))
    registry=owned(struct.pack('<4I',array,owned(bytes(12)),2,2))
    symbol_table=ROWS+(0x9024 if gold else 0x8d68)
    words(symbol_table+0x18,registry)
    words(ROOT,7);words(ROOT+(0x64 if gold else 0x58),0x2468ace)
    for off,value in ((4,'Synthetic Start'),(0x14,'Synthetic Trade'),(0x24,'Synthetic Tech')):
        string(ROOT+off,value)
    words(ROOT+(0x70 if gold else 0x64),CONFIG);words(CONFIG,ROWS,ROWS)
    words(STREAM,STREAM_TABLE);words(STREAM_TABLE+0x14,POSITION)
    words(STREAM_TABLE+0x28,WRITE);words(STREAM_TABLE+0x20,READ)
    for tag,address in image.crt_imports.items():
        words(address,allocate if tag==b'malloc' else free if tag==b'free' else COPY)
    # Model only explicit, fingerprinted CRT imports. No engine lookup,
    # serializer, container constructor or field operation is replaced.
    for tag,address in image.setup_reader_imports.items():
        words(address,allocate if tag==b'??2@YAPAXI@Z' else free if tag==b'??3@YAXPAX@Z'
              else FILL if tag==b'memset' else SAFE_COPY)

    def span(at,size):
        return (ROWS<=at<=at+size<=ROWS+0x9094 or ROOT<=at<=at+size<=ROOT+0xa0
            or ARENA<=at<=at+size<=cursor or STACK<=at<=at+size<=STACK+0x10000
            or image.base<=at<=at+size<=image.base+image.size
            or any(p<=at<=at+size<=p+n for p,n in allocations.items()))

    def writable_span(at,size):
        return (ROWS<=at<=at+size<=ROWS+0x9094 or ROOT<=at<=at+size<=ROOT+0xa0
            or ARENA<=at<=at+size<=cursor or STACK<=at<=at+size<=STACK+0x10000
            or any(p<=at<=at+size<=p+n for p,n in allocations.items()))

    def returned(count,value=0):
        sp=u.reg_read(UC_X86_REG_ESP);u.reg_write(UC_X86_REG_EAX,value)
        u.reg_write(UC_X86_REG_EIP,word(sp));u.reg_write(UC_X86_REG_ESP,sp+4+count)

    def code(uc,at,size,data):
        nonlocal completed,allocation_count,position
        if at==STOP:
            completed=True;uc.emu_stop()
        elif at==allocate:
            length=word(uc.reg_read(UC_X86_REG_ESP)+4)
            if not 1<=length<=512 or allocation_count>=180:
                raise RuntimeError('Bounded CRT allocation contract')
            pointer=HEAP+allocation_count*0x400;allocation_count+=1
            if pointer in allocations:raise RuntimeError('Live allocation overlap')
            allocations[pointer]=length;returned(0,pointer)
        elif at==free:
            pointer=word(uc.reg_read(UC_X86_REG_ESP)+4)
            if pointer not in allocations:raise RuntimeError('Unowned/double free')
            del allocations[pointer];returned(0)
        elif at in (COPY,SAFE_COPY,FILL):
            sp=uc.reg_read(UC_X86_REG_ESP);destination=word(sp+4)
            if at==SAFE_COPY:
                capacity,source,length=word(sp+8),word(sp+12),word(sp+16)
                if length>capacity:raise RuntimeError('Safe-copy capacity')
            else:
                source,length=word(sp+8),word(sp+12)
            if length>512 or length and (not writable_span(destination,length)
                    or phase!='read' and not (STACK<=destination<=destination+length<=STACK+0x10000
                        or any(p<=destination<=destination+length<=p+n for p,n in allocations.items()))
                    or at!=FILL and not span(source,length)):
                raise RuntimeError('CRT copy/fill ownership')
            if length:
                uc.mem_write(destination,bytes([source&255])*length if at==FILL else bytes(uc.mem_read(source,length)))
            returned(0,0 if at==SAFE_COPY else destination)
        elif at in (POSITION,WRITE,READ):
            if uc.reg_read(UC_X86_REG_ECX)!=STREAM:raise RuntimeError('Foreign stream')
            if at==POSITION:
                returned(0,position if phase=='read' else len(output));return
            sp=uc.reg_read(UC_X86_REG_ESP);pointer,length=word(sp+4),word(sp+8)
            if length>4096:raise RuntimeError('Stream operation bound')
            if at==WRITE:
                if phase not in ('write','rewrite') or not span(pointer,length) or len(output)+length>65536:
                    raise RuntimeError('Writer stream mode/ownership')
                output.extend(uc.mem_read(pointer,length));returned(8)
            else:
                if phase!='read' or word(sp+12) or not writable_span(pointer,length):
                    raise RuntimeError('Reader stream mode/ownership')
                if position+length>len(input_wire):raise InputRejected('Input exhausted')
                uc.mem_write(pointer,input_wire[position:position+length]);position+=length;returned(12,length)
        elif at==PREPARATION_WRITERS[image.label]:
            sp=uc.reg_read(UC_X86_REG_ESP)
            if ((word(sp+4) if gold else uc.reg_read(UC_X86_REG_ECX))!=ROWS
                    or (uc.reg_read(UC_X86_REG_EAX) if gold else word(sp+4))!=VISITOR
                    or word(sp+8)!=1 or preparation_versions):
                raise RuntimeError('Original root preparation dispatch')
            preparation_versions.append(1)
        elif at==row_entry:
            sp=uc.reg_read(UC_X86_REG_ESP);number=word(sp+(4 if gold else 8))&255
            if (number!=len(row_slots) or number>=25 or uc.reg_read(UC_X86_REG_ECX)!=ROWS
                    or (uc.reg_read(UC_X86_REG_EDI) if gold else word(sp+4))!=VISITOR
                    or word(sp+(8 if gold else 12))!=1):
                raise RuntimeError('Original row dispatch')
            row_slots.append(number)
        elif not image.base<=at<image.base+image.size:
            raise RuntimeError('Foreign code')

    def writes(uc,access,at,size,value,data):
        if version==2 and phase in ('write','rewrite') and at==ROOT+(0x64 if gold else 0x58) and size==4 and value==0x2468ace:
            return
        visitor_end=VISITOR+(16 if phase=='read' else 528)
        if not (at==0 and size==4 or STACK<=at<=at+size<=STACK+0x10000
                or VISITOR+12<=at<=at+size<=visitor_end
                or phase=='read' and writable_span(at,size)
                or any(p<=at<=at+size<=p+n for p,n in allocations.items())):
            raise RuntimeError('Write outside owned phase storage')

    def reads(uc,access,at,size,value,data):
        if at<4096 and (at!=0 or size!=4):raise RuntimeError('Unexpected null read')

    u.hook_add(UC_HOOK_CODE,code);u.hook_add(UC_HOOK_MEM_WRITE,writes);u.hook_add(UC_HOOK_MEM_READ,reads)
    sp=STACK+0xf000
    kept={UC_X86_REG_EBX:0x12345678,UC_X86_REG_EBP:0x23456789,
          UC_X86_REG_ESI:VISITOR if gold else 0x34567890,UC_X86_REG_EDI:0x456789ab}

    def run(entry,args,this,*,hash_call=False):
        nonlocal completed
        completed=False;words(sp,STOP,*args)
        registers={**kept,UC_X86_REG_ECX:this,UC_X86_REG_ESP:sp}
        if gold and not hash_call:registers[UC_X86_REG_EAX]=ROOT
        for reg,value in registers.items():u.reg_write(reg,value)
        u.emu_start(entry,STOP+1,count=6250000,timeout=15000000)
        if (not completed or u.reg_read(UC_X86_REG_EIP)!=STOP or word(0)
                or u.reg_read(UC_X86_REG_ESP)!=sp+4+4*len(args)
                or any(u.reg_read(k)!=v for k,v in kept.items())):
            raise RuntimeError('Original call ABI/SEH/instruction bound')
        return u.reg_read(UC_X86_REG_EAX)

    symbol_hash=run(METHODS[image.label][4][0],(),word(symbols+16),hash_call=True)
    words(symbols+28,symbol_hash)
    buckets=owned(bytes(32));table=owned(struct.pack('<2I',buckets,3))
    words(buckets+8*(symbol_hash&3),1,symbol_hash);words(symbol_table+0x1c,table)
    original_rows=bytes(u.mem_read(ROWS,0x9094));original_root=bytes(u.mem_read(ROOT,0xa0))
    original_arena=bytes(u.mem_read(ARENA,cursor-ARENA));canary=b'RdrGuard'
    args=(version,) if gold else (VISITOR,version)
    entry=SETUP_ROOT_WRITERS[image.label]
    phase='write';words(VISITOR,image.tables[WRITER][0],0,STREAM,0)
    u.mem_write(VISITOR-8,canary);u.mem_write(VISITOR+528,canary)
    run(entry,args,ROOT)
    if (allocations or allocation_count!=50 or row_slots!=list(range(25)) or preparation_versions!=[1]
            or word(VISITOR+12) or bytes(u.mem_read(ROWS,0x9094))!=original_rows
            or bytes(u.mem_read(ROOT,0xa0))!=original_root or bytes(u.mem_read(ARENA,cursor-ARENA))!=original_arena
            or bytes(u.mem_read(VISITOR-8,8))!=canary or bytes(u.mem_read(VISITOR+528,8))!=canary):
        raise RuntimeError('Initial original writer preservation')
    input_wire=bytes(output);expected_wire=input_wire
    if input_case=='truncated':input_wire=input_wire[:-1]
    elif input_case=='trailing':input_wire+=b'\0'
    phase='read';position=0;allocation_count=0;row_slots.clear();preparation_versions.clear()
    words(VISITOR,image.tables[READER][0],1,STREAM,0);u.mem_write(VISITOR+16,canary)
    words(ROOT,19);words(ROWS+stride+0x60,3)
    if version==2:words(ROOT+(0x64 if gold else 0x58),0xdeadbeef)
    try:
        run(entry,args,ROOT)
        if position!=len(input_wire):raise InputRejected('Trailing bytes')
    except InputRejected:
        if input_case=='complete':raise
        return {'input_case':input_case,'rejected':True,'world_permission':False}
    if input_case!='complete':raise RuntimeError('Malformed fixture accepted')
    normalized=bytearray(u.mem_read(ROWS,0x9094));vectors=[]
    for number in range(25):
        off=number*stride+(0x558 if gold else 0x544)
        begin,end,capacity=(word(ROWS+off+4*i) for i in range(3))
        if (allocations.get(begin)!=25 or end!=begin+25 or capacity!=end
                or bytes(u.mem_read(begin,25))!=bytes(25)):
            raise RuntimeError('Original version-1 migration vector')
        vectors.append(begin);normalized[off:off+12]=original_rows[off:off+12]
    if (bytes(normalized)!=original_rows or len(set(vectors))!=25 or set(vectors)!=set(allocations)
            or bytes(u.mem_read(ROOT,0xa0))!=original_root or bytes(u.mem_read(ARENA,cursor-ARENA))!=original_arena
            or bytes(u.mem_read(VISITOR-8,8))!=canary or bytes(u.mem_read(VISITOR+16,8))!=canary
            or row_slots!=list(range(25)) or preparation_versions!=[1] or word(VISITOR+12)):
        raise RuntimeError('Original reader target, ownership or canary')
    reader_allocations=allocation_count;retained=dict(allocations)
    migrated_rows=bytes(u.mem_read(ROWS,0x9094))
    migrated_vectors={p:bytes(u.mem_read(p,n)) for p,n in retained.items()}
    phase='rewrite';output.clear();row_slots.clear();preparation_versions.clear()
    words(VISITOR,image.tables[WRITER][0],0,STREAM,0)
    run(entry,args,ROOT)
    if (bytes(output)!=expected_wire or allocations!=retained or word(VISITOR+12)
            or row_slots!=list(range(25)) or preparation_versions!=[1]
            or bytes(u.mem_read(VISITOR-8,8))!=canary or bytes(u.mem_read(VISITOR+528,8))!=canary
            or bytes(u.mem_read(ROWS,0x9094))!=migrated_rows or bytes(u.mem_read(ROOT,0xa0))!=original_root
            or bytes(u.mem_read(ARENA,cursor-ARENA))!=original_arena
            or any(bytes(u.mem_read(p,len(value)))!=value for p,value in migrated_vectors.items())):
        raise RuntimeError('Original write/read/write differs')
    return {'input_case':input_case,'bytes':len(output),'sha256':hashlib.sha256(output).hexdigest(),
        'original_write_read_write_equal':True,'all25rows_read':True,'nested_preparation_version':1,
        'reader_allocations':reader_allocations,'owned_migration_vectors':25,'migration_vector_size':25,
        'migration_vectors_zero':True,'altered_target_scalars_restored':True,'reader_storage_bytes':16,
        'writer_storage_bytes':528,'live_reader_qualified':False,'world_permission':False}


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    for label in PROFILES:parser.add_argument('--'+label.replace('_','-'),type=Path,required=True)
    parser.add_argument('output',type=Path);args=parser.parse_args()
    root=Path(__file__).resolve().parents[2];out=args.output.resolve()
    if not out.is_relative_to((root/'work').resolve()) or out.exists() or not out.parent.is_dir():
        parser.error('Use a fresh output under repository work')
    profiles={}
    for label in PROFILES:
        image=Image(label,getattr(args,label),types=());cases={}
        # Parse imports once per profile, retaining no second image buffer.
        # Check this second read against the already authenticated image too.
        import pefile
        raw=getattr(args,label).read_bytes()
        if hashlib.sha256(raw).hexdigest()!=image.pin:raise RuntimeError('Image changed before import parsing')
        pe=pefile.PE(data=raw)
        tags={b'??2@YAPAXI@Z',b'??3@YAXPAX@Z',b'memmove_s',b'memcpy_s',b'memset'}
        image.setup_reader_imports={i.name:i.address for lib in pe.DIRECTORY_ENTRY_IMPORT for i in lib.imports if i.name in tags}
        required=({b'??2@YAPAXI@Z',b'??3@YAXPAX@Z',b'memset',b'memmove_s'}
                  if label=='gold' else {b'memset'})
        if not required<=set(image.setup_reader_imports):
            raise RuntimeError('Original reader CRT contract differs')
        del raw,pe
        for version in (1,2):
            cases[str(version)]={kind:audit(image,version,kind) for kind in ('complete','truncated','trailing')}
        profiles[label]=cases;print(json.dumps({'profile':label,'cases':6,'passed':True}),flush=True)
        del image;gc.collect()
    for version in ('1','2'):
        values=[profiles[label][version]['complete'] for label in PROFILES]
        if any((x['bytes'],x['sha256'])!=(values[0]['bytes'],values[0]['sha256']) for x in values[1:]):
            raise RuntimeError('Cross-edition baseline streams differ')
    result={'format':'native-setup-root-reader-baseline-v1','profiles':profiles,
        'fingerprints':{k:v[0] for k,v in PROFILES.items()},'positive_cases':6,'rejected_inputs':12,
        'crt_heap_and_stream_modeled':True,'populated_rule_vectors_tested':False,
        'populated_player_containers_tested':False,'live_reader_qualified':False,
        'game_process_started':False,'world_permission':False,'gold_neo_qualified':False,
        'mixed_save_coldreload_qualified':False}
    with out.open('x',encoding='utf-8') as file:json.dump(result,file,indent=2)
    print(json.dumps({k:v for k,v in result.items() if k not in ('profiles','fingerprints')}))


if __name__=='__main__':
    main()
