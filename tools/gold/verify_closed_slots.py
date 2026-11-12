"""Offline compiled guards and original closed-slot visitors/consumers.

Original code executes with synthetic objects/streams and modeled Windows locks.
No process, native host factory, real queue, network or game world is started.
Requires pefile and Unicorn; inputs must match the pinned owned analysis images.
"""
import argparse
import gc
import hashlib
import json
from pathlib import Path
import re
import struct
import pefile
import emulate_event_payloads as m
from verify_codec_guards import check_image
from unicorn import Uc,UC_ARCH_X86,UC_MODE_32,UC_HOOK_CODE,UC_HOOK_MEM_WRITE
from unicorn.x86_const import UC_X86_REG_EIP,UC_X86_REG_ESP,UC_X86_REG_ECX,UC_X86_REG_EAX,UC_X86_REG_EBX,UC_X86_REG_EBP,UC_X86_REG_ESI,UC_X86_REG_EDI
parser=argparse.ArgumentParser(description=__doc__)
for label in ('gold','ubisoft','steam_analysis'):parser.add_argument('--'+label.replace('_','-'),type=Path,required=True)
parser.add_argument('--thunk-executable',type=Path,required=True)
parser.add_argument('output',type=Path)
args=parser.parse_args()
root=Path(__file__).resolve().parents[2];output=args.output.resolve()
if not output.is_relative_to((root/'work').resolve()) or output.exists() or not output.parent.is_dir():
 parser.error('Use a fresh output file under an existing repository work directory')
sources={label:getattr(args,label) for label in ('gold','ubisoft','steam_analysis')}
pin_text=(root/'native/crossplay_closed_slot_pins.h').read_text()
# The first row starts after the declaration's '='; parse the bounded array.
pin_body=pin_text.split('Pins[3][16]={',1)[1].split('};',1)[0]
pin_ranges=[(int(a,16),int(n)) for a,n in re.findall(r'\{(0x[0-9a-f]+),(\d+)\}',pin_body)]
if len(pin_ranges)!=48:raise ValueError('Unexpected closed-slot pin table')
profile_ranges={label:[pair for pair in pin_ranges[index*16:(index+1)*16] if pair[1]] for index,label in enumerate(sources)}
m.EVENTS=('CChangePlayerSlotEvent',)
fixture_path=args.thunk_executable
fixture=pefile.PE(data=fixture_path.read_bytes())
if fixture.FILE_HEADER.Machine!=0x14c or fixture.OPTIONAL_HEADER.ImageBase!=0x24000000 or fixture.OPTIONAL_HEADER.SizeOfImage>4*1024*1024:
 raise ValueError('Expected the bounded fixed x86 closed-slot guard fixture')

def consumer(image,slot):
 u=Uc(UC_ARCH_X86,UC_MODE_32);u.mem_map(image.base,(image.size+4095)&~4095);u.mem_write(image.base,image.mapped)
 u.mem_map(0,4096);u.mem_map(0x20000000,65536);u.mem_map(0x30000000,65536)
 plugin,event,rows,holder,lock,stop,enter,leave=(0x20000000+i for i in (0,0x1000,0x2000,0x3000,0x4000,0x5000,0x5010,0x5020))
 def put(address,*words):u.mem_write(address,struct.pack('<'+'I'*len(words),*words))
 def word(address):return struct.unpack('<I',u.mem_read(address,4))[0]
 put(plugin,0,0,holder,0);put(plugin+0x44,rows,rows+8*24,rows+8*24);put(holder,0,lock)
 vt=image.tables[m.EVENTS[0]][0];put(event,vt,7|(55<<16),42,1,slot)
 for i in range(8):put(rows+24*i,1,0,0xffffffff,0,0,0xabcdef00+i)
 before=bytes(u.mem_read(rows,192));event_before=bytes(u.mem_read(event,20));sp=0x3000f000;put(sp,stop,event)
 steam=image.label=='steam_analysis'
 pe=pefile.PE(str(sources[image.label]))
 imports=tuple(next(imp.address for lib in pe.DIRECTORY_ENTRY_IMPORT for imp in lib.imports if imp.name==name)
               for name in (b'EnterCriticalSection',b'LeaveCriticalSection'))
 for address,target in zip(imports,(enter,leave)):put(address,target)
 calls=[]
 def code(uc,address,length,data):
  if address==stop:uc.emu_stop();return
  if address in (enter,leave):
   esp=uc.reg_read(UC_X86_REG_ESP);assert word(esp+4)==lock;calls.append(address)
   uc.reg_write(UC_X86_REG_ESP,esp+8);uc.reg_write(UC_X86_REG_EIP,word(esp));return
  if not image.base<=address<image.base+image.size:raise RuntimeError('Unexpected consumer target')
  if not any(start<=address and address+length<=start+size for start,size in profile_ranges[image.label]):
   raise RuntimeError('Original consumer instruction outside immutable closed-slot path')
 def write(uc,access,address,size,value,data):
  if not (0x30000000<=address<=address+size<=0x30010000 or address==0 and size==4
          or rows<=address<=address+size<=rows+192 or plugin+12<=address<=address+size<=plugin+16):
   raise RuntimeError('Native slot consumer wrote outside owned cache/stack')
 preserved={UC_X86_REG_EBX:0x12345678,UC_X86_REG_EBP:0x23456789,UC_X86_REG_ESI:0x34567890,UC_X86_REG_EDI:0x456789ab}
 for reg,value in {**preserved,UC_X86_REG_ECX:plugin,UC_X86_REG_ESP:sp}.items():u.reg_write(reg,value)
 u.hook_add(UC_HOOK_CODE,code);u.hook_add(UC_HOOK_MEM_WRITE,write)
 entry=0x840e30 if steam else 0x841060
 u.emu_start(entry,stop+1,count=10000,timeout=2000000)
 expected=bytearray(before);at=(slot-1)*24
 struct.pack_into('<I',expected,at,2);expected[at+4]=1;struct.pack_into('<I',expected,at+8,slot);expected[at+12]=0;struct.pack_into('<II',expected,at+16,0,0)
 assert u.reg_read(UC_X86_REG_EIP)==stop and u.reg_read(UC_X86_REG_ESP)==sp+8
 assert all(u.reg_read(k)==v for k,v in preserved.items()) and bytes(u.mem_read(event,20))==event_before
 actual=bytes(u.mem_read(rows,192))
 if actual!=expected or word(plugin+12)!=2 or calls!=[enter,leave]:
  raise RuntimeError(str({'profile':image.label,'slot':slot,'differences':[(i,expected[i],actual[i]) for i in range(192) if expected[i]!=actual[i]],'dirty':word(plugin+12),'calls':calls}))

results={};wires={}
for edition,(label,path) in enumerate(sources.items(),1):
 image=m.Image(label,path)
 table,methods=image.tables[m.EVENTS[0]]
 ranges=profile_ranges[label]
 expected=[(table,24),*((a,32) for a in methods),(methods[3],0x94 if edition==1 else 0x34),(methods[5],0x44 if edition==1 else 0x35)]
 if ranges[:9]!=expected:raise ValueError('Closed-slot event pins differ from original reflection methods')
 if edition>1:
  entry=0x841060 if edition==2 else 0x840e30
  expected_path=[(entry,0x4e),(entry+0x221,0xba),(entry+0xb64,0x20),(entry+0xb84,0x7f)]
  if ranges[9:13]!=expected_path:raise ValueError('Unexpected closed-slot consumer path ranges')
  selector=image.mapped[entry+0xbcc+52-image.base]
  target=struct.unpack_from('<I',image.mapped,entry+0xb84+4*selector-image.base)[0]
  if selector!=14 or target!=entry+0x244:raise ValueError('Original subtype 55 dispatch differs')
 guards=check_image(image,fixture,edition,oracle=b'WePlayClosedSlotMethodsOracle',methods=ranges)
 native_checks=0
 for slot in (3,4):
  wire=m.payload(image,m.EVENTS[0],scalar=slot-16,asynchronous=1)
  # payload() runs the original event visitor without the outer factory class-ID.
  assert len(wire)==13 and struct.unpack('<II',wire[:8])==(0x11112222,0x33334444) and wire[8]==1 and struct.unpack('<I',wire[9:])[0]==slot
  fields=m.payload(image,m.EVENTS[0],read_wire=wire)
  assert fields[:4]==(0x11112222,0x33334444,1,slot)
  wires[(label,slot)]=wire;native_checks+=2
  if label!='gold':consumer(image,slot);native_checks+=1
 results[label]={'compiled_guard_checks':guards,'original_native_checks':native_checks}
 print(json.dumps({label:results[label]}),flush=True);del image;gc.collect()
assert all(wires[(label,slot)]==wires[('gold',slot)] for label in sources for slot in (3,4))
receipt={'profiles':results,'fixture_sha256':hashlib.sha256(fixture_path.read_bytes()).hexdigest(),'synthetic_objects_and_streams':True,'critical_sections_modeled':True,'original_dispatch_and_getter_executed':True,'game_started':False,'world_permission':False}
with output.open('x') as stream:json.dump(receipt,stream,indent=2)
print(json.dumps(receipt))
