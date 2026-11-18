"""Execute original Ready consumers on bounded synthetic lobby objects.

The original Confirmed-data consumer and preparation Ready setter execute.
The optional Gold probe executes the original configuration Ready write.
The guest-role callback and Win32 lock imports are modeled. No queue dispatcher,
backend join, game process, socket or mixed world is executed.
"""
import argparse,gc,json,struct
from pathlib import Path
import pefile
import emulate_event_payloads as native
from unicorn import Uc,UC_ARCH_X86,UC_MODE_32,UC_HOOK_CODE,UC_HOOK_MEM_WRITE
from unicorn.x86_const import UC_X86_REG_EAX,UC_X86_REG_EBX,UC_X86_REG_EBP,UC_X86_REG_ECX,UC_X86_REG_ESI,UC_X86_REG_EDI,UC_X86_REG_ESP,UC_X86_REG_EIP

S,STACK=0x20000000,0x30000000
STOP,ROLE,ENTER,LEAVE=S,S+16,S+32,S+48
MAIN,MANAGER,CONFIG,SESSION,EVENT,HOLDER,VT,PREP,SLOTS,ROWS=(S+x for x in (0x1000,0x2000,0x3000,0x4000,0x5000,0x6000,0x7000,0x8000,0x9000,0x10000))

def execute(image,imports,consumer,setter,main_at,manager_at,slot,ready):
    u=Uc(UC_ARCH_X86,UC_MODE_32);u.mem_map(image.base,(image.size+4095)&~4095);u.mem_write(image.base,image.mapped)
    u.mem_map(S,0x20000);u.mem_map(STACK,0x10000)
    def words(at,*values):u.mem_write(at,struct.pack('<'+'I'*len(values),*values))
    def word(at):return struct.unpack('<I',u.mem_read(at,4))[0]
    words(main_at,MAIN);words(manager_at,MANAGER);words(MAIN+8,MANAGER);words(MAIN+0x14,SESSION)
    words(MANAGER+0xc4+0x98,PREP);words(PREP,ROWS,ROWS);words(SESSION,VT);words(VT+0x58,ROLE)
    words(CONFIG+8,HOLDER);words(HOLDER+4,HOLDER+0x80);words(CONFIG+0x44,SLOTS,SLOTS+8*0x18,SLOTS+8*0x18)
    for index in range(8):words(SLOTS+index*0x18,3,1-ready,index+1,0,0,0)
    u.mem_write(ROWS,b'\xa5'*(25*0x5a8));words(EVENT+0x10,2,42,0x1234,slot)
    u.mem_write(EVENT+0x35,bytes((ready,)))
    for name,at in imports:
        if name in ('EnterCriticalSection','LeaveCriticalSection'):words(at,ENTER if name=='EnterCriticalSection' else LEAVE)
    locks=[];executed=set()
    def ret(count,value=0):
        sp=u.reg_read(UC_X86_REG_ESP);u.reg_write(UC_X86_REG_EAX,value);u.reg_write(UC_X86_REG_EIP,word(sp));u.reg_write(UC_X86_REG_ESP,sp+4+count)
    def code(uc,at,size,data):
        executed.add(at)
        if at==STOP:uc.emu_stop()
        elif at==ROLE:ret(0) # Explicit synthetic guest-role fixture.
        elif at in (ENTER,LEAVE):
            if word(uc.reg_read(UC_X86_REG_ESP)+4)!=HOLDER+0x80:raise RuntimeError('Foreign configuration lock')
            locks.append(at);ret(4)
        elif not image.base<=at<image.base+image.size:raise RuntimeError('Foreign executable')
    def write(uc,access,at,size,value,data):
        if not (STACK<=at<=at+size<=STACK+0x10000 or CONFIG<=at<=at+size<=CONFIG+0x80
                or SLOTS<=at<=at+size<=SLOTS+8*0x18 or ROWS<=at<=at+size<=ROWS+25*0x5a8):raise RuntimeError('Write outside owned target')
    u.hook_add(UC_HOOK_CODE,code);u.hook_add(UC_HOOK_MEM_WRITE,write)
    kept={UC_X86_REG_EBX:0x12345678,UC_X86_REG_EBP:0x23456789,UC_X86_REG_ESI:0x34567890,UC_X86_REG_EDI:0x456789ab}
    def call(at,this,args):
        sp=STACK+0xf000;words(sp,STOP,*args)
        for reg,value in {**kept,UC_X86_REG_ESP:sp,UC_X86_REG_ECX:this}.items():u.reg_write(reg,value)
        u.emu_start(at,STOP+1,count=100000,timeout=2000000)
        if u.reg_read(UC_X86_REG_EIP)!=STOP or u.reg_read(UC_X86_REG_ESP)!=sp+4+4*len(args) or any(u.reg_read(k)!=v for k,v in kept.items()):raise RuntimeError('Original consumer ABI incomplete')
    original_event=bytes(u.mem_read(EVENT,0xa0));original_rows=bytes(u.mem_read(ROWS,25*0x5a8))
    call(consumer,CONFIG,[EVENT]);call(setter,ROWS+slot*0x5a8,[ready])
    if locks!=[ENTER,LEAVE] or consumer not in executed or setter not in executed:raise RuntimeError('Original consumer/lock path incomplete')
    for index in range(8):
        if u.mem_read(SLOTS+index*0x18+4,1)!=bytes((ready if index==slot-1 else 1-ready,)):raise RuntimeError('Incorrect native configuration Ready target')
    expected=bytearray(original_rows);expected[slot*0x5a8+0x71]=ready
    if bytes(u.mem_read(ROWS,len(expected)))!=expected or bytes(u.mem_read(EVENT,0xa0))!=original_event or word(CONFIG+0xc)!=2:raise RuntimeError('Native target/source ownership mismatch')


def execute_gold(image, slot, ready):
    # Run the original configuration write and its lock calls. Stop before
    # the following notification and automatic-session-start branch.
    entry, done = 0x98e303, 0x98e346
    uc = Uc(UC_ARCH_X86, UC_MODE_32)
    uc.mem_map(image.base, (image.size+4095)&~4095)
    uc.mem_write(image.base, image.mapped)
    uc.mem_map(S, 0x20000)
    uc.mem_map(STACK, 0x10000)

    def write_word(address, value):
        uc.mem_write(address, struct.pack('<I', value))

    def read_word(address):
        return struct.unpack('<I', uc.mem_read(address, 4))[0]

    write_word(CONFIG+8, HOLDER)
    write_word(HOLDER+4, HOLDER+0x80)
    write_word(CONFIG+0x48, SLOTS)
    write_word(EVENT+0x10, slot)
    uc.mem_write(EVENT+0x14, bytes((ready,)))
    write_word(0xebf04c, ENTER)
    write_word(0xebf064, LEAVE)
    original = bytearray(8*0x18)
    for index in range(8):
        original[index*0x18+4] = 1-ready
    uc.mem_write(SLOTS, bytes(original))
    stack = STACK+0xf000
    uc.reg_write(UC_X86_REG_ESP, stack)
    uc.reg_write(UC_X86_REG_EBX, CONFIG)
    uc.reg_write(UC_X86_REG_EDI, EVENT)
    locks = []

    def code(cpu,address,_size,_data):
        if address in (ENTER, LEAVE):
            sp = cpu.reg_read(UC_X86_REG_ESP)
            if read_word(sp+4)!=HOLDER+0x80:
                raise RuntimeError('Gold used a foreign configuration lock')
            locks.append(address)
            cpu.reg_write(UC_X86_REG_EIP,read_word(sp))
            cpu.reg_write(UC_X86_REG_ESP,sp+8)
        elif not entry<=address<done:
            raise RuntimeError('Execution left the original Gold Ready write')

    ready_at = (slot-1)*0x18+4
    def write(_cpu,_access,address,size,_value,_data):
        if not (STACK<=address<=address+size<=STACK+0x10000 or (address,size)==(SLOTS+ready_at,1)):
            raise RuntimeError('Gold wrote outside the selected Ready field')

    uc.hook_add(UC_HOOK_CODE, code)
    uc.hook_add(UC_HOOK_MEM_WRITE, write)
    uc.emu_start(entry, done, count=1000, timeout=2000000)
    original[ready_at] = ready
    if (uc.reg_read(UC_X86_REG_EIP)!=done or uc.reg_read(UC_X86_REG_ESP)!=stack
            or locks!=[ENTER,LEAVE] or bytes(uc.mem_read(SLOTS,len(original)))!=original):
        raise RuntimeError('Original Gold Ready write did not update precisely the selected slot')


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    for label in ('ubisoft','steam_analysis'):parser.add_argument('--'+label.replace('_','-'),type=Path,required=True)
    parser.add_argument('--gold',type=Path)
    parser.add_argument('output',type=Path);args=parser.parse_args();root=Path(__file__).resolve().parents[2];out=args.output.resolve()
    if not out.is_relative_to((root/'work').resolve()) or out.exists() or not out.parent.is_dir():parser.error('Use a fresh file under repository work')
    native.EVENTS=();results={}
    for label,consumer,setter,main_at,manager_at in (('ubisoft',0x841c70,0x5862c0,0x13a5210,0x13a5218),('steam_analysis',0x841a40,0x585d80,0x13a230c,0x13a2300)):
        path=getattr(args,label);image=native.Image(label,path);pe=pefile.PE(str(path))
        imports=[(imp.name.decode() if imp.name else '',imp.address) for lib in pe.DIRECTORY_ENTRY_IMPORT for imp in lib.imports];del pe
        cases=0
        for slot in range(1,9):
            for ready in (0,1):execute(image,imports,consumer,setter,main_at,manager_at,slot,ready)
            cases+=2
        results[label]=cases
        print(json.dumps({'profile':label,'original_consumer_and_setter_cases':cases}),flush=True)
        del image;gc.collect()
    if args.gold:
        image=native.Image('gold',args.gold)
        for slot in range(1,9):
            for ready in (0,1):
                execute_gold(image,slot,ready)
        results['gold']=16
        print(json.dumps({'profile':'gold','original_ready_write_cases':16}),flush=True)
        del image;gc.collect()
    receipt={
        'cases':sum(results.values()),
        'profiles':results,
        'original_confirmed_data_consumer_executed':True,
        'original_preparation_setter_executed':True,
        'original_gold_ready_write_executed':bool(args.gold),
        'modeled_guest_role_and_lock_imports':True,
        'native_queue_dispatcher_executed':False,
        'game_process_started':False,
        'mixed_gameplay_qualified':False,
    }
    with out.open('x') as stream:json.dump(receipt,stream,indent=2)
    print(json.dumps(receipt))
if __name__=='__main__':main()
