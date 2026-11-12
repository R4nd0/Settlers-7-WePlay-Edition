"""Run compiled participant thunks and pinned original constructors/queues in x86 emulation.

Only bounded synthetic names, IDs and empty/three-part castle values are used.
Imports are emulated heap/memory operations; no game process or network runs.
This verifies construction, cloning and local enqueue/destruction, not native
session admission, queue-consumer dispatch or crossplay. Empty deque maps and
lock imports are modeled; original clone/enqueue/destructor code executes.
"""
from pathlib import Path
import argparse,hashlib,struct,json,gc
import emulate_event_payloads as m
import pefile
from unicorn import Uc,UC_ARCH_X86,UC_MODE_32,UC_HOOK_CODE,UC_HOOK_MEM_WRITE
from unicorn.x86_const import *
m.EVENTS=('CPlayerConnectionEvent',)
PROFILES={'gold':(0xa50370,0xaa6fe0,0x51f7b0,0x9c,0x94,0x98),
 'ubisoft':(0x656f50,0x690c20,0x586ab0,0x94,0x8c,0x90),
 'steam_analysis':(0x656b30,0x690800,0x586570,0x94,0x8c,0x90)}
S=0x20000000;O=S+0x1000;D=S+0x2000;N=S+0x3000;C=S+0x4000;STOP=S+0x5000;H=S+0x10000;STACK=0x30000000
T=S+0x8000;MAP=S+0x9000
SEND={'gold':0xa4e430,'ubisoft':0xedb300,'steam_analysis':0xeda9f0}
def execute(im,imports,thunk,ctor,update,castle_default,obj_size,extra_offset,local_offset,subtype=2,text=b'Crossplay',connection=0,local=False,host=False,castle_parts=False,ready=False):
 u=Uc(UC_ARCH_X86,UC_MODE_32);u.mem_map(im.base,(im.size+4095)&~4095);u.mem_write(im.base,im.mapped)
 u.mem_map(0,4096);u.mem_map(S,0x100000);u.mem_map(STACK,0x10000)
 def words(a,*v):u.mem_write(a,struct.pack('<'+'I'*len(v),*v))
 def word(a):return struct.unpack('<I',u.mem_read(a,4))[0]
 if subtype not in (2,3,4,44) or not 0<=len(text)<=64 or any(c<32 or c>126 for c in text):raise ValueError('Unsupported synthetic participant')
 words(D,2 if subtype==3 else 6 if subtype in(4,44) else 1,connection,0,1 if host else 0xffffffff if subtype==2 else 2)
 principal=0 if local else 0x3456
 words(D+0x10,N,len(text)+1,len(text),0);words(D+0x20,principal);u.mem_write(D+0x24,bytes((int(host),int(ready),0,0)));words(D+0x28,connection,0,0)
 u.mem_write(N,text+b'\0');u.mem_write(O-16,b'\xa5'*16);u.mem_write(O+obj_size,b'\xa5'*16)
 next_heap=H;allocs={};stubs={};copies=set();last=[];locks=[]
 u.mem_map(thunk.OPTIONAL_HEADER.ImageBase,(thunk.OPTIONAL_HEADER.SizeOfImage+4095)&~4095)
 u.mem_write(thunk.OPTIONAL_HEADER.ImageBase,thunk.get_memory_mapped_image())
 exports={entry.name.decode('ascii'):thunk.OPTIONAL_HEADER.ImageBase+entry.address for entry in thunk.DIRECTORY_ENTRY_EXPORT.symbols if entry.name}
 def owned(a,n):
  return (STACK<=a<=a+n<=STACK+0x10000 or O<=a<=a+n<=O+obj_size
   or C<=a<=a+n<=C+0x60 or T<=a<=a+n<=T+0x100 or MAP<=a<=a+n<=MAP+0x100
   or any(p<=a<=a+n<=p+size for p,size in allocs.items()))
 for name,address in imports:
  name={'??2@YAPAXI@Z':'malloc','??3@YAXPAX@Z':'free'}.get(name,name)
  if name in ('malloc','free','memcpy','memmove','memset','EnterCriticalSection','LeaveCriticalSection'):
   stub=S+0x6000+len(stubs)*16;stubs[stub]=name;words(address,stub)
 if im.label=='gold':copies.add(0xbaee6c)
 def ret(n,value=0):
  sp=u.reg_read(UC_X86_REG_ESP);u.reg_write(UC_X86_REG_EAX,value);u.reg_write(UC_X86_REG_EIP,word(sp));u.reg_write(UC_X86_REG_ESP,sp+4+n)
 def code(uc,a,size,data):
  nonlocal next_heap
  last.append(a)
  if len(last)>12:last.pop(0)
  if a==STOP:uc.emu_stop();return
  name='memcpy' if a in copies else stubs.get(a)
  if name:
   sp=uc.reg_read(UC_X86_REG_ESP);p=word(sp+4)
   if name in ('EnterCriticalSection','LeaveCriticalSection'):
    if p!=T+0x80:raise RuntimeError('foreign native queue lock')
    locks.append(name);ret(4)
   elif name=='malloc':
    if not 0<p<=65536:raise RuntimeError('allocation size')
    ptr=next_heap;next_heap+=(p+15)&~15
    if next_heap>H+0x80000:raise RuntimeError('heap bound')
    allocs[ptr]=p;ret(0,ptr)
   elif name=='free':
    if p:del allocs[p]
    ret(0)
   else:
    v,n=word(sp+8),word(sp+12)
    if n>65536 or not owned(p,n):raise RuntimeError('copy bound/ownership')
    uc.mem_write(p,bytes((v&255,))*n if name=='memset' else bytes(uc.mem_read(v,n)));ret(0,p)
  elif not (im.base<=a<im.base+im.size or thunk.OPTIONAL_HEADER.ImageBase<=a<thunk.OPTIONAL_HEADER.ImageBase+thunk.OPTIONAL_HEADER.SizeOfImage):raise RuntimeError('unmapped executable '+hex(a))
 def writes(uc,access,a,size,value,data):
  if not (a==0 and size==4 or owned(a,size)):
   raise RuntimeError('write outside owned object/stack/heap '+hex(a))
 u.hook_add(UC_HOOK_CODE,code);u.hook_add(UC_HOOK_MEM_WRITE,writes)
 preserved={UC_X86_REG_EBX:0x12345678,UC_X86_REG_EBP:0x23456789,UC_X86_REG_ESI:0x34567890,UC_X86_REG_EDI:0x456789ab}
 def call(entry,args,this=None,regs=None,cdecl=False):
  sp=STACK+0xf000;words(sp,STOP,*args)
  for k,v in {**preserved,UC_X86_REG_ESP:sp,UC_X86_REG_ECX:this or 0,**(regs or {})}.items():u.reg_write(k,v)
  u.emu_start(entry,STOP+1,count=300000,timeout=2000000)
  if u.reg_read(UC_X86_REG_EIP)!=STOP or u.reg_read(UC_X86_REG_ESP)!=sp+4+(0 if cdecl else 4*len(args)) or word(0)!=0:raise RuntimeError('call/stack/SEH incomplete '+str([hex(x) for x in last]))
  for k,v in {**preserved,**{k:v for k,v in (regs or {}).items() if k in preserved}}.items():
   if u.reg_read(k)!=v:raise RuntimeError('nonvolatile register')
 try:
  # Independent original default castle object; never reinterpret another edition's memory.
  call(castle_default,[C] if im.label=='gold' else [],C if im.label!='gold' else None)
  if castle_parts:
   if subtype!=3:raise ValueError('A foreign design needs the confirmed constructor')
   type_offset=0 if im.label=='gold' else 4
   arrays=S+0xa000
   def native_text(where,text,pointer):
    u.mem_write(pointer,text+b'\0');words(where,pointer,len(text)+1,len(text),0)
   native_text(C+type_offset,b'woodland',arrays+0x300)
   for index,asset in enumerate((b'Keep',b'Tower',b'Gate')):
    native_text(arrays+index*16,asset,arrays+0x340+index*32)
   words(arrays+0x100,0xffffffff,0,0);words(arrays+0x200,0,3,4)
   for vector,begin,stride in ((0x24,arrays,16),(0x34 if im.label=='gold' else 0x30,arrays+0x100,4),(0x44 if im.label=='gold' else 0x3c,arrays+0x200,4)):
    words(C+vector,begin,begin+3*stride,begin+3*stride)
  source=bytes(u.mem_read(D,0x34));castle=bytes(u.mem_read(C,0x50 if im.label=='gold' else 0x48))
  extra=0x10203040
  if im.label=='gold':
   if subtype==3:call(exports['WePlayGoldConfirmedOracle'],[update,O,D,connection,int(local),C,extra],cdecl=True)
   else:call(exports['WePlayGoldAddedOracle'],[ctor,O,D,subtype,connection,int(local)],cdecl=True)
  else:call(exports['WePlayHistoryOracle'],[update if subtype==3 else ctor,O,D,subtype,connection,int(local),C if subtype==3 else 0,extra if subtype==3 else 0,int(subtype==3)],cdecl=True)
  checks=(word(O)==im.tables[m.EVENTS[0]][0] and word(O+4)==(subtype<<16|7) and word(O+8)==connection,
   bytes(u.mem_read(O+0x10,0x10))==source[:0x10] and word(O+0x30)==principal,
   bytes(u.mem_read(O+0x34,4))==source[0x24:0x28] and word(O+0x38)==connection,
   bytes(u.mem_read(word(O+0x20),len(text)+1))==text+b'\0' and word(O+0x20)!=N,
   u.mem_read(O+local_offset,1)==bytes((int(local),)),
   subtype!=3 or word(O+extra_offset)==extra,
   source==bytes(u.mem_read(D,0x34)) and castle==bytes(u.mem_read(C,len(castle))),
   bytes(u.mem_read(O-16,16))==b'\xa5'*16 and bytes(u.mem_read(O+obj_size,16))==b'\xa5'*16)
  if not all(checks):raise RuntimeError('Native construction output/ownership mismatch '+str(checks))
  def check_castle(event):
   if not castle_parts:return
   castle=event+0x44
   type_at=castle+(0 if im.label=='gold' else 4)
   if bytes(u.mem_read(word(type_at),9))!=b'woodland\0' or word(type_at)==arrays+0x300:raise RuntimeError('castle type copy')
   parts=word(castle+0x24)
   if parts==arrays or word(castle+0x28)-parts!=48:raise RuntimeError('castle vector copy')
   for index,asset in enumerate((b'Keep',b'Tower',b'Gate')):
    if bytes(u.mem_read(word(parts+16*index),len(asset)+1))!=asset+b'\0' or word(parts+16*index)==arrays+0x340+index*32:raise RuntimeError('castle part string copy')
   for offset,values in ((0x34 if im.label=='gold' else 0x30,(0xffffffff,0,0)),(0x44 if im.label=='gold' else 0x3c,(0,3,4))):
    if tuple(word(word(castle+offset)+4*i) for i in range(3))!=values:raise RuntimeError('castle parent/slot copy')
  check_castle(O)
  # The real local-only transport method must clone and choose its own queue.
  # Preallocated empty maps model valid native deque storage. No send/factory/
  # clone/queue helper is stubbed; only imported heap/lock operations are.
  async_value=int(local);u.mem_write(O+0xc,bytes((async_value,)))
  queues=(T+0x1c,T+0x3c) if im.label=='gold' else (T+0x18,T+0x2c)
  map_offset,count_offset=(0x10,0x1c) if im.label=='gold' else (4,0x10)
  words(T+(0x18 if im.label=='gold' else 0x14),T+0x80)
  for index,queue in enumerate(queues):words(queue+map_offset,MAP+index*0x40,4,0,0)
  original=bytes(u.mem_read(O,obj_size))
  call(exports['WePlayLocalSendOracle'],[SEND[im.label],T,O],cdecl=True)
  if locks!=['EnterCriticalSection','LeaveCriticalSection']:raise RuntimeError('native queue lock lifetime')
  selected=queues[async_value];other=queues[1-async_value]
  if word(selected+count_offset)!=1 or word(other+count_offset)!=0:raise RuntimeError('native queue selection/count')
  block=word(MAP+async_value*0x40);queued=word(block)
  if queued==O or queued not in allocs or word(queued)!=word(O):raise RuntimeError('native queue did not take an owned clone')
  if u.mem_read(queued+0x35,1)!=bytes((int(ready),)):raise RuntimeError('native clone lost observed Ready field')
  check_castle(queued)
  if bytes(u.mem_read(O,obj_size))!=original or bytes(u.mem_read(queued+4,0x1c))!=original[4:0x20]:raise RuntimeError('native send changed event scalars')
  queued_name=word(queued+0x20)
  if len(text) and (queued_name==word(O+0x20) or bytes(u.mem_read(queued_name,len(text)+1))!=text+b'\0'):raise RuntimeError('queued string ownership')
  # Queue consumer owns the clone and uses delete flag one. Original storage
  # remains independently valid and is destructed below with flag zero.
  call(im.tables[m.EVENTS[0]][1][0],[1],queued)
  if bytes(u.mem_read(O,obj_size))!=original:raise RuntimeError('clone destruction changed borrowed input')
  del allocs[block]  # Owned native deque block, not an event allocation.
  # Original virtual destructor, with native delete flag zero for caller-owned storage.
  call(im.tables[m.EVENTS[0]][1][0],[0],O)
  if allocs:raise RuntimeError('Native destructor left owned allocations')
  return {'ok':True,'subtype':subtype,'connection_zero':connection==0,'local':local,'host':host,'name_length':len(text),'ready':ready}
 except Exception as e:raise RuntimeError(im.label+' '+str(e)+' near '+str([hex(x) for x in last])) from e
def main():
 parser=argparse.ArgumentParser(description=__doc__)
 for label in PROFILES:parser.add_argument('--'+label.replace('_','-'),required=True,type=Path)
 parser.add_argument('--thunk-executable',required=True,type=Path)
 parser.add_argument('output',type=Path)
 args=parser.parse_args();root=Path(__file__).resolve().parents[2];output=args.output.resolve()
 if not output.is_relative_to((root/'work').resolve()) or output.exists() or not output.parent.is_dir():parser.error('Use a new file in an existing repository work directory')
 thunk_raw=args.thunk_executable.read_bytes();thunk=pefile.PE(data=thunk_raw)
 if thunk.FILE_HEADER.Machine!=0x14c or thunk.OPTIONAL_HEADER.ImageBase!=0x18000000 or thunk.OPTIONAL_HEADER.SizeOfImage>16*1024*1024:raise ValueError('Expected the bounded x86 participant test executable')
 required={'WePlayGoldAddedOracle','WePlayGoldConfirmedOracle','WePlayHistoryOracle','WePlayLocalSendOracle'}
 if not required.issubset({entry.name.decode('ascii') for entry in thunk.DIRECTORY_ENTRY_EXPORT.symbols if entry.name}):raise ValueError('Missing compiled thunks')
 result={};pins={};total=0
 for label,p in PROFILES.items():
  path=getattr(args,label);im=m.Image(label,path);pins[label]=im.pin;count=0
  pe=pefile.PE(str(path));imports=[(imp.name.decode() if imp.name else '',imp.address) for library in pe.DIRECTORY_ENTRY_IMPORT for imp in library.imports];del pe
  for subtype in (2,3,4,44):
   for local in (False,True):
    for host in (False,True):
     for connection in ((1,0x12345678) if label=='gold' else (0,0x12345678)):
      for length in (0,1,64):
       execute(im,imports,thunk,*p,subtype=subtype,text=b'x'*length,connection=connection,local=local,host=host)
       count+=1
       if count%8==0:gc.collect()
  for local in (False,True):
   for host in (False,True):
    for connection in ((1,0x12345678) if label=='gold' else (0,0x12345678)):
     execute(im,imports,thunk,*p,subtype=3,connection=connection,local=local,host=host,castle_parts=True)
     count+=1;gc.collect()
  ready_cases=0
  if label!='gold':
   for local in (False,True):
    for host in (False,True):
     for ready in (False,True):
      execute(im,imports,thunk,*p,subtype=3,connection=0 if local else 42,local=local,host=host,castle_parts=True,ready=ready)
      count+=1;ready_cases+=1;gc.collect()
  result[label]={'construction_and_destruction_cases':count,'native_local_queue_cases':count,'nonempty_castle_cases':8+ready_cases,'ready_field_cases':ready_cases,'passed':True};total+=count
  print(json.dumps({'profile':label,'passed_cases':count}),flush=True)
 receipt={'format':'native-participant-local-queue-v2','image_fingerprints':pins,
  'compiled_thunk_sha256':hashlib.sha256(thunk_raw).hexdigest(),'profiles':result,
  'original_native_calls':5*total,'compiled_thunk_calls':2*total,'emulated_heap':True,
  'native_clone_and_queue_executed':True,'emulated_lock_imports':True,
  'synthetic_empty_native_deques':True,'native_queue_consumer_executed':False,
  'original_empty_castle_only':False,'three_part_castles':True,'original_destructors':True,'actual_game_test':False,
  'session_admission_implemented':False,'crossplay_qualified':False}
 with output.open('x',encoding='utf-8') as stream:json.dump(receipt,stream,indent=2)
 print(json.dumps(receipt))
if __name__=='__main__':main()
