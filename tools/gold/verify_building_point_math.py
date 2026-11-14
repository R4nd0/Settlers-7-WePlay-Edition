"""Bounded original Steam History building-point oracle, synthetic data only.

Requires pefile, Unicorn and Capstone. The image is fingerprinted by Image;
only allowlisted geometry helpers execute in emulation. No imports, heap,
native game process, accounts or network are used. This is not crossplay proof.
"""
from pathlib import Path
import argparse,json,random,struct,subprocess
import emulate_event_payloads as native
from unicorn import Uc,UC_ARCH_X86,UC_MODE_32,UC_HOOK_CODE
from unicorn.x86_const import *
from capstone import Cs,CS_ARCH_X86,CS_MODE_32
S=0x20000000;STACK=0x30000000;STOP=S+0xf000
class Geometry:
 def __init__(self,path):
  self.im=native.Image('steam_analysis',path,types=());im=self.im
  self.u=u=Uc(UC_ARCH_X86,UC_MODE_32);u.mem_map(im.base,(im.size+4095)&~4095);u.mem_write(im.base,im.mapped)
  u.mem_map(S,0x10000);u.mem_map(STACK,0x10000)
  self.entry=0x6fcb20
  self.allowed=[(self.entry,0x6fd0e3)]
  starts=[0x5a2360,0x5a2370,0x4c6f10,0x4c7c70]
  for at in starts:
   end=None
   for ins in Cs(CS_ARCH_X86,CS_MODE_32).disasm(im.mapped[at-im.base:at-im.base+256],at):
    if ins.mnemonic.startswith('ret'):end=ins.address+ins.size;break
   assert end is not None;self.allowed.append((at,end))
  def hook(u,at,size,_):
   if at==STOP:u.emu_stop();return
   if not any(a<=at<z for a,z in self.allowed):raise AssertionError('Disallowed native target '+hex(at))
  u.hook_add(UC_HOOK_CODE,hook)
 def run(self,case):
  u=self.u;u.mem_write(S,bytes(0xe000));u.mem_write(STACK,bytes(0x10000))
  def words(at,*v):u.mem_write(at,struct.pack('<'+'I'*len(v),*v))
  def floats(at,*v):u.mem_write(at,struct.pack('<'+'f'*len(v),*v))
  obj=S;prop=S+0x1000;manager=S+0x2000;props=S+0x3000;table=S+0x4000
  words(0x13a2304,manager)
  words(manager+0x14c,props);words(props,table);words(table,prop)
  floats(obj+0x28,*case['position'],case['angle'])
  a,b=0x128,0x134
  p,q=0x48,0x54
  for i,(vec,src) in enumerate([(a,p),(b,q)]):
   out=S+0x5000+i*0x100;inp=S+0x6000+i*0x100
   words(obj+vec,out,out+24,out+24)
   words(prop+src,inp,inp+24);floats(inp,*case['points'][i],*case['deltas'][i],7.25);words(inp+20,0)
  for at,xy in zip([0xb4,0xbc,0xc4,0xcc],case['single']):floats(prop+at,*xy)
  esp=STACK+0xf000;words(esp,STOP);u.reg_write(UC_X86_REG_ESP,esp);u.reg_write(UC_X86_REG_ECX,obj)
  u.reg_write(UC_X86_REG_FPCW,0x27f);u.reg_write(UC_X86_REG_FPSW,0);u.reg_write(UC_X86_REG_FPTAG,0xffff);u.reg_write(UC_X86_REG_MXCSR,0x1f80)
  u.emu_start(self.entry,STOP,count=10000)
  assert u.reg_read(UC_X86_REG_EIP)==STOP and u.reg_read(UC_X86_REG_ESP)==esp+4
  return bytes(u.mem_read(S+0x5000,20))+bytes(u.mem_read(S+0x5100,20))+bytes(u.mem_read(obj+0x14c,32))
def main():
 parser=argparse.ArgumentParser(description=__doc__)
 parser.add_argument('--image',type=Path,required=True,help='Pinned private original Steam History analysis image')
 parser.add_argument('--oracle',type=Path,required=True,help='Compiled crossplay_building_point_math_oracle.exe')
 parser.add_argument('--output',type=Path,required=True,help='New private result directory under work/')
 args=parser.parse_args();root=Path(__file__).resolve().parents[2];out=args.output.resolve()
 if not out.is_relative_to((root/'work').resolve()):parser.error('Private oracle artifacts must remain under work/')
 out.mkdir(parents=True,exist_ok=False);he=Geometry(args.image);rng=random.Random(53127);expected=bytearray();inputs=bytearray()
 for i in range(2048):
  xy=lambda scale:[rng.uniform(-scale,scale) for _ in range(2)]
  c={'position':xy(140000),'angle':rng.uniform(-720,720),'points':[xy(10000),xy(10000)],'deltas':[xy(10000),xy(10000)],'single':[xy(10000) for _ in range(4)]}
  if i<16:c['angle']=[0,45,90,135,180,225,270,315,-45,-90,-180,-360,360,0.01,-0.01,359.99][i]
  if i%5==0:c['single'][1]=[0,0]
  if i%7==0:c['single'][3]=[0,0]
  if i%13==0:c['single'][1]=[-0.0,0]
  flat=[*c['position'],c['angle'],*c['points'][0],*c['deltas'][0],7.25,*c['points'][1],*c['deltas'][1],7.25,*[x for p in c['single'] for x in p]]
  inputs.extend(struct.pack('<21f',*flat));expected.extend(he.run(c))
 input_path=out/'input.private.bin';actual_path=out/'actual.private.bin';input_path.write_bytes(inputs)
 p=subprocess.run([str(args.oracle.resolve()),str(input_path),str(actual_path)],capture_output=True,text=True,timeout=30)
 if p.returncode:raise RuntimeError('Compiled oracle failed: '+str(p.returncode)+' '+p.stdout+p.stderr)
 actual=actual_path.read_bytes()
 if len(actual)!=len(expected):raise AssertionError('Compiled oracle output length differs')
 different=[i for i in range(2048) if actual[i*72:i*72+72]!=expected[i*72:i*72+72]]
 result={'cases':2048,'scalar_values':36864,'different_cases':different,'original_history_geometry_emulated':True,'native_imports_modeled':False,'kernel_compiled_and_executed':True,'game_started':False}
 (out/'result.private.json').write_text(json.dumps(result,indent=2));print(json.dumps(result))
 if different:raise AssertionError('Kernel differs from original History geometry')
 print('PASS bit-identical original History building-point oracle')
if __name__=='__main__':main()
