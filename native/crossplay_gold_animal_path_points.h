// Opt-in Gold/History candidate; ordinary adapters do not include this module.
// Caller binds the exact executable and authenticated fresh-world owner.
// Install before GoldAnimalMovementTime while its original byte guards match.
// Only a typed registered animal may replace its temporary native stack point;
// original path search, smoothing, append/allocation, RNG and CRC remain active.
// Diagnostics are a caller-owned handle with32 changed-point records maximum.
#pragma once
#include "crossplay_gold_animal_movement_time.h"
#include "crossplay_path_point_math.h"
namespace GoldAnimalPathPoints {
using namespace CrossplayNativeCodec;
inline uintptr_t original=0x4efe60,first=0x746fef,second=0x747028;
inline DWORD owner=0;inline bool (*ready)()=nullptr;inline bool installed=false;
inline HANDLE report=INVALID_HANDLE_VALUE;inline unsigned applied=0,excluded=0,records=0;
inline bool Guard();
inline bool __stdcall Prepare(const uint32_t* frame) {
 const DWORD error=GetLastError();bool accepted=false;
 if(owner==GetCurrentThreadId() && ready && ready()) {
  ULONG_PTR low=0,high=0;GetCurrentThreadStackLimits(&low,&high);
  const uintptr_t native=frame?frame[2]:0,base=reinterpret_cast<uintptr_t>(frame)+40;
  const bool plus=frame && frame[9]==second+5;
  if(frame && (frame[9]==first+5 || plus) && native>=low+0x60 && native<=high-16
   && base==native-0x30 && frame[5]==native-0x24 && frame[0]>=8
   && GoldAnimalMovementTime::AnimalMovement(frame[0]-8)) {
   uint32_t logic=0,system=0,bound=0;
   if(GoldAnimalMovementTime::Word(GoldAnimalMovementTime::logic_global,logic)
    && GoldAnimalMovementTime::Word(logic+0x24,system)
    && GoldAnimalMovementTime::Word(native+8,bound) && system && bound==system) {
    float ratio=0,cx=0,cy=0,px=0,py=0;
    memcpy(&ratio,reinterpret_cast<void*>(native-0x10),4);
    memcpy(&cx,reinterpret_cast<void*>(native+0xc),4);
    memcpy(&cy,reinterpret_cast<void*>(native-8),4);
    memcpy(&px,reinterpret_cast<void*>(native-4),4);
    memcpy(&py,reinterpret_cast<void*>(native-0xc),4);
    if(std::isfinite(ratio) && ratio>=0 && ratio<=1
     && std::isfinite(cx) && cx>=0 && cx<=1000000 && std::isfinite(cy) && cy>=0 && cy<=1000000
     && std::isfinite(px) && px>=0 && px<=1000000 && std::isfinite(py) && py>=0 && py<=1000000) {
     float point[2]{};HistoryPathPointMath::Point(ratio,cx,cy,px,py,plus,point);
     uint32_t before[2]{},after[2]{};memcpy(before,reinterpret_cast<void*>(native-0x24),8);memcpy(after,point,8);
     memcpy(reinterpret_cast<void*>(native-0x24),point,8);accepted=true;++applied;
     if(report!=INVALID_HANDLE_VALUE && records<32 && memcmp(before,after,8)) {
      char line[256]{};int n=sprintf_s(line,"{\"applied\":%u,\"plus\":%s,\"gold\":[%u,%u],\"history\":[%u,%u]}\n",applied,plus?"true":"false",before[0],before[1],after[0],after[1]);
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
  sub esp,528
  and esp,0xfffffff0
  fxsave [esp]
  push ebp
  call Prepare
  fxrstor [esp]
  mov esp,ebp
  popad
  popfd
  jmp dword ptr [original]
 }
}
inline bool Install(DWORD thread,bool (*predicate)(),HANDLE diagnostics=INVALID_HANDLE_VALUE) {
 const DWORD error=GetLastError();LoadProgressDetail::FpScope fp;
 if(installed || original!=0x4efe60 || first!=0x746fef || second!=0x747028
  || thread!=GetCurrentThreadId() || !predicate || !Guard())return false;
 unsigned char before[2][5]{};const uintptr_t sites[2]={first,second};
 for(unsigned i=0;i<2;++i){
  memcpy(before[i],reinterpret_cast<void*>(sites[i]),5);int32_t previous=0;memcpy(&previous,before[i]+1,4);
  if(before[i][0]!=0xe8 || sites[i]+5+previous!=original)return false;
 }
 MEMORY_BASIC_INFORMATION a{},b{};
 if(VirtualQuery(reinterpret_cast<void*>(first),&a,sizeof(a))!=sizeof(a) || VirtualQuery(reinterpret_cast<void*>(second),&b,sizeof(b))!=sizeof(b) || a.Protect!=b.Protect)return false;
 const SIZE_T bytes=second+5-first;DWORD protection=0;
 if(!VirtualProtect(reinterpret_cast<void*>(first),bytes,PAGE_EXECUTE_READWRITE,&protection))return false;
 for(unsigned i=0;i<2;++i){const int32_t delta=static_cast<int32_t>(reinterpret_cast<uintptr_t>(&Bridge)-(sites[i]+5));memcpy(reinterpret_cast<void*>(sites[i]+1),&delta,4);}
 const bool flushed=FlushInstructionCache(GetCurrentProcess(),reinterpret_cast<void*>(first),bytes)!=0;DWORD ignored=0;
 const bool restored=VirtualProtect(reinterpret_cast<void*>(first),bytes,protection,&ignored)!=0;
 if(!flushed || !restored){
  DWORD temporary=0;if(VirtualProtect(reinterpret_cast<void*>(first),bytes,PAGE_EXECUTE_READWRITE,&temporary)){
   for(unsigned i=0;i<2;++i)memcpy(reinterpret_cast<void*>(sites[i]),before[i],5);
   FlushInstructionCache(GetCurrentProcess(),reinterpret_cast<void*>(first),bytes);VirtualProtect(reinterpret_cast<void*>(first),bytes,protection,&ignored);
  }
  SetLastError(error);return false;
 }
 owner=thread;ready=predicate;report=diagnostics;installed=true;SetLastError(error);return true;
}
}
namespace GoldAnimalPathPoints {
inline bool Guard() {
 if(!GoldAnimalMovementTime::Guard())return false;
 struct Region {uintptr_t at;unsigned bytes;uint64_t hash;};
 static constexpr Region regions[]={
 {0x746f40,288,0x13bde8a1351fef11ull},
 {0x746db0,307,0x8eb9d18f6795e437ull},
 {0x746ef0,75,0x82a3fd0d00f856bbull},
 {0x73cc30,245,0x1ba0853fb144bceull},
 {0x4efe60,95,0x59c50d5b62c538bull},
 {0xf245b8,8,0xa94c4232288a6844ull},
 {0xf62290,8,0xaae7e93229e886a8ull},
 {0xf62800,8,0x8ee534a90425d7a7ull},
 };
 for(const auto& p:regions)if(GoldAnimalMovementTime::Hash(p.at,p.bytes)!=p.hash)return false;
 return true;
}
}

