#include "crossplay_shared_engine_mode.h"

#include <cstdio>
namespace {
unsigned char flag=1;unsigned setters=0,getters=0;
__declspec(noinline) unsigned char __cdecl Get(){++getters;return flag;}
__declspec(noinline) void __cdecl Zero(){++setters;flag=0;_mm_setcsr(0x3f80);SetLastError(0x1234);}
}
extern "C" __declspec(dllexport) uint32_t __cdecl WePlaySharedModeGuardOracle(uint32_t edition) {
    return SharedEngineMode::Guard(SharedEngineMode::Pin(static_cast<Crossplay::Edition>(edition)))?1u:0u;
}
int main() {
    using namespace SharedEngineMode;
    Profile p{reinterpret_cast<uintptr_t>(&flag),reinterpret_cast<uintptr_t>(&Get),reinterpret_cast<uintptr_t>(&Zero),8,8,Hash(reinterpret_cast<uintptr_t>(&Get),8),Hash(reinterpret_cast<uintptr_t>(&Zero),8),true};
    bool changed=true;const auto thread=GetCurrentThreadId();SetLastError(0x7777);const auto sse=_mm_getcsr();
    if(!Apply(p,thread,changed)||!changed||flag||setters!=1||getters!=2||GetLastError()!=0x7777||_mm_getcsr()!=sse)return 2;
    if(!Apply(p,thread,changed)||changed||setters!=1)return 3;
    flag=1;const auto calls=getters;if(Apply(p,thread^1,changed)||changed||flag!=1||setters!=1||getters!=calls)return 4;
    p.gold=false;if(Apply(p,thread,changed)||changed||flag!=1||setters!=1)return 5;
    flag=0;if(!Apply(p,thread,changed)||changed||setters!=1)return 6;
    flag=2;if(Apply(p,thread,changed)||changed||flag!=2||setters!=1)return 7;
    flag=1;p.gold=true;p.getter_hash^=1;if(Apply(p,thread,changed)||flag!=1||setters!=1)return 8;
    puts("PASS fresh-mode helper with modeled original accessors: Gold calls original zero setter once, History verifies existing zero; wrong-owner/unknown-byte/method-mutation rejected, LastError and FP state preserved. No game started.");return 0;
}
