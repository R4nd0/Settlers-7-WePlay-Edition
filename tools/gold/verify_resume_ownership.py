"""Verify original native file clone/destructor ownership and compiled guards.

Pinned owned analysis images, pefile and Unicorn are required. Executes original
clone/destructor code with modeled native allocation/free functions; no game,
network, file consumer or resumed world runs in this oracle.
"""
from pathlib import Path
import sys,json,struct,hashlib
import argparse
parser=argparse.ArgumentParser(description=__doc__)
for name in ('gold','ubisoft','steam-analysis','fixture'):parser.add_argument('--'+name,type=Path,required=True)
parser.add_argument('output',type=Path)
args=parser.parse_args()
r=Path(__file__).resolve().parents[2]
out=args.output.resolve()
if not out.is_relative_to((r/'work').resolve()) or out.exists() or not out.parent.is_dir():parser.error('Use a fresh output under work')
from emulate_event_payloads import Image
from capstone import Cs,CS_ARCH_X86,CS_MODE_32
from unicorn import Uc,UC_ARCH_X86,UC_MODE_32,UC_HOOK_CODE
from unicorn.x86_const import UC_X86_REG_ESP,UC_X86_REG_ECX,UC_X86_REG_EAX,UC_X86_REG_EIP
import pefile
from verify_codec_guards import check_image
fixture=pefile.PE(str(args.fixture))
if fixture.FILE_HEADER.Machine!=0x14c or fixture.OPTIONAL_HEADER.ImageBase!=0x24000000 or fixture.OPTIONAL_HEADER.SizeOfImage>4*1024*1024:parser.error('Expected bounded fixed-address x86 ownership fixture')
cs=Cs(CS_ARCH_X86,CS_MODE_32);result={}
for kind,path,vt,transport in (('gold',args.gold,0xf4e1e8,0xa4e430),('ubisoft',args.ubisoft,0x114de08,0xedb300),('steam',args.steam_analysis,0x114c3ac,0xeda9f0)):
 image=Image('steam_analysis' if kind=='steam' else kind,path,types=());methods=struct.unpack_from('<6I',image.mapped,vt-image.base)
 def body(address,maximum=2048):
  stop=address
  for ins in cs.disasm(image.mapped[address-image.base:address-image.base+maximum],address):
   if ins.mnemonic=='int3':break
   stop=ins.address+ins.size
  assert address<stop<address+maximum
  return image.mapped[address-image.base:stop-image.base]
 dtor,clone=methods[0],methods[5];db,cb=body(dtor),body(clone)
 clone_calls=[int(i.op_str,16) for i in cs.disasm(cb,clone) if i.mnemonic=='call' and i.op_str.startswith('0x')]
 allocator=clone_calls[0];dtor_calls=[int(i.op_str,16) for i in cs.disasm(db,dtor) if i.mnemonic=='call' and i.op_str.startswith('0x')]
 release=dtor_calls[-1]
 imports=[i for i in cs.disasm(db,dtor) if i.mnemonic=='call' and i.op_str.startswith('dword ptr [0x')]
 assert len(imports)==1
 free_iat=int(imports[0].op_str.split('0x')[1].split(']')[0],16)
 rows=[]
 for detach in (False,True):
  cpu=Uc(UC_ARCH_X86,UC_MODE_32);cpu.mem_map(image.base,(image.size+4095)&~4095);cpu.mem_write(image.base,image.mapped)
  cpu.mem_map(0,4096);cpu.mem_write(0,struct.pack('<I',0xffffffff));cpu.mem_map(0x20000000,65536);cpu.mem_map(0x30000000,65536)
  source=0x20000100;copy=0x20000200;payload=0x20001000;stub=0x2000f000;stop=0x2000ff00;stack=0x3000f000
  cpu.mem_write(source,struct.pack('<8I',vt,0x240007,42,1,payload,0x12345678,8192,24722247));cpu.mem_write(payload,bytes([0x5a])*8192)
  cpu.mem_write(free_iat,struct.pack('<I',stub));frees=[];allocated=[];object_frees=[]
  def ret(u,value=None):
   sp=u.reg_read(UC_X86_REG_ESP);target=struct.unpack('<I',bytes(u.mem_read(sp,4)))[0]
   if value is not None:u.reg_write(UC_X86_REG_EAX,value)
   u.reg_write(UC_X86_REG_ESP,sp+4);u.reg_write(UC_X86_REG_EIP,target)
  def hook(u,at,size,_):
   sp=u.reg_read(UC_X86_REG_ESP)
   if at==allocator:
    amount=struct.unpack('<I',bytes(u.mem_read(sp+4,4)))[0];assert amount==32;allocated.append(copy);ret(u,copy)
   elif at==release:
    obj=struct.unpack('<I',bytes(u.mem_read(sp+4,4)))[0];object_frees.append(obj);ret(u)
   elif at==stub:
    pointer=struct.unpack('<I',bytes(u.mem_read(sp+4,4)))[0];frees.append(pointer);ret(u)
  cpu.hook_add(UC_HOOK_CODE,hook)
  def call(at,obj,flag=None):
   words=[stop]+([] if flag is None else [flag]);cpu.mem_write(stack,struct.pack('<'+'I'*len(words),*words));cpu.reg_write(UC_X86_REG_ESP,stack);cpu.reg_write(UC_X86_REG_ECX,obj)
   cpu.emu_start(at,stop,count=10000,timeout=2000000)
   assert cpu.reg_read(UC_X86_REG_EIP)==stop
  call(clone,source);assert cpu.reg_read(UC_X86_REG_EAX)==copy and allocated==[copy]
  assert bytes(cpu.mem_read(copy,32))==bytes(cpu.mem_read(source,32))
  if detach:cpu.mem_write(source+16,struct.pack('<I',0))
  call(dtor,source,1);call(dtor,copy,1)
  assert object_frees==[source,copy];assert frees==[payload]*(1 if detach else 2)
  assert bytes(cpu.mem_read(payload,8192))==bytes([0x5a])*8192
  rows.append({'detach_factory_payload_after_original_local_clone':detach,'actual_original_clone_executed':True,'both_actual_original_destructors_executed':True,'payload_free_calls':len(frees),'object_free_calls':len(object_frees),'allocator_and_free_imports_modeled':True})
 material=db+cb;fnv=14695981039346656037
 for byte in material:fnv=((fnv^byte)*1099511628211)&((1<<64)-1)
 tb=body(transport)
 result[kind]={'dtor':dtor,'dtor_bytes':len(db),'clone':clone,'clone_bytes':len(cb),'ownership_fnv64':f'{fnv:016x}','transport':transport,'transport_bytes':len(tb),'transport_sha256':hashlib.sha256(tb).hexdigest(),'cases':rows}
 if kind!='gold':
  result[kind]['compiled_guard_checks']=check_image(image,fixture,2 if kind=='ubisoft' else 3,oracle=b'WePlayResumeOwnershipOracle',methods=((dtor,len(db)),(clone,len(cb)),(transport,len(tb))))
 print(kind,'original shallow clone and destructor cases passed; compiled guard checks',result[kind].get('compiled_guard_checks',0))
out.write_text(json.dumps({'fixture_sha256':hashlib.sha256(args.fixture.read_bytes()).hexdigest(),'profiles':result,'native_allocators_and_free_imports_modeled':True,'game_started':False,'actual_file_delivery_qualified_by_this_tool':False},indent=2))
