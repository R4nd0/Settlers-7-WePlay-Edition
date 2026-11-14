"""Compare compiled interpolation with fingerprinted original x86 path outputs.

Requires private analysis images, pefile and Unicorn. The complete original
arithmetic routines run on a modeled CPU; vector append and the temporary point
constructor are modeled. Search, smoothing and real gameplay are separate tests.
No game, account, network or original native import is executed.
"""
from pathlib import Path
import argparse
import json
import random
import struct
import subprocess
import emulate_event_payloads as native
from unicorn import Uc, UC_ARCH_X86, UC_MODE_32, UC_HOOK_CODE
from unicorn.x86_const import (
    UC_X86_REG_EAX, UC_X86_REG_ECX, UC_X86_REG_EDX, UC_X86_REG_EBP,
    UC_X86_REG_ESP, UC_X86_REG_EIP, UC_X86_REG_FPCW, UC_X86_REG_FPSW,
    UC_X86_REG_FPTAG, UC_X86_REG_MXCSR,
)

SCRATCH=0x20000000
STACK=0x30000000
PROFILES={
    'gold':(0x746f40,0x747060,0x4efe60,None),
    'ubisoft':(0x99e3d0,0x99e57e,0x59e030,0x4c7b10),
    'steam_analysis':(0x99e1a0,0x99e34e,0x59daf0,0x4c7c70),
}


class Original:
    def __init__(self,kind,path):
        self.kind=kind
        image=native.Image(kind,path,types=())
        self.entry,self.end,self.append,self.constructor=PROFILES[kind]
        self.u=u=Uc(UC_ARCH_X86,UC_MODE_32)
        u.mem_map(image.base,(image.size+4095)&~4095)
        u.mem_write(image.base,image.mapped)
        u.mem_map(SCRATCH,0x10000)
        u.mem_map(STACK,0x10000)
        self.points=[]
        self.operands=[]

        def hook(cpu,pc,size,_unused):
            esp=cpu.reg_read(UC_X86_REG_ESP)

            def word(at):return struct.unpack('<I',cpu.mem_read(at,4))[0]

            def returned(extra=0):
                address=word(esp)
                cpu.reg_write(UC_X86_REG_ESP,esp+4+extra)
                cpu.reg_write(UC_X86_REG_EIP,address)

            if pc==self.append:
                pointer=cpu.reg_read(UC_X86_REG_EDX) if kind=='gold' else word(esp+4)
                self.points.append(tuple(struct.unpack('<2I',cpu.mem_read(pointer,8))))
                if kind=='gold':
                    ebp=cpu.reg_read(UC_X86_REG_EBP)

                    def scalar(offset):return struct.unpack('<f',cpu.mem_read(ebp+offset,4))[0]

                    if word(esp) not in (0x746ff4,0x74702d):
                        raise AssertionError('Unexpected original Gold append caller')
                    self.operands.append((*map(scalar,(-0x10,0xc,-8,-4,-0xc)),
                                          word(esp)==0x74702d))
                returned(0 if kind=='gold' else 4)
                return
            if self.constructor is not None and pc==self.constructor:
                cpu.mem_write(cpu.reg_read(UC_X86_REG_ECX),bytes(8))
                returned()
                return
            if not (self.entry<=pc and pc+size<=self.end):
                raise AssertionError('Disallowed original instruction')
        u.hook_add(UC_HOOK_CODE,hook)

    def run(self,nodes):
        if len(nodes)>16:raise ValueError('Bounded original node fixture')
        u=self.u
        u.mem_write(SCRATCH,bytes(0x10000))
        u.mem_write(STACK,bytes(0x10000))
        self.points=[]
        self.operands=[]

        def word(at,*values):u.mem_write(at,struct.pack('<'+'I'*len(values),*values))

        esp=STACK+0xe000
        root=SCRATCH+0x100
        manager=SCRATCH+0x200
        registry=SCRATCH+0x300
        vector=SCRATCH+0x500
        ids=SCRATCH+0x600
        output=SCRATCH+0x700
        word(root+(0x24 if self.kind=='gold' else 0x20),manager)
        word(manager+0x1c,registry)
        for index,(x,y,side) in enumerate(nodes):
            node=SCRATCH+0x1000+index*0x20
            word(registry+index*4,node)
            word(node,0,x,y,side)
            word(ids+index*4,index)
        if self.kind=='gold':
            word(vector,0,ids,ids+len(nodes)*4)
            word(esp,SCRATCH+0xf000,root,vector)
        else:
            word(vector,ids,ids+len(nodes)*4)
            word(esp,SCRATCH+0xf000,output,vector)
        for register,value in (
                (UC_X86_REG_ESP,esp),(UC_X86_REG_EBP,STACK+0xf000),
                (UC_X86_REG_EAX,output),(UC_X86_REG_ECX,root),
                (UC_X86_REG_FPCW,0x27f),(UC_X86_REG_FPSW,0),
                (UC_X86_REG_FPTAG,0xffff),(UC_X86_REG_MXCSR,0x1f80)):
            u.reg_write(register,value)
        u.emu_start(self.entry,SCRATCH+0xf000,count=50000)
        if u.reg_read(UC_X86_REG_ESP)!=esp+12:
            raise AssertionError('Original stack drift')
        if len(self.points)!=2*max(0,len(nodes)-1):
            raise AssertionError('Original output count differs')
        return self.points[:],self.operands[:]


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--gold-image',type=Path,required=True)
    parser.add_argument('--ubisoft-image',type=Path,required=True)
    parser.add_argument('--steam-image',type=Path,required=True)
    parser.add_argument('--oracle',type=Path,required=True)
    parser.add_argument('--output',type=Path,required=True)
    args=parser.parse_args()
    root=Path(__file__).resolve().parents[2]
    out=args.output.resolve()
    if not out.is_relative_to((root/'work').resolve()):
        parser.error('Private artifacts must remain under work/')
    gold=Original('gold',args.gold_image)
    history={name:Original(label,path) for name,label,path in (
        ('ubisoft','ubisoft',args.ubisoft_image),
        ('steam','steam_analysis',args.steam_image))}
    cases=[[],[(0,0,1)],[(0,0,1),(0,0,1)],
           [(1000,1000,1),(1001,999,64)],
           [(1192,239,2),(1494,941,4),(894,130,2),(1155,253,8)]]
    rng=random.Random(116116)
    for _ in range(2048):
        cases.append([(rng.randrange(0,1500),rng.randrange(0,1500),
                       rng.choice((1,2,4,8,16,32,64)))
                      for _ in range(rng.randrange(2,10))])
    expected=bytearray()
    operands=bytearray()
    differences=0
    for nodes in cases:
        original,inputs=gold.run(nodes)
        reference,_=history['ubisoft'].run(nodes)
        for name,model in history.items():
            if name=='ubisoft':continue
            observed,_=model.run(nodes)
            if observed!=reference:raise AssertionError('Original History points differ')
        if original!=reference:differences+=1
        if len(inputs)!=len(reference):raise AssertionError('Gold operand count differs')
        operands.extend(b''.join(struct.pack('<5fI',*row) for row in inputs))
        expected.extend(b''.join(struct.pack('<2I',*point) for point in reference))
    out.mkdir(parents=True,exist_ok=False)
    input_path=out/'operands.private.bin'
    actual_path=out/'actual.private.bin'
    input_path.write_bytes(operands)
    run=subprocess.run([str(args.oracle.resolve()),str(input_path),str(actual_path)],
                       capture_output=True,text=True,timeout=30)
    if run.returncode:raise RuntimeError('Compiled oracle failed: '+str(run.returncode))
    if actual_path.read_bytes()!=expected:raise AssertionError('Compiled kernel differs')
    result={'original_full_output_cases_per_edition':len(cases),
            'history_editions':list(history),'points_per_edition':len(expected)//8,
            'compiled_kernel_matches_both_original_history_editions':True,
            'gold_differing_cases':differences,'vector_append_and_constructor_modeled':True,
            'original_native_imports_executed':False,'game_started':False}
    (out/'result.private.json').write_text(json.dumps(result,indent=2))
    print(json.dumps(result))


if __name__=='__main__':main()
