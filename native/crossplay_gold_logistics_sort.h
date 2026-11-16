#pragma once
#include "crossplay_gold_logistics_binding.h"
namespace GoldLogisticsSort {
using namespace CrossplayNativeCodec;
inline uintptr_t entry=0x91ad00,resume=0x91ad05,caller=0x93b5f3;
inline DWORD owner=0;inline bool (*ready)()=nullptr;
inline uint32_t (*policy)(const GoldLogisticsBinding::World&,const GoldLogisticsBinding::Request&,const GoldLogisticsBinding::Request&)=nullptr;
inline unsigned observed=0,excluded=0;inline bool installed=false;
inline bool Guard();
__declspec(naked) inline void Replay() {
 __asm {
  push ebp
  mov ebp,esp
  mov ecx,[eax]
  jmp dword ptr [resume]
 }
}
inline uintptr_t original=reinterpret_cast<uintptr_t>(&Replay);
inline void __stdcall Prepare(const uint32_t* frame,uint32_t* decision) {
 const DWORD error=GetLastError();*decision=2;
 if(owner==GetCurrentThreadId() && ready && ready()) {
  ULONG_PTR low=0,high=0;GetCurrentThreadStackLimits(&low,&high);
  const uintptr_t native=frame?frame[2]:0;
  uint32_t native_caller=0,right=0;
  GoldLogisticsBinding::World world{};GoldLogisticsBinding::Request left{},other{};
  if(frame && native>=low+0x80 && native<=high-16 && reinterpret_cast<uintptr_t>(frame)==native-52
   && frame[7]>=low && frame[7]<=high-4
   && GoldLogisticsBinding::Word(native+8,native_caller) && native_caller==caller
   && GoldLogisticsBinding::Word(native+12,right)
   && GoldLogisticsBinding::CaptureSort(frame[7],frame[5],right,world,left,other)) {
   ++observed;
   if(policy){const uint32_t value=policy(world,left,other);if(value<=2)*decision=value;}

  }else {++excluded;}
 }
 SetLastError(error);
}
__declspec(naked) inline void Bridge() {
 __asm {
  pushfd
  push ebp
  mov ebp,esp
  sub esp,16
  mov [ebp-4],eax
  mov [ebp-8],edx
  mov [ebp-12],ecx
  pushfd
  pushad
  cld
  mov edx,esp
  sub esp,528
  and esp,0xfffffff0
  fxsave [esp]
  lea eax,[ebp-16]
  push eax
  push edx
  call Prepare
  fxrstor [esp]
  lea esp,[ebp-52]
  popad
  popfd
  push dword ptr [ebp+12]
  mov eax,[ebp-4]
  mov edx,[ebp-8]
  mov ecx,[ebp-12]
  push dword ptr [ebp+4]
  popfd
  call dword ptr [original]
  pushfd
  cmp dword ptr [ebp-16],2
  jae Keep
  mov al,byte ptr [ebp-16]
 Keep:
  popfd
  mov esp,ebp
  pop ebp
  lea esp,[esp+4]
  ret 4
 }
}
inline bool Install(DWORD thread,bool (*predicate)()) {
 const DWORD error=GetLastError();LoadProgressDetail::FpScope fp;
 if(installed || entry!=0x91ad00 || resume!=0x91ad05 || caller!=0x93b5f3
  || GoldLogisticsBinding::logic_global!=0x10f8354 || original!=reinterpret_cast<uintptr_t>(&Replay)
  || thread!=GetCurrentThreadId() || !predicate || !Guard())return false;
 unsigned char before[5]{};memcpy(before,reinterpret_cast<void*>(entry),5);
 constexpr unsigned char expected[]={0x55,0x8b,0xec,0x8b,0x08};if(memcmp(before,expected,5))return false;
 unsigned char patch[5]={0xe9};const int32_t delta=static_cast<int32_t>(reinterpret_cast<uintptr_t>(&Bridge)-(entry+5));memcpy(patch+1,&delta,4);
 DWORD protection=0;if(!VirtualProtect(reinterpret_cast<void*>(entry),5,PAGE_EXECUTE_READWRITE,&protection))return false;
 memcpy(reinterpret_cast<void*>(entry),patch,5);const bool flushed=FlushInstructionCache(GetCurrentProcess(),reinterpret_cast<void*>(entry),5)!=0;DWORD ignored=0;
 const bool restored=VirtualProtect(reinterpret_cast<void*>(entry),5,protection,&ignored)!=0;
 if(!flushed || !restored){
  DWORD temporary=0;if(!VirtualProtect(reinterpret_cast<void*>(entry),5,PAGE_EXECUTE_READWRITE,&temporary))__fastfail(7);
  memcpy(reinterpret_cast<void*>(entry),before,5);
  const bool back=FlushInstructionCache(GetCurrentProcess(),reinterpret_cast<void*>(entry),5)!=FALSE;
  const bool protected_again=VirtualProtect(reinterpret_cast<void*>(entry),5,protection,&ignored)!=FALSE;
  if(!back || !protected_again)__fastfail(7);
  SetLastError(error);return false;
 }
 owner=thread;ready=predicate;installed=true;SetLastError(error);return true;
}
inline void Rollback() {
 constexpr uintptr_t at=0x91ad00;const unsigned char bytes[]={0x55,0x8b,0xec,0x8b,0x08};
 int32_t delta=0;memcpy(&delta,reinterpret_cast<const void*>(at+1),4);
 if(*reinterpret_cast<const unsigned char*>(at)!=0xe9 || at+5+delta!=reinterpret_cast<uintptr_t>(&GoldLogisticsSort::Bridge))__fastfail(7);
 DWORD old=0;if(!VirtualProtect(reinterpret_cast<void*>(at),5,PAGE_EXECUTE_READWRITE,&old))__fastfail(7);
 memcpy(reinterpret_cast<void*>(at),bytes,5);DWORD ignored=0;
 const bool flushed=FlushInstructionCache(GetCurrentProcess(),reinterpret_cast<void*>(at),5)!=FALSE;
 const bool protected_again=VirtualProtect(reinterpret_cast<void*>(at),5,old,&ignored)!=FALSE;
 if(!flushed || !protected_again || !GoldLogisticsSort::Guard())__fastfail(7);
 GoldLogisticsSort::installed=false;GoldLogisticsSort::owner=0;GoldLogisticsSort::ready=nullptr;GoldLogisticsSort::policy=nullptr;
}

}
#include "crossplay_gold_logistics_sort_guards.h"
