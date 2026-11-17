#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "crossplay_history_host_configuration.h"
#include <cstdio>
#include <cstdlib>
namespace {
unsigned checks=0;uintptr_t seen[6]{};
void Check(bool value){++checks;if(!value){printf("FAIL History host configuration check %u\n",checks);std::exit(1);}}
void* __cdecl Construct(void* output,uint32_t unused,uint32_t source,const void* map,const void* save,const void* slots){
    seen[0]=reinterpret_cast<uintptr_t>(output);seen[1]=unused;seen[2]=source;
    seen[3]=reinterpret_cast<uintptr_t>(map);seen[4]=reinterpret_cast<uintptr_t>(save);seen[5]=reinterpret_cast<uintptr_t>(slots);return output;
}
__declspec(naked) void Original(){__asm{
    push ebp
    mov ebp,esp
    push dword ptr [ebp+0x18]
    push dword ptr [ebp+0x14]
    push dword ptr [ebp+0x10]
    push dword ptr [ebp+0xc]
    push dword ptr [ebp+8]
    push ecx
    call Construct
    add esp,24
    mov esp,ebp
    pop ebp
    ret 20
}}
}
int main(){
    unsigned char output[0x3c]{},map[16]{},save[16]{},slots[12]{};uintptr_t before=0,after=0;
    __asm mov before,esp
    auto* result=CrossplayHistoryHostConfiguration::ConstructWith(reinterpret_cast<uintptr_t>(&Original),output,19,map,save,slots);
    __asm mov after,esp
    Check(result==output);Check(before==after);Check(seen[0]==reinterpret_cast<uintptr_t>(output));Check(seen[1]==0);Check(seen[2]==19);
    Check(seen[3]==reinterpret_cast<uintptr_t>(map));Check(seen[4]==reinterpret_cast<uintptr_t>(save));Check(seen[5]==reinterpret_cast<uintptr_t>(slots));
    CrossplayNative::ParticipantEvent binding;std::vector<unsigned char> wire(4,1);SetLastError(1943);
    Check(!CrossplayHistoryHostConfiguration::Capture(binding,{0,1},wire));Check(wire.empty());Check(GetLastError()==1943);
    printf("PASS %u History host configuration ABI/scope checks; authored constructor boundary only, no native host/config/world admitted.\n",checks);return 0;
}
