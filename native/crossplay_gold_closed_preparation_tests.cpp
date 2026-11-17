#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "crossplay_gold_closed_preparation.h"
#include <cstdio>
#include <cstdlib>
namespace {
unsigned checks=0;void* seen_row=nullptr;uint32_t seen_value=0;
void Check(bool value){++checks;if(!value){printf("FAIL Gold closed preparation %u\n",checks);std::exit(1);}}
__declspec(naked) void Scalar(){__asm{mov seen_row,eax
    mov seen_value,ecx
    ret}}
}
extern "C" __declspec(dllexport) uint32_t __cdecl ProductionGoldClosedPreparationGuardOracle(){return CrossplayGoldClosedPreparation::Methods()?1u:0u;}
int main(){
    using namespace CrossplayGoldClosedPreparation;
    uint32_t value=0;uintptr_t before=0,after=1;
    __asm mov before,esp
    SetWith(reinterpret_cast<uintptr_t>(&Scalar),&value,4);
    __asm mov after,esp
    Check(before==after && seen_row==&value && seen_value==4);
    std::vector<unsigned char> rows(25*0x5c4,0);auto original=rows;
    for(uint32_t slot:{3u,4u}){
        const size_t at=slot*0x5c4;
        for(size_t offset:{at+0x1c,at+0x2b,at+0x5c,at+0x64}){rows[offset]^=1;Check(Preserved(original.data(),rows.data(),rows.size(),slot));rows[offset]^=1;}
        for(size_t offset:{at+0x18,at+0x2c,at+0x60,at+0x68,at+0x71,at+0x574,size_t(0x5c4+0x1c)}){rows[offset]^=1;Check(!Preserved(original.data(),rows.data(),rows.size(),slot));rows[offset]^=1;}
    }
    Check(!Preserved(nullptr,rows.data(),rows.size(),3));Check(!Preserved(original.data(),rows.data(),rows.size()-1,3));
    Check(!Preserved(original.data(),rows.data(),rows.size(),2));
    CrossplayNative::ParticipantEvent binding;SetLastError(1982);
    Check(!Project(binding,{1,2},3));Check(GetLastError()==1982);
    CrossplayConfigurationReceipt::Receipt receipt;CrossplayConfigurationReceipt::Interval interval{GetCurrentThreadId(),1,2};
    Check(!Project(binding,{1,2},3,&interval,&receipt));
    uintptr_t session=9,cache=9;
    Check(!CrossplayClosedConfigReceipt::Context(Crossplay::Edition::Gold,binding,{1,2},3,interval,receipt,session,cache));
    Check(!session && !cache);
    Check(!receipt.ConsumedIn(interval,GetTickCount64()));
    printf("PASS %u Gold closed preparation register ABI/scope/preservation checks; authored only, no native consumer/world qualified.\n",checks);return 0;
}
