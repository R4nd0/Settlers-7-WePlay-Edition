#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "crossplay_history_host_guest_row.h"
#include <cstdio>
#include <cstdlib>

namespace {
unsigned checks=0,prepared=0,claimed=0;void* expected=nullptr;bool return_value=true;
void Check(bool ok){++checks;if(!ok){printf("FAIL History host guest-row check %u\n",checks);std::exit(1);}}
void __fastcall Prepare(void* configuration,void*,uint32_t slot){Check(configuration==expected && slot==2);++prepared;}
bool __fastcall Claim(void* configuration,void*,uint32_t slot,uint32_t network_slot){
    Check(configuration==expected && slot==2 && network_slot==2);++claimed;return return_value;
}
}
extern "C" __declspec(dllexport) uint32_t __cdecl ProductionHistoryHostGuestRowGuardOracle(uint32_t edition){
    return edition==3 && CrossplayNative::HistoryHostGuestRowDetail::Methods()?1u:0u;
}
int main(){
    using namespace CrossplayNative::HistoryHostGuestRowDetail;
    uint32_t configuration[4]{};expected=configuration;uintptr_t before=0,after=1;
    __asm {mov before,esp}
    PrepareWith(reinterpret_cast<uintptr_t>(&Prepare),configuration,2);
    Check(ClaimWith(reinterpret_cast<uintptr_t>(&Claim),configuration,2,2));
    return_value=false;Check(!ClaimWith(reinterpret_cast<uintptr_t>(&Claim),configuration,2,2));
    __asm {mov after,esp}
    Check(before==after && prepared==1 && claimed==2);
    std::vector<unsigned char> original(0x8dc0,0xa5),actual=original;
    constexpr size_t changed=2*0x5a8+0x5c;
    CrossplayChat::Put(actual.data()+changed,2);CrossplayChat::Put(actual.data()+changed+4,3);
    Check(Preserved(original.data(),actual.data(),actual.size()));
    for(size_t offset:{size_t(0),size_t(0x5a8+0x5c),size_t(2*0x5a8+0x18),size_t(2*0x5a8+0x64),
                       size_t(2*0x5a8+0x71),size_t(2*0x5a8+0x55c),size_t(24*0x5a8),size_t(0x8db8)}){
        actual[offset]^=1;Check(!Preserved(original.data(),actual.data(),actual.size()));actual[offset]^=1;
    }
    Check(!Preserved(nullptr,actual.data(),actual.size()));Check(!Preserved(original.data(),actual.data(),actual.size()-1));
    CrossplayChat::Put(actual.data()+changed,1);Check(!Preserved(original.data(),actual.data(),actual.size()));
    CrossplayNative::ParticipantEvent binding;CrossplayNative::LobbyAdmission admission;
    CrossplayNative::HistoryHostGuestRowClaim row;SetLastError(1971);
    Check(!row.Begin(binding,admission));Check(!row.Applied(binding,admission));Check(GetLastError()==1971);
    printf("PASS %u History host guest-row ABI/stack/preservation/scope checks; authored only, no native row or Ready qualified.\n",checks);return 0;
}
