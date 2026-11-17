#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#define S7_CAPTURE_CLEANUP_TESTING
#include "crossplay_history_sdk_cleanup.h"
#include <cstdio>
#include <cstdlib>
#include <xmmintrin.h>
#pragma optimize("",off)
namespace {
unsigned checks=0,forwarded=0,probed=0;bool skip=false;void* expected=reinterpret_cast<void*>(0x10203040);
void Check(bool value){++checks;if(!value){printf("FAIL cleanup ABI check %u\n",checks);std::exit(1);}}
bool __cdecl Probe(void* value){Check(value==expected);++probed;SetLastError(444);_mm_setcsr(0x5f80);return skip;}
void __fastcall Original(void* value,void*,uint32_t argument){Check(value==expected && argument==1);Check(_mm_getcsr()==0x1f80);++forwarded;SetLastError(991);}
}
extern "C" __declspec(dllexport) uint32_t __cdecl ProductionHistorySdkCleanupGuardOracle(uint32_t edition){return edition==3 && CrossplayHistorySdkCleanup::MetadataGuard::Methods()?1u:0u;}
extern "C" __declspec(dllexport) uint32_t __cdecl ProductionHistorySdkCleanupOwnedGuardOracle(uint32_t edition){
    using G=CrossplayHistorySdkCleanup::MetadataGuard;if(edition!=3 || !G::Methods())return 0;
    auto* at=reinterpret_cast<unsigned char*>(G::Site);unsigned char old[5]{};memcpy(old,at,5);
    const int32_t delta=static_cast<int32_t>(reinterpret_cast<uintptr_t>(&CrossplayHistorySdkCleanup::MetadataEntry)-(G::Site+5));
    at[0]=0xe8;memcpy(at+1,&delta,4);G::Current().installed=true;
    const bool okay=G::Methods();memcpy(at,old,5);G::Current()={};return okay?1u:0u;
}
int main(){
    CrossplayHistorySdkCleanup::MetadataGuard::test=&Probe;CrossplayHistorySdkCleanup::test_forward=reinterpret_cast<uintptr_t>(&Original);
    using CrossplayHistorySdkCleanup::MetadataEntry;
    for(bool value:{false,true}){
        skip=value;SetLastError(1978);_mm_setcsr(0x1f80);uintptr_t before=0,after=1;
        __asm {
            mov before,esp
            push 1
            mov ecx,expected
            call MetadataEntry
            mov after,esp
        }
        if(before!=after)printf("authored caller stack before=%08x after=%08x skip=%u\n",static_cast<unsigned>(before),static_cast<unsigned>(after),value?1u:0u);
        Check(before==after);Check(_mm_getcsr()==0x1f80);Check(GetLastError()==(value?1978u:991u));
    }
    Check(probed==2 && forwarded==1);CrossplayHistorySdkCleanup::MetadataGuard::test=nullptr;
    Check(!CrossplayHistorySdkCleanup::MetadataGuard::Methods());SetLastError(1978);
    Check(!CrossplayHistorySdkCleanup::MetadataGuard::Install(0,nullptr));Check(GetLastError()==1978);
    printf("PASS %u cleanup wrapper/stack/FP/LastError/original-forwarding checks; authored only, no SDK result or native world qualified.\n",checks);return 0;
}
