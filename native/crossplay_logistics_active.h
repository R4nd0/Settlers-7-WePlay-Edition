#pragma once
#include "crossplay_gold_logistics_inline.h"
#include "crossplay_native_clock.h"
#include "crossplay_logistics_policy.h"
#include "crossplay_logistics_guards.h"
#include "crossplay_neo_agreement.h"
// Explicitly installed native comparator component, not a session controller.
// The owner must first bind the exact executable and authenticate the shared
// feature-19 choice/content/setup (or selected saved source) for its real world.
// Its predicate must remain false outside that owned world. Never stack these
// hooks with History's separate NEO5 implementation. This header does not start
// a world, acknowledge Ready or provide the production UI/lifecycle integration.
namespace CrossplayLogisticsActive {
inline CrossplayLogistics::Ledger ledger{};
inline Crossplay::Edition edition{};inline DWORD owner=0;
inline bool (*ready)()=nullptr;inline bool installed=false,begun=false;
inline bool selected=false;
inline bool resume_armed=false,resume_restored=false;
inline uint64_t resume_unrestored_comparisons=0;
inline std::string resume_payload;
inline bool ArmResume(const std::string& payload){
 if(!installed || begun || resume_armed || owner!=GetCurrentThreadId())return false;
 neo_logistics::SavedState saved;if(!neo_logistics::Decode(payload,saved) || saved.logic_turn<0 || !saved.enabled)return false;
 resume_payload=payload;resume_armed=true;selected=saved.enabled;return true;
}

inline uint64_t comparisons=0,changes=0,excluded=0;
inline unsigned pending_reason=0;inline uint32_t pending_turn=UINT32_MAX;
inline uintptr_t history_entry=0,history_resume=0,history_global=0;
inline uintptr_t history_original=0;
#ifdef S7_CROSSPLAY_LOGISTICS_TESTING
inline uint32_t (*test_decision)(const CrossplayLogisticsBinding::World&,const CrossplayLogisticsBinding::Request&,const CrossplayLogisticsBinding::Request&)=nullptr;
inline bool (*test_clock)(CrossplayNativeClock::Snapshot&)=nullptr;
#endif
inline bool Advance(){
 CrossplayLogisticsBinding::ErrorScope error;
 pending_reason=0;pending_turn=UINT32_MAX;
 if(owner!=GetCurrentThreadId()){pending_reason=1;return false;}
 if(!ready || !ready()){pending_reason=2;return false;}
 CrossplayLogisticsBinding::World world{};CrossplayNativeClock::Snapshot clock{};
 if(!CrossplayLogisticsBinding::CaptureWorld(world)){pending_reason=3;return false;}
 bool read_clock=false;
#ifdef S7_CROSSPLAY_LOGISTICS_TESTING
 if(test_clock)read_clock=test_clock(clock);else
#endif
 read_clock=CrossplayNativeClock::Read(CrossplayNativeClock::Pin(edition),clock);
 if(!read_clock){pending_reason=4;return false;}
 pending_turn=clock.turn;
 if(!begun){
  if(resume_armed){
   neo_logistics::SavedState saved;
   if(!neo_logistics::Decode(resume_payload,saved) || saved.turn!=clock.turn){pending_reason=7;return false;}
   if(!ledger.Restore(world,clock.turn,resume_payload)){pending_reason=8;return false;}
   resume_restored=true;
  }else if(!ledger.Begin(world,clock.turn,selected)){pending_reason=5;return false;}
  begun=true;
 }
 if(!ledger.Advance(world,clock.turn)){pending_reason=6;return false;}return true;
}
inline uint32_t Decide(const CrossplayLogisticsBinding::World& world,const CrossplayLogisticsBinding::Request& left,const CrossplayLogisticsBinding::Request& right){
#ifdef S7_CROSSPLAY_LOGISTICS_TESTING
 if(test_decision)return test_decision(world,left,right);
#endif
 if(!begun && !resume_armed)return 2;
 if(!Advance()){if(resume_armed && !resume_restored)++resume_unrestored_comparisons;return 2;}
 if(!ledger.enabled)return 2;
 const auto result=ledger.Compare(world,left,right);if(result>1){++excluded;return 2;}
 ++comparisons;if((result!=0)!=(static_cast<int32_t>(left.priority)<static_cast<int32_t>(right.priority)))++changes;return result;
}
inline uint32_t Gold(const GoldLogisticsBinding::World&,const GoldLogisticsBinding::Request& left,const GoldLogisticsBinding::Request& right){
 CrossplayLogisticsBinding::ErrorScope error;CrossplayLogisticsBinding::World world{};CrossplayLogisticsBinding::Request a{},b{};
 if(!CrossplayLogisticsBinding::CaptureWorld(world) || !CrossplayLogisticsBinding::CaptureRequest(world,left.reservation,a) || !CrossplayLogisticsBinding::CaptureRequest(world,right.reservation,b)){++excluded;return 2;}
 return Decide(world,a,b);
}
inline void __stdcall PrepareHistory(const uint32_t* frame,uint32_t* output){
 CrossplayLogisticsBinding::ErrorScope error;*output=2;
 if(owner!=GetCurrentThreadId() || !ready || !ready() || (!begun && !resume_armed))return;
 ULONG_PTR low=0,high=0;GetCurrentThreadStackLimits(&low,&high);
 const auto at=reinterpret_cast<uintptr_t>(frame);if(at<low || at>high-64 || frame[3]!=at+32)return;
 CrossplayLogisticsBinding::World world{};CrossplayLogisticsBinding::Request a{},b{};
 if(!CrossplayLogisticsBinding::CaptureWorld(world) || !CrossplayLogisticsBinding::CaptureRecord(world,frame[6],a) || !CrossplayLogisticsBinding::CaptureRecord(world,frame[5],b)){++excluded;return;}
 *output=Decide(world,a,b);
}
__declspec(naked) inline void HistoryReplay(){__asm{
 mov eax,dword ptr [history_global]
 mov eax,dword ptr [eax]
 jmp dword ptr [history_resume]
}}
__declspec(naked) inline void HistoryBridge(){__asm{
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
 call PrepareHistory
 fxrstor [esp]
 lea esp,[ebp-52]
 popad
 popfd
 mov eax,[ebp-4]
 mov edx,[ebp-8]
 mov ecx,[ebp-12]
 push dword ptr [ebp+4]
 popfd
 call dword ptr [history_original]
 pushfd
 cmp dword ptr [ebp-16],2
 jae Keep
 mov al,byte ptr [ebp-16]
 Keep:
 popfd
 mov esp,ebp
 pop ebp
 lea esp,[esp+4]
 ret
}}
inline bool Install(Crossplay::Edition value,DWORD thread,bool (*predicate)()){
 CrossplayLogisticsBinding::ErrorScope error;CrossplayNativeCodec::LoadProgressDetail::FpScope fp;
 if(installed || thread!=GetCurrentThreadId() || !predicate || !CrossplayLogisticsBinding::OriginalGuards(value)
  || !CrossplayNativeClock::Guard(CrossplayNativeClock::Pin(value)))return false;
 edition=value;owner=thread;ready=predicate;CrossplayLogisticsBinding::pin=CrossplayLogisticsBinding::Pin(value);
 if(value==Crossplay::Edition::Gold){
  if(!GoldLogisticsSort::Install(thread,predicate))return false;
  if(!GoldLogisticsInline::Install(thread,predicate)){GoldLogisticsSort::Rollback();return false;}
  GoldLogisticsSort::policy=&Gold;GoldLogisticsInline::policy=&Gold;
 }else{
  history_entry=CrossplayLogisticsBinding::pin.compare;history_resume=history_entry+5;history_global=CrossplayLogisticsBinding::pin.global;history_original=reinterpret_cast<uintptr_t>(&HistoryReplay);
  unsigned char expected[5]={0xa1};memcpy(expected+1,&history_global,4);
  if(!history_entry || memcmp(reinterpret_cast<const void*>(history_entry),expected,5))return false;
  unsigned char patch[5]={0xe9};const int32_t delta=static_cast<int32_t>(reinterpret_cast<uintptr_t>(&HistoryBridge)-(history_entry+5));memcpy(patch+1,&delta,4);
  DWORD protection=0;if(!VirtualProtect(reinterpret_cast<void*>(history_entry),5,PAGE_EXECUTE_READWRITE,&protection))return false;
  memcpy(reinterpret_cast<void*>(history_entry),patch,5);const bool flushed=FlushInstructionCache(GetCurrentProcess(),reinterpret_cast<void*>(history_entry),5)!=FALSE;DWORD ignored=0;
  const bool restored=VirtualProtect(reinterpret_cast<void*>(history_entry),5,protection,&ignored)!=FALSE;
  if(!flushed || !restored){DWORD tmp=0;if(!VirtualProtect(reinterpret_cast<void*>(history_entry),5,PAGE_EXECUTE_READWRITE,&tmp))__fastfail(7);memcpy(reinterpret_cast<void*>(history_entry),expected,5);const bool back=FlushInstructionCache(GetCurrentProcess(),reinterpret_cast<void*>(history_entry),5)!=FALSE;const bool guarded=VirtualProtect(reinterpret_cast<void*>(history_entry),5,protection,&ignored)!=FALSE;if(!back || !guarded)__fastfail(7);return false;}
 }
 installed=true;return true;
}
}
