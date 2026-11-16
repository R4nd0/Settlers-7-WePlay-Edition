#pragma once
#include "crossplay_logistics_active.h"
namespace CrossplayLogisticsIntent {
inline uintptr_t resume=0;inline DWORD owner=0;inline Crossplay::Edition edition{};
inline bool installed=false;inline uint64_t accepted=0,ignored=0;
struct Profile{uintptr_t entry=0;unsigned prefix=0,stride=0,cell=0,head=0,size=0;uintptr_t recursive=0;};
inline Profile pin{};
inline Profile Pin(Crossplay::Edition value){
 if(value==Crossplay::Edition::Gold)return {0x90abf0,6,0xe0,12,0x28,0x2c,0x90ae04};
 if(value==Crossplay::Edition::UbisoftHistory)return {0x782ee0,5,0xac,8,0x1c,0x20,0x7830ed};
 if(value==Crossplay::Edition::SteamHistory)return {0x782cb0,5,0xac,8,0x1c,0x20,0x782ebd};return {};
}
inline bool Explicit(uintptr_t caller){
 if(edition==Crossplay::Edition::Gold)return caller==0x9002e3;
 const bool steam=edition==Crossplay::Edition::SteamHistory;
 const uintptr_t ubi[]={0x77abc1,0x7bbb9c,0x9e77f6,0xa145c9,0xa146d9,0xa147a5,0xa147de};
 const uintptr_t stm[]={0x77a991,0x7bb96c,0x9e71c6,0xa13a49,0xa13b59,0xa13c25,0xa13c5e};
 for(unsigned i=0;i<7;++i)if(caller==(steam?stm[i]:ubi[i]))return true;return false;
}
inline int Position(const CrossplayLogisticsBinding::World& world,uint32_t task,uint32_t player,uint32_t group){
 using CrossplayLogisticsBinding::Word;uint32_t head=0,size=0,node=0;
 if(player>=25 || group>=8)return -1;
 const auto at=static_cast<uintptr_t>(world.priority)+player*pin.stride+group*pin.cell;
 if(!Word(at+pin.head,head) || !Word(at+pin.size,size) || !size || size>8192 || !Word(head,node))return -1;
 for(uint32_t i=0;i<size && node!=head;++i){uint32_t bound=0;if(!Word(static_cast<uintptr_t>(node)+8,bound))return -1;if(bound==task)return static_cast<int>(i);if(!Word(node,node))return -1;}
 return -1;
}
inline bool Track(const CrossplayLogisticsBinding::World& world,uint32_t id,uint32_t relative,uintptr_t caller,uintptr_t outer){
 using namespace CrossplayLogisticsBinding;
 if(!CrossplayLogisticsActive::begun || !CrossplayLogisticsActive::ledger.Live(world) || !id || id==relative)return false;
 uint32_t task=0,player=0,group=0,birth=0;
 if(!Task(world,id,task) || !Word(task+0x34,player) || !Word(task+0x1c,group) || !Word(task+0x2c,birth))return false;
 player&=255u;if(player>=25 || group>=8)return false;
 auto intent=neo_fifo::Intent::Ordinary;bool explicit_call=Explicit(caller);
 if(!explicit_call && caller==pin.recursive){
  ULONG_PTR low=0,high=0;GetCurrentThreadStackLimits(&low,&high);uint32_t parent_caller=0,parent_id=0,parent_task=0,parent_birth=0;
  if(outer<low || outer>high-12 || !Word(outer+4,parent_caller) || !Explicit(parent_caller) || !Word(outer+8,parent_id)
   || !Task(world,parent_id,parent_task) || !Word(parent_task+0x2c,parent_birth))return false;
  const auto& mark=CrossplayLogisticsActive::ledger.marks[parent_id&65535u];
  if(!mark.Matches(parent_id,parent_birth,parent_task))return false;intent=mark.intent;explicit_call=true;
 }
 if(!explicit_call)return false;
 const int position=Position(world,task,player,group);if(position<0)return false;
 if(intent==neo_fifo::Intent::Ordinary){
  if(!relative)intent=neo_fifo::Intent::Defer;
  else {uint32_t target=0,target_player=0,target_group=0;
   if(!Task(world,relative,target) || !Word(target+0x34,target_player) || (target_player&255u)!=player || !Word(target+0x1c,target_group) || target_group!=group)return false;
   const auto target_position=Position(world,target,player,group);if(target_position<0)return false;
   intent=position>target_position?neo_fifo::Intent::Prefer:neo_fifo::Intent::Defer;
  }
 }
 return CrossplayLogisticsActive::ledger.Mark(id,intent);
}
inline void __cdecl Observe(const uint32_t* frame){
 CrossplayLogisticsBinding::ErrorScope error;
 if(owner!=GetCurrentThreadId() || !CrossplayLogisticsActive::begun || !CrossplayLogisticsActive::ready || !CrossplayLogisticsActive::ready())return;
 ULONG_PTR low=0,high=0;GetCurrentThreadStackLimits(&low,&high);const auto at=reinterpret_cast<uintptr_t>(frame);
 if(at<low || at>high-48 || frame[3]!=at+32)return;
 CrossplayLogisticsBinding::World world{};
 if(!CrossplayLogisticsActive::Advance() || !CrossplayLogisticsBinding::CaptureWorld(world) || frame[6]!=world.priority)return;
 if(Track(world,frame[10],frame[11],frame[9],frame[2]))++accepted;else ++ignored;
}
__declspec(naked) inline void GoldBridge(){__asm{
 pushfd
 pushad
 cld
 mov edi,esp
 sub esp,528
 and esp,0xfffffff0
 fxsave [esp]
 push edi
 call Observe
 add esp,4
 fxrstor [esp]
 mov esp,edi
 popad
 popfd
 push ebp
 mov ebp,esp
 and esp,0xfffffff8
 jmp dword ptr [resume]
}}
__declspec(naked) inline void HistoryBridge(){__asm{
 pushfd
 pushad
 cld
 mov edi,esp
 sub esp,528
 and esp,0xfffffff0
 fxsave [esp]
 push edi
 call Observe
 add esp,4
 fxrstor [esp]
 mov esp,edi
 popad
 popfd
 push ebp
 mov ebp,esp
 push -1
 jmp dword ptr [resume]
}}
inline bool OriginalGuards(Crossplay::Edition value);
inline bool Install(Crossplay::Edition value,DWORD thread){
 CrossplayLogisticsBinding::ErrorScope error;CrossplayNativeCodec::LoadProgressDetail::FpScope fp;
 if(installed || thread!=GetCurrentThreadId() || !CrossplayLogisticsActive::installed
  || CrossplayLogisticsActive::edition!=value || !OriginalGuards(value))return false;
 const auto p=Pin(value);if(!p.entry)return false;
 unsigned char expected[6]={0x55,0x8b,0xec,0x6a,0xff,0};if(value==Crossplay::Edition::Gold){expected[3]=0x83;expected[4]=0xe4;expected[5]=0xf8;}
 if(memcmp(reinterpret_cast<const void*>(p.entry),expected,p.prefix))return false;
 unsigned char patch[6]={0xe9,0,0,0,0,0x90};const auto target=value==Crossplay::Edition::Gold?&GoldBridge:&HistoryBridge;
 const int32_t delta=static_cast<int32_t>(reinterpret_cast<uintptr_t>(target)-(p.entry+5));memcpy(patch+1,&delta,4);
 DWORD protection=0;if(!VirtualProtect(reinterpret_cast<void*>(p.entry),p.prefix,PAGE_EXECUTE_READWRITE,&protection))return false;
 pin=p;resume=p.entry+p.prefix;owner=thread;edition=value;memcpy(reinterpret_cast<void*>(p.entry),patch,p.prefix);
 const bool flushed=FlushInstructionCache(GetCurrentProcess(),reinterpret_cast<void*>(p.entry),p.prefix)!=FALSE;DWORD ignored_protection=0;
 const bool restored=VirtualProtect(reinterpret_cast<void*>(p.entry),p.prefix,protection,&ignored_protection)!=FALSE;
 if(!flushed || !restored){DWORD tmp=0;if(!VirtualProtect(reinterpret_cast<void*>(p.entry),p.prefix,PAGE_EXECUTE_READWRITE,&tmp))__fastfail(7);memcpy(reinterpret_cast<void*>(p.entry),expected,p.prefix);const bool back=FlushInstructionCache(GetCurrentProcess(),reinterpret_cast<void*>(p.entry),p.prefix)!=FALSE;const bool guarded=VirtualProtect(reinterpret_cast<void*>(p.entry),p.prefix,protection,&ignored_protection)!=FALSE;if(!back || !guarded)__fastfail(7);return false;}
 installed=true;return true;
}
}
#include "crossplay_logistics_intent_guards.h"
