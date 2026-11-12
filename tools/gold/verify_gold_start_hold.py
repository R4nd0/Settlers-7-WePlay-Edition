"""Verify compiled Gold fresh-start hold ABI and callsite byte guards offline.

Synthetic native roots/events and original callsite continuation are modeled.
Windows imports are modeled; no game starts and no C++ exception is emulated.
Requires pefile/unicorn and a separately owned fingerprinted original Gold image.
"""
from pathlib import Path
import argparse,struct,json,hashlib
import pefile
from unicorn import Uc,UC_ARCH_X86,UC_MODE_32,UC_HOOK_CODE
from unicorn.x86_const import UC_X86_REG_EAX,UC_X86_REG_EBX,UC_X86_REG_ECX,UC_X86_REG_EDX,UC_X86_REG_ESI,UC_X86_REG_EDI,UC_X86_REG_EBP,UC_X86_REG_ESP,UC_X86_REG_EIP
from emulate_event_payloads import Image
from verify_codec_guards import check_image
parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('--gold',type=Path,required=True)
parser.add_argument('--thunk-executable',type=Path,required=True)
parser.add_argument('output',type=Path)
args=parser.parse_args();root=Path(__file__).resolve().parents[2];out=args.output.resolve()
if not out.is_relative_to((root/'work').resolve()) or out.exists() or not out.parent.is_dir():
 parser.error('Use a fresh output under repository work')
raw=args.thunk_executable.read_bytes();pe=pefile.PE(data=raw);base=pe.OPTIONAL_HEADER.ImageBase
if pe.FILE_HEADER.Machine!=0x14c or base!=0x24000000 or pe.OPTIONAL_HEADER.SizeOfImage>4*1024*1024:
 raise ValueError('Expected bounded fixed x86 fixture')
entry=base+next(x.address for x in pe.DIRECTORY_ENTRY_EXPORT.symbols if x.name==b'WePlayGoldStartHoldABICase')
cases=[(0,v) for v in range(14)]
for mode,variant in cases:
 u=Uc(UC_ARCH_X86,UC_MODE_32);u.mem_map(base,(pe.OPTIONAL_HEADER.SizeOfImage+4095)&~4095);u.mem_write(base,pe.get_memory_mapped_image())
 for at in (0xa30000,0x10f0000,0x30000000,0x41000000):u.mem_map(at,0x10000)
 u.mem_map(0,0x1000);last_error=0;stubs={};stop=0x4100f000
 for group in pe.DIRECTORY_ENTRY_IMPORT:
  for item in group.imports:
   address=0x41000100+16*len(stubs);stubs[address]=item.name.decode('ascii') if item.name else str(item.ordinal)
   u.mem_write(item.address,struct.pack('<I',address))
 def word(at):return struct.unpack('<I',u.mem_read(at,4))[0]
 def code(uc,at,size,data):
  global last_error
  if at==stop:uc.emu_stop();return
  if at not in stubs:return
  name=stubs[at];sp=uc.reg_read(UC_X86_REG_ESP);value=0;pop=0
  if name=='GetLastError':value=last_error
  elif name=='SetLastError':last_error=word(sp+4);pop=4
  elif name=='GetCurrentThreadId':value=0x4042
  elif name=='VirtualQuery':
   address,out,length=word(sp+4),word(sp+8),word(sp+12);assert length==28
   mapped=any(start<=address<end+1 for start,end,protection in uc.mem_regions())
   if mapped:uc.mem_write(out,struct.pack('<IIIIIII',address&~4095,address&~4095,4,4096,0x1000,4,0x20000));value=28
   pop=12
  else:raise RuntimeError('Unexpected emulated import '+name)
  uc.reg_write(UC_X86_REG_EAX,value);uc.reg_write(UC_X86_REG_ESP,sp+4+pop);uc.reg_write(UC_X86_REG_EIP,word(sp))
 u.hook_add(UC_HOOK_CODE,code)
 stack=0x3000f000;u.mem_write(stack,struct.pack('<II',stop,variant))
 kept={UC_X86_REG_EBX:0x11111111,UC_X86_REG_ESI:0x22222222,UC_X86_REG_EDI:0x33333333,UC_X86_REG_EBP:0x44444444}
 for reg,value in {**kept,UC_X86_REG_ESP:stack}.items():u.reg_write(reg,value)
 u.emu_start(entry,stop,count=500000,timeout=2000000)
 assert u.reg_read(UC_X86_REG_EIP)==stop and u.reg_read(UC_X86_REG_EAX)==1,(mode,variant,hex(u.reg_read(UC_X86_REG_EIP)),u.reg_read(UC_X86_REG_EAX))
 assert u.reg_read(UC_X86_REG_ESP)==stack+4 and all(u.reg_read(reg)==value for reg,value in kept.items())
 print('PASS ABI mode',mode,'variant',variant,flush=True)
image=Image('gold',args.gold,types=())
guards=check_image(image,pe,1,oracle=b'WePlayGoldStartHoldMethodsOracle',methods=((0xa35eb0,254),))
receipt={'abi_cases':len(cases),'guard_cases':guards,'fixture_sha256':hashlib.sha256(raw).hexdigest(),'original_callsite_continuation_modeled':True,'original_sender_called':False,'windows_imports_modeled':True,'cpp_exception_path_emulated':False,'game_started':False}
with out.open('x') as f:json.dump(receipt,f,indent=2)
print(json.dumps(receipt))
