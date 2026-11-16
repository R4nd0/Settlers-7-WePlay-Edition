#pragma once
#include "crossplay_gold_logistics_sort.h"
namespace GoldLogisticsInline {
using namespace CrossplayNativeCodec;
inline constexpr uintptr_t Site=0x91b747,Resume=0x91b750;
inline uintptr_t resume=Resume;
inline DWORD owner=0;inline bool (*ready)()=nullptr;
inline uint32_t (*policy)(const GoldLogisticsBinding::World&,const GoldLogisticsBinding::Request&,const GoldLogisticsBinding::Request&)=nullptr;
inline bool installed=false;
inline unsigned observed=0,excluded=0;
inline bool Guard();
inline bool TaskRequest(const GoldLogisticsBinding::World& world,uint32_t task,GoldLogisticsBinding::Request& out) {
 uint32_t reservation=0;
 return task && GoldLogisticsBinding::Word(task+0x70,reservation)
  && GoldLogisticsBinding::CaptureRequest(world,reservation,out) && out.ordinary_request && out.task==task;
}
inline uint32_t __stdcall Prepare(const uint32_t* frame) {
 GoldLogisticsBinding::ErrorScope error;
 if(!frame || owner!=GetCurrentThreadId() || !ready || !ready())return 2;
 ULONG_PTR low=0,high=0;GetCurrentThreadStackLimits(&low,&high);
 const auto at=reinterpret_cast<uintptr_t>(frame);
 if(at<low || at>high-36 || frame[3]!=at+32)return 2;
 GoldLogisticsBinding::World world{};GoldLogisticsBinding::Request left{},right{};
 if(!GoldLogisticsBinding::CaptureWorld(world) || !TaskRequest(world,frame[4],left) || !TaskRequest(world,frame[7],right)) {++excluded;return 2;}
 ++observed;if(!policy)return 2;
 const uint32_t value=policy(world,left,right);return value<=1?value:2;
}
__declspec(naked) inline void Bridge() {
 __asm {
  pushfd
  pushad
  cld
  mov ebp,esp
  sub esp,528
  and esp,0xfffffff0
  fxsave [esp]
  push ebp
  call Prepare
  mov [ebp+12],eax
  fxrstor [esp]
  mov esp,ebp
  popad
  popfd
  // Exact original nine bytes. EBX and native CMP flags retain their result.
  mov ebx,[ebx+14h]
  cmp ebx,[eax+14h]
  setl al
  pushfd
  cmp dword ptr [esp-20],2
  jae Keep
  mov al,byte ptr [esp-20]
 Keep:
  popfd
  jmp dword ptr [resume]
 }
}
inline bool Install(DWORD thread,bool (*predicate)()) {
 GoldLogisticsBinding::ErrorScope error;LoadProgressDetail::FpScope fp;
 if(installed || thread!=GetCurrentThreadId() || !predicate || resume!=Resume
  || GoldLogisticsBinding::logic_global!=0x10f8354 || !Guard())return false;
 const unsigned char expected[]={0x8b,0x5b,0x14,0x3b,0x58,0x14,0x0f,0x9c,0xc0};
 if(!Detail::Readable(reinterpret_cast<void*>(Site),sizeof(expected)) || memcmp(reinterpret_cast<void*>(Site),expected,sizeof(expected)))return false;
 unsigned char patch[sizeof(expected)]{};memset(patch,0x90,sizeof(patch));patch[0]=0xe9;
 const int32_t delta=static_cast<int32_t>(reinterpret_cast<uintptr_t>(&Bridge)-(Site+5));memcpy(patch+1,&delta,4);
 DWORD protection=0;if(!VirtualProtect(reinterpret_cast<void*>(Site),sizeof(patch),PAGE_EXECUTE_READWRITE,&protection))return false;
 memcpy(reinterpret_cast<void*>(Site),patch,sizeof(patch));const bool flushed=FlushInstructionCache(GetCurrentProcess(),reinterpret_cast<void*>(Site),sizeof(patch))!=FALSE;
 DWORD ignored=0;const bool restored=VirtualProtect(reinterpret_cast<void*>(Site),sizeof(patch),protection,&ignored)!=FALSE;
 if(!flushed || !restored) {
  DWORD temporary=0;if(!VirtualProtect(reinterpret_cast<void*>(Site),sizeof(patch),PAGE_EXECUTE_READWRITE,&temporary))__fastfail(7);
  memcpy(reinterpret_cast<void*>(Site),expected,sizeof(expected));
  const bool back=FlushInstructionCache(GetCurrentProcess(),reinterpret_cast<void*>(Site),sizeof(expected))!=FALSE;
  const bool protected_again=VirtualProtect(reinterpret_cast<void*>(Site),sizeof(expected),protection,&ignored)!=FALSE;
  if(!back || !protected_again)__fastfail(7);return false;
 }
 owner=thread;ready=predicate;installed=true;return true;
}
}
#include "crossplay_gold_logistics_inline_guards.h"
