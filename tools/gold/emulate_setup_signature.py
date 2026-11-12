"""Run compiled setup writer calls plus original visitors in bounded offline x86.

The recording visitor skips nested nodes and uses empty containers/neutral-only
rows. Heap/binary streams and running worlds are NOT tested. Live native writer
results have separate receipts. Owned game images remain private.
"""
import argparse,gc,hashlib,json,struct
from pathlib import Path
import pefile
from emulate_event_payloads import Image,PROFILES
from emulate_logistics_fields import REFLECTION_IAT,S,STACK,OBJECT,VISITOR,TABLE,NAME,REFLECT,STOP
from verify_codec_guards import check_image
METHODS={'gold':(0x9cf3f0,0x9ce840),'ubisoft':(0x5f6490,0x5f5820),'steam_analysis':(0x5f5ff0,0x5f5380)}

def survey(image, name, fixture, entry, edition):
    from unicorn import Uc, UC_ARCH_X86, UC_MODE_32, UC_HOOK_CODE, UC_HOOK_MEM_WRITE
    from unicorn.x86_const import (UC_X86_REG_EAX, UC_X86_REG_EBX, UC_X86_REG_EBP,
        UC_X86_REG_ECX, UC_X86_REG_ESI, UC_X86_REG_EDI, UC_X86_REG_ESP, UC_X86_REG_EIP)
    u = Uc(UC_ARCH_X86, UC_MODE_32)
    u.mem_map(image.base, (image.size + 4095) & ~4095)
    u.mem_write(image.base, image.mapped)
    u.mem_map(0, 4096)  # Emulated SEH head only.
    u.mem_map(S, 0x30000)
    base=fixture.OPTIONAL_HEADER.ImageBase
    u.mem_map(base,(fixture.OPTIONAL_HEADER.SizeOfImage+4095)&~4095)
    u.mem_write(base,fixture.get_memory_mapped_image())
    u.mem_map(STACK, 0x10000)

    def words(at, *values):
        u.mem_write(at, struct.pack('<' + 'I' * len(values), *values))

    def word(at):
        return struct.unpack('<I', u.mem_read(at, 4))[0]

    def text(at):
        if not (image.base <= at <= image.base + image.size - 128
                or S <= at <= S + 0x10000 - 128):
            raise RuntimeError('Foreign field name')
        raw = bytes(u.mem_read(at, 128))
        end = raw.find(b'\0')
        if end < 0 or any(not 32 <= b < 127 for b in raw[:end]):
            raise RuntimeError('Unbounded/non-ASCII field name')
        return raw[:end].decode('ascii')

    words(OBJECT, 0)
    if name == 'players':
        reference,configuration,rows=S+0x8000,S+0x8100,S+0x10000
        words(OBJECT+(0x6c if image.label=='gold' else 0x60),reference,configuration)
        words(reference,rows);words(configuration,rows,rows)
        u.mem_write(rows+(0x9078 if image.label=='gold' else 0x8db8),b'\x01')
    words(VISITOR, TABLE, 0)  # Original serializers take their write branch.
    u.mem_write(NAME, b'class SyntheticTask\0')
    for index in range(39):
        words(TABLE + index * 4, S + 0x7000 + index * 16)
    words(REFLECTION_IAT[image.label], REFLECT)
    fields = []
    completed = False

    def returned(count, value=0):
        sp = u.reg_read(UC_X86_REG_ESP)
        u.reg_write(UC_X86_REG_EAX, value)
        u.reg_write(UC_X86_REG_EIP, word(sp))
        u.reg_write(UC_X86_REG_ESP, sp + 4 + count)

    def code(uc, at, size, data):
        nonlocal completed
        if at == STOP:
            completed = True
            uc.emu_stop()
        elif at == REFLECT or at == image.name_helper:
            returned(4 if image.label == 'gold' else 0, NAME)
        elif at == image.compare_helper:
            returned(0, 1)
        elif S + 0x7000 <= at < S + 0x7000 + 39 * 16 and (at - S - 0x7000) % 16 == 0:
            index = (at - S - 0x7000) // 16
            sp = uc.reg_read(UC_X86_REG_ESP)
            if uc.reg_read(UC_X86_REG_ECX) != VISITOR:
                raise RuntimeError('Foreign visitor')
            if index == 1:
                fields.append({'kind': 'node', 'name': text(word(sp + 4))})
                returned(16, 0)  # Deliberately skip nested node bodies.
            elif index in (2, 4):
                returned(0)
            elif index == 3:
                if word(sp + 8) != 0:
                    raise RuntimeError('Nonempty container outside fixture')
                fields.append({'kind': 'container', 'name': text(word(sp + 4)), 'count': 0})
                returned(12, 0)
            elif index in (0x2c//4,0x34//4,0x38//4,0x3c//4,0x44//4,0x58//4):
                pointer = word(sp + 4)
                width = {0x2c:16,0x34:4,0x38:2,0x3c:1,0x44:4,0x58:1}[index * 4]
                if OBJECT <= pointer <= OBJECT + 0x300 - width:
                    offset = pointer - OBJECT
                elif STACK <= pointer <= STACK + 0x10000 - width:
                    offset = None  # A temporary, not an inferred object offset.
                else:
                    raise RuntimeError('Foreign field pointer')
                fields.append({'kind': 'field', 'visitor_slot': index * 4,
                               'name': text(word(sp + 8)), 'offset': offset})
                returned(12)
            else:
                raise RuntimeError('Unexpected visitor method')
        elif not (image.base<=at<image.base+image.size or base<=at<base+fixture.OPTIONAL_HEADER.SizeOfImage):
            raise RuntimeError('Foreign code')

    def write(uc, access, at, size, value, data):
        if not (at == 0 and size == 4 or STACK <= at <= at + size <= STACK + 0x10000
                or OBJECT <= at <= at + size <= OBJECT + 0x300):
            raise RuntimeError('Foreign write')

    u.hook_add(UC_HOOK_CODE, code)
    u.hook_add(UC_HOOK_MEM_WRITE, write)
    sp = STACK + 0xf000
    words(sp, STOP, edition, OBJECT, VISITOR)
    kept = {UC_X86_REG_EBX: 0x12345678, UC_X86_REG_EBP: 0x23456789,
            UC_X86_REG_ESI: 0x34567890, UC_X86_REG_EDI: 0x456789ab}
    for reg, value in {**kept, UC_X86_REG_ESP: sp, UC_X86_REG_ECX: OBJECT}.items():
        u.reg_write(reg, value)
    before = bytes(u.mem_read(OBJECT, 0x400))
    u.emu_start(entry, STOP + 1, count=200000, timeout=3_000_000)
    if (not completed or u.reg_read(UC_X86_REG_EIP) != STOP
            or u.reg_read(UC_X86_REG_ESP) != sp + 4 or word(0) != 0
            or any(u.reg_read(k) != v for k, v in kept.items())
            or bytes(u.mem_read(OBJECT, 0x400)) != before):
        raise RuntimeError('Incomplete serializer or changed ABI/input state')
    return fields


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    for label in PROFILES:parser.add_argument('--'+label.replace('_','-'),type=Path,required=True)
    parser.add_argument('--thunk-executable',type=Path,required=True)
    parser.add_argument('output',type=Path);args=parser.parse_args()
    root=Path(__file__).resolve().parents[2];out=args.output.resolve()
    if not out.is_relative_to((root/'work').resolve()) or out.exists() or not out.parent.is_dir():parser.error('Use a fresh output under repository work')
    raw=args.thunk_executable.read_bytes();fixture=pefile.PE(data=raw)
    if fixture.FILE_HEADER.Machine!=0x14c or fixture.OPTIONAL_HEADER.ImageBase!=0x24000000 or fixture.OPTIONAL_HEADER.SizeOfImage>4*1024*1024:raise ValueError('Expected bounded fixed x86 fixture')
    base=fixture.OPTIONAL_HEADER.ImageBase
    exports={e.name.decode():base+e.address for e in fixture.DIRECTORY_ENTRY_EXPORT.symbols if e.name}
    expected=json.loads((root/'tests/fixtures/gold-native-setup-fields.json').read_text())
    results={}
    for edition,label in enumerate(PROFILES,1):
        image=Image(label,getattr(args,label),types=())
        guards=check_image(image,fixture,edition,oracle=b'WePlaySetupMethodsOracle',methods=tuple((m,4096) for m in METHODS[label]))
        fields={name:survey(image,name,fixture,exports[export],edition) for name,export in [('rules','WePlaySetupRulesOracle'),('players','WePlaySetupPlayersOracle')]}
        if fields!=expected:raise RuntimeError('Original setup fields differ from reviewed fixture: '+label)
        results[label]={'guard_checks':guards,'original_visitors_completed':len(fields)}
        print(json.dumps({'profile':label,**results[label]}),flush=True);del image;gc.collect()
    receipt={'format':'native-setup-writer-abi-v1','profiles':results,'native_executions':6,'guard_checks':sum(v['guard_checks'] for v in results.values()),
        'fixture_sha256':hashlib.sha256(raw).hexdigest(),'fingerprints':{k:v[0] for k,v in PROFILES.items()},
        'recording_visitor_and_reflection_modeled':True,'nested_nodes_traversed':False,'nonempty_containers_tested':False,
        'native_binary_stream_tested':False,'game_process_started':False,'crossplay_qualified':False}
    with out.open('x') as stream:json.dump(receipt,stream,indent=2)
    print(json.dumps(receipt))
if __name__=='__main__':main()
