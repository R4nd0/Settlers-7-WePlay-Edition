#pragma once
// Opt-in Gold/History candidate; ordinary adapters do not include it.
// The caller binds the exact executable and authenticated fresh-world owner.
// Only a typed animal registered in the original world manager may use the
// History float quotient; original movement, conversion and CRC remain active.
// Optional diagnostics are a caller-owned handle, bounded to32 scalar records.
#include "crossplay_load_progress_codec.h"
#include "crossplay_movement_time_math.h"
#include <cmath>
namespace GoldAnimalMovementTime {
using namespace CrossplayNativeCodec;
inline uintptr_t original=0xbaf0e0,site=0x73ce56,logic_global=0x10f8354;
inline DWORD owner=0;inline bool (*ready)()=nullptr;inline bool installed=false;
inline uint32_t applied=0,excluded=0;
inline HANDLE report=INVALID_HANDLE_VALUE;inline unsigned records=0;
inline bool Guard();
inline bool Word(uintptr_t at,uint32_t& value){
 if(!Detail::Readable(reinterpret_cast<void*>(at),4))return false;
 memcpy(&value,reinterpret_cast<void*>(at),4);return true;
}
inline uint64_t Hash(uintptr_t at,unsigned size){
 if(!Detail::Readable(reinterpret_cast<void*>(at),size))return 0;
 uint64_t value=14695981039346656037ull;
 for(unsigned i=0;i<size;++i)value=(value^reinterpret_cast<const unsigned char*>(at)[i])*1099511628211ull;
 return value;
}
inline bool AnimalMovement(uintptr_t movement) {
 uint32_t table=0,id=0,logic=0,manager=0,begin=0,end=0,identity=0,object=0;
 if(movement<0x2c || !Word(movement,table) || table!=0xf404b0)return false;
 const uintptr_t animal=movement-0x2c;
 if(!Word(animal,table) || table!=0xf3f454 || !Word(animal+0x28,id)
  || !Word(logic_global,logic) || !Word(logic,table) || table!=0xf317e0
  || !Word(logic+0xfc,manager) || !Word(manager,table) || table!=0xf3f784
  || !Word(manager+0x24,begin) || !Word(manager+0x28,end)
  || begin>end || (end-begin)%8 || (end-begin)/8>65536 || (id&65535u)>=(end-begin)/8
  || !Word(begin+(id&65535u)*8,identity) || identity!=id
  || !Word(begin+(id&65535u)*8+4,object) || object!=animal)return false;
 return true;
}
inline bool __stdcall Prepare(const uint32_t* frame,float* result) {
 const DWORD error=GetLastError();bool accepted=false;
 if(owner==GetCurrentThreadId() && ready && ready()) {
  ULONG_PTR low=0,high=0;GetCurrentThreadStackLimits(&low,&high);
  const uintptr_t native=frame?frame[2]:0,base=reinterpret_cast<uintptr_t>(frame)+40;
  if(frame && frame[9]==site+5 && native>=low+0x60 && native<=high-16
   && base==((native&~uintptr_t(7))-0x50) && AnimalMovement(frame[1])) {
   float distance=0,multiplier=0;
   memcpy(&distance,reinterpret_cast<void*>(base+0x18),4);
   memcpy(&multiplier,reinterpret_cast<void*>(base+0x14),4);
   if(std::isfinite(distance) && distance>=0 && distance<=1000000.0f
    && std::isfinite(multiplier) && multiplier>0 && multiplier<=10000.0f) {
    const float value=MovementTimeMath::Divide(distance,multiplier);
    if(std::isfinite(value) && value>=0 && value<2147483648.0f){
     *result=value;accepted=true;++applied;
     // Bounded scalar diagnostic only; it does not select or change the result.
     const int legacy=_mm_cvttsd_si32(_mm_div_sd(_mm_set_sd(distance),_mm_set_sd(multiplier)));
     const int history=_mm_cvtt_ss2si(_mm_set_ss(value));
     if(report!=INVALID_HANDLE_VALUE && records<32 && legacy!=history) {
      uint32_t distance_bits=0,multiplier_bits=0;memcpy(&distance_bits,&distance,4);memcpy(&multiplier_bits,&multiplier,4);
      char line[256]{};const int n=sprintf_s(line,"{\"applied_calls\":%u,\"excluded_calls\":%u,\"distance_bits\":%u,\"multiplier_bits\":%u,\"legacy_ms\":%d,\"history_ms\":%d}\n",applied,excluded,distance_bits,multiplier_bits,legacy,history);
      DWORD written=0;if(n>0 && WriteFile(report,line,n,&written,nullptr) && written==static_cast<DWORD>(n))++records;
     }
    }
   }
  }
  if(!accepted)++excluded;
 }
 SetLastError(error);return accepted;
}
__declspec(naked) inline void Bridge() {
 __asm {
  pushfd
  pushad
  mov ebp,esp
  sub esp,544
  and esp,0xfffffff0
  fxsave [esp]
  lea eax,[esp+512]
  push eax
  push ebp
  call Prepare
  test eax,eax
  jz Forward
  fxrstor [esp]
  fstp st(0)
  fld dword ptr [esp+512]
  mov esp,ebp
  popad
  popfd
  jmp dword ptr [original]
 Forward:
  fxrstor [esp]
  mov esp,ebp
  popad
  popfd
  jmp dword ptr [original]
 }
}
inline bool Install(DWORD thread,bool (*predicate)(),HANDLE diagnostics=INVALID_HANDLE_VALUE) {
 const DWORD error=GetLastError();LoadProgressDetail::FpScope fp;
 if(installed || original!=0xbaf0e0 || site!=0x73ce56 || logic_global!=0x10f8354
  || thread!=GetCurrentThreadId() || !predicate || !Guard())return false;
 unsigned char before[5]{};memcpy(before,reinterpret_cast<void*>(site),5);
 int32_t previous=0;memcpy(&previous,before+1,4);if(before[0]!=0xe8 || site+5+previous!=original)return false;
 DWORD protection=0;if(!VirtualProtect(reinterpret_cast<void*>(site),5,PAGE_EXECUTE_READWRITE,&protection))return false;
 const int32_t delta=static_cast<int32_t>(reinterpret_cast<uintptr_t>(&Bridge)-(site+5));memcpy(reinterpret_cast<void*>(site+1),&delta,4);
 const bool flushed=FlushInstructionCache(GetCurrentProcess(),reinterpret_cast<void*>(site),5)!=0;DWORD ignored=0;
 const bool restored=VirtualProtect(reinterpret_cast<void*>(site),5,protection,&ignored)!=0;
 if(!flushed || !restored) {
  DWORD temporary=0;if(VirtualProtect(reinterpret_cast<void*>(site),5,PAGE_EXECUTE_READWRITE,&temporary)) {
   memcpy(reinterpret_cast<void*>(site),before,5);FlushInstructionCache(GetCurrentProcess(),reinterpret_cast<void*>(site),5);
   VirtualProtect(reinterpret_cast<void*>(site),5,protection,&ignored);
  }
  SetLastError(error);return false;
 }
 owner=thread;ready=predicate;report=diagnostics;installed=true;SetLastError(error);return true;
}
}

namespace GoldAnimalMovementTime {
inline bool Guard(){
struct Region{uintptr_t at;unsigned size;uint64_t hash;};
static constexpr Region regions[]={
 {0x73cd30,877,0xedc9d8f2823f8fcbull},
 {0xbaf0e0,171,0x4846720ec908da45ull},
 {0xf404b0,28,0xb26e425a8833a715ull},
 {0x73d0a0,4,0x922bac28691e026ull},
 {0x73d0b0,4,0xdc057d81441bfbf2ull},
 {0x73d0d0,23,0xda72c2a4b06eb4edull},
 {0x73d0c0,15,0x446ef3844dba67d5ull},
 {0x73c9e0,4,0x1c797ef6febd9160ull},
 {0x73c9f0,4,0xdacdd58143140c36ull},
 {0x8b0270,6638,0x34dc0d1b42a177e1ull},
 {0x57f1c0,1026,0x8effde48a9e8adacull},
 {0x728d30,139,0x5e90e6c3f89f78e1ull},
 {0x724b70,20,0xe4a149c8ead99d63ull},
 {0x724110,4,0x804c2c2859e7d32ull},
 {0x8cc410,215,0x15d559d7e8b81a9ull},
 {0xf3f454,84,0xc5681fe1b1e27cf8ull},
 {0xf3f784,4,0x2e51d1c27b341e04ull},
};
for(const auto& p:regions)if(Hash(p.at,p.size)!=p.hash)return false;
return true;
}
}
