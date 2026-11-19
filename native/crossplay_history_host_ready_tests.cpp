#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "crossplay_history_host_ready.h"
#include <cstdio>
#include <cstdlib>
namespace {
unsigned checks=0;uintptr_t seen_output=0;uint32_t seen_subtype=0,seen_source=0;
void Check(bool value){++checks;if(!value){printf("FAIL History host Ready check %u\n",checks);std::exit(1);}}
void* __cdecl Construct(void* output,uint32_t subtype,uint32_t source){
    seen_output=reinterpret_cast<uintptr_t>(output);seen_subtype=subtype;seen_source=source;return output;
}
__declspec(naked) void Original(){__asm{
    push ebp
    mov ebp,esp
    push dword ptr [ebp+0xc]
    push dword ptr [ebp+8]
    push ecx
    call Construct
    add esp,12
    mov esp,ebp
    pop ebp
    ret 8
}}
}
extern "C" __declspec(dllexport) uint32_t __cdecl ProductionHistoryHostReadyGuardOracle(uint32_t edition){
    return (edition==2 || edition==3) && CrossplayHistoryHostReady::Methods(static_cast<Crossplay::Edition>(edition))?1u:0u;
}
int main(){
    using namespace CrossplayHistoryHostReady;
    unsigned char event[0x18]{};uintptr_t before=0,after=0;
    __asm mov before,esp
    void* result=ConstructWith(reinterpret_cast<uintptr_t>(&Original),event,19);
    __asm mov after,esp
    Check(result==event && before==after);Check(seen_output==reinterpret_cast<uintptr_t>(event));Check(seen_subtype==29);Check(seen_source==19);
    const View original{1,2,3,4,5,1};Check(Same(original,original));Check(!Same(View{},View{}));
    for(unsigned which=0;which<6;++which){auto changed=original;
        if(which==0)++changed.session;if(which==1)++changed.cache;if(which==2)++changed.manager;
        if(which==3)++changed.configuration;if(which==4)++changed.rows;if(which==5)changed.mode^=1;
        Check(!Same(original,changed));
    }
    CrossplayNative::ParticipantEvent binding;CrossplayNative::LobbyAdmission admission;Pending pending;std::vector<unsigned char> intent;
    SetLastError(1963);Check(!Enqueue(binding,admission,{0,1},intent,pending));Check(!pending.owner && GetLastError()==1963);
    CrossplayReadyState::Snapshot state{1,1,1};Check(Observe(binding,admission,pending,1,state)==CrossplayNativeCodec::ReadyObservation::Invalid);
    Check(!state.revision && GetLastError()==1963);Check(!Methods());
    std::array<unsigned char,4> actual{1,1,1,1};
    Check(Observe(binding,admission,pending,1,state,&actual)==CrossplayNativeCodec::ReadyObservation::Invalid);
    Check(actual==std::array<unsigned char,4>{0xff,0xff,0xff,0xff} && !state.revision && GetLastError()==1963);
    printf("PASS %u History host Ready constructor ABI/owner/interval/exclusion checks; authored only, no native toggle or world qualified.\n",checks);return 0;
}
