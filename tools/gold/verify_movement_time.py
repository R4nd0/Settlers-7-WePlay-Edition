"""Compare compiled movement-time math with bounded original x86 instructions.

Requires fingerprinted private analysis images, pefile, Capstone and Unicorn.
No game, network, account or native import is used. This is arithmetic evidence,
separate from native crossplay qualification.
"""
from pathlib import Path
import argparse
import json
import random
import struct
import subprocess
import emulate_event_payloads as native
from capstone import Cs, CS_ARCH_X86, CS_MODE_32
from unicorn import Uc, UC_ARCH_X86, UC_MODE_32, UC_HOOK_CODE
from unicorn.x86_const import (
    UC_X86_REG_ESP, UC_X86_REG_EBP, UC_X86_REG_EAX, UC_X86_REG_ECX,
    UC_X86_REG_FPCW, UC_X86_REG_FPSW, UC_X86_REG_FPTAG,
    UC_X86_REG_MXCSR, UC_X86_REG_XMM3,
)

STACK=0x30000000


class Original:
    def __init__(self,kind,path):
        self.kind=kind
        image=native.Image(kind,path,types=('CGridMovement@ELSet',))
        self.u=u=Uc(UC_ARCH_X86,UC_MODE_32)
        u.mem_map(image.base,(image.size+4095)&~4095)
        u.mem_write(image.base,image.mapped)
        u.mem_map(STACK,0x10000)
        if kind=='gold':
            if image.tables['CGridMovement@ELSet'][1][4]!=0x73cd30:
                raise AssertionError('Gold movement entry differs')
            self.entry,self.stop=0x73ce46,0x73ce5b
            allowed=[(self.entry,self.stop),(0xbaf0e0,0xbaf0fc)]
            call=image.mapped[0x73ce56-image.base:0x73ce5b-image.base]
            if call[0]!=0xe8 or 0x73ce5b+struct.unpack('<i',call[1:])[0]!=0xbaf0e0:
                raise AssertionError('Gold original conversion call differs')
        else:
            # Derive each edition independently from its typed native table.
            entry=image.tables['CGridMovement@ELSet'][1][4]
            code=image.mapped[entry-image.base:entry-image.base+1400]
            instructions=[]
            for ins in Cs(CS_ARCH_X86,CS_MODE_32).disasm(code,entry):
                if ins.mnemonic=='int3':break
                instructions.append(ins)
            pattern=[('movss','xmm2, dword ptr [esp + 0x14]'),
                     ('movaps','xmm0, xmm2'),('divss','xmm0, xmm3'),
                     ('cvttss2si','ecx, xmm0'),
                     ('mov','dword ptr [esp + 0x14], ecx')]
            hits=[i for i in range(len(instructions)-4)
                  if [(x.mnemonic,x.op_str) for x in instructions[i:i+5]]==pattern]
            if len(hits)!=1:raise AssertionError('Ambiguous History quotient block')
            at=hits[0]
            self.entry=instructions[at].address
            self.stop=instructions[at+4].address+instructions[at+4].size
            allowed=[(self.entry,self.stop)]

        def guard(_u,pc,size,_data):
            if not any(lo<=pc and pc+size<=hi for lo,hi in allowed):
                raise AssertionError('Disallowed original instruction')
        u.hook_add(UC_HOOK_CODE,guard)

    def run(self,distance,multiplier):
        u=self.u
        u.mem_write(STACK,bytes(0x10000))
        esp=STACK+0xe000
        for register,value in [(UC_X86_REG_ESP,esp),(UC_X86_REG_EBP,esp+0x50),
                (UC_X86_REG_FPCW,0x27f),(UC_X86_REG_FPSW,0),
                (UC_X86_REG_FPTAG,0xffff),(UC_X86_REG_MXCSR,0x1f80)]:
            u.reg_write(register,value)
        if self.kind=='gold':
            u.mem_write(esp+0x18,struct.pack('<f',distance))
            u.mem_write(esp+0x14,struct.pack('<f',multiplier))
            # Execute the original SSE2 conversion branch on the modeled CPU.
            u.mem_write(0x1110898,struct.pack('<I',1))
        else:
            u.mem_write(esp+0x14,struct.pack('<f',distance))
            u.reg_write(UC_X86_REG_XMM3,struct.unpack('<I',struct.pack('<f',multiplier))[0])
        u.emu_start(self.entry,self.stop,count=100)
        if u.reg_read(UC_X86_REG_ESP)!=esp:raise AssertionError('Original stack drift')
        value=u.reg_read(UC_X86_REG_EAX if self.kind=='gold' else UC_X86_REG_ECX)
        return value if value<0x80000000 else value-0x100000000


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--gold-image',type=Path,required=True)
    parser.add_argument('--ubisoft-image',type=Path,required=True)
    parser.add_argument('--steam-image',type=Path)
    parser.add_argument('--oracle',type=Path,required=True)
    parser.add_argument('--output',type=Path,required=True)
    args=parser.parse_args()
    root=Path(__file__).resolve().parents[2]
    out=args.output.resolve()
    if not out.is_relative_to((root/'work').resolve()):
        parser.error('Private artifacts must remain under work/')
    gold=Original('gold',args.gold_image)
    history={'ubisoft':Original('ubisoft',args.ubisoft_image)}
    if args.steam_image:history['steam']=Original('steam_analysis',args.steam_image)
    out.mkdir(parents=True,exist_ok=False)
    f32=lambda x:struct.unpack('<f',struct.pack('<f',x))[0]
    cases=[(9.0,f32(0.2)),(0.0,1.0),(1.0,1.0),(100.0,1.0),(20.0,f32(0.2))]
    rng=random.Random(90090)
    for _ in range(4096):
        speed=f32(rng.uniform(0.01,8.0))
        cases.append((f32(speed*rng.randint(1,20000)),speed))
    for _ in range(4096):
        cases.append((f32(rng.uniform(0,10000)),f32(rng.uniform(0.01,8.0))))
    if gold.run(*cases[0])!=44:raise AssertionError('Gold reproducer differs')
    expected=b''.join(struct.pack('<i',history['ubisoft'].run(*x)) for x in cases)
    if expected[:4]!=struct.pack('<i',45):raise AssertionError('History reproducer differs')
    for name,model in history.items():
        observed=b''.join(struct.pack('<i',model.run(*x)) for x in cases)
        if observed!=expected:raise AssertionError(name+' original costs differ')
    input_path=out/'input.private.bin'
    actual_path=out/'actual.private.bin'
    input_path.write_bytes(b''.join(struct.pack('<2f',*x) for x in cases))
    run=subprocess.run([str(args.oracle.resolve()),str(input_path),str(actual_path)],
                       capture_output=True,text=True,timeout=30)
    if run.returncode:raise RuntimeError('Compiled oracle failed: '+str(run.returncode))
    if actual_path.read_bytes()!=expected:raise AssertionError('Compiled kernel differs')
    different=sum(gold.run(*x)!=history['ubisoft'].run(*x) for x in cases)
    result={'original_cases_per_edition':len(cases),'history_editions':list(history),
            'compiled_kernel_matches_original_history_costs':True,
            'gold_differing_cases':different,'critical_gold_ms':44,'critical_history_ms':45,
            'original_imports_modeled':False,'game_started':False}
    (out/'result.private.json').write_text(json.dumps(result,indent=2))
    print(json.dumps(result))


if __name__=='__main__':main()
