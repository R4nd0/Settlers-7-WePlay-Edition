#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "crossplay_gold_guest_identity.h"
#include <cstdio>
#include <cstdlib>
namespace {
unsigned checks=0;void* seen_destination=nullptr;const void* seen_source=nullptr;char copied[128]{};
void Check(bool value){++checks;if(!value){printf("FAIL Gold guest identity check %u\n",checks);std::exit(1);}}
void* __cdecl Copy(void* destination,const CrossplayNative::GoldGuestIdentityDetail::Text* source){
    seen_destination=destination;seen_source=source;Check(source->length<sizeof(copied));
    memcpy(copied,source->data,source->length+1);
    *static_cast<CrossplayNative::GoldGuestIdentityDetail::Text*>(destination)={copied,sizeof(copied),source->length,0};return destination;
}
__declspec(naked) void NativeCopy(){__asm {
    push eax
    push esi
    call Copy
    add esp,8
    ret
}}
}
extern "C" __declspec(dllexport) int __cdecl ProductionGoldGuestIdentityGuardOracle(){
    return CrossplayNative::GoldGuestIdentityDetail::Methods()?1:0;
}
int main(){
    using namespace CrossplayNative;using namespace GoldGuestIdentityDetail;
    const std::string name="GoldGuest";Text source{name.c_str(),static_cast<uint32_t>(name.size()+1),static_cast<uint32_t>(name.size()),0},destination;
    uintptr_t before=0,after=0;
    __asm mov before,esp
    auto* result=CopyWith(reinterpret_cast<uintptr_t>(&NativeCopy),&destination,&source);
    __asm mov after,esp
    Check(result==&destination && seen_destination==&destination && seen_source==&source && before==after);
    Check(destination.data!=source.data && destination.length==source.length && !memcmp(destination.data,source.data,source.length+1));
    Check(Storage(destination));Check(Storage(Text{}));
    auto bad=destination;bad.capacity=bad.length;Check(!Storage(bad));
    bad=destination;bad.data=nullptr;Check(!Storage(bad));bad=destination;bad.capacity=65537;Check(!Storage(bad));
    bad=destination;bad.length=65;Check(!Storage(bad));
    copied[1]=0;Check(!Storage(destination));copied[1]='o';copied[destination.length]='x';Check(!Storage(destination));copied[destination.length]=0;
    SYSTEM_INFO system{};GetSystemInfo(&system);const size_t page=system.dwPageSize;
    auto* guarded=static_cast<char*>(VirtualAlloc(nullptr,2*page,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));Check(guarded!=nullptr);
    DWORD previous=0;Check(VirtualProtect(guarded+page,page,PAGE_NOACCESS,&previous)!=0);
    char* edge=guarded+page-16;memcpy(edge,"GoldGuest",10);
    Check(!Storage(Text{edge,32,9,0})); // Current text is readable; claimed growth storage is not.
    Check(VirtualFree(guarded,0,MEM_RELEASE)!=0);
    std::vector<unsigned char> rows(25*0x5c4,0);auto original=rows;
    for(size_t at:{size_t(2*0x5c4+0x1c),size_t(2*0x5c4+0x2b)}){rows[at]^=1;Check(Preserved(original.data(),rows.data(),rows.size()));}
    for(size_t at:{size_t(2*0x5c4+0x18),size_t(0x18),size_t(0x5c4+0x18),size_t(2*0x5c4+0x2c),size_t(2*0x5c4+0x5c),size_t(2*0x5c4+0x60),size_t(2*0x5c4+0x64),size_t(2*0x5c4+0x71)}){
        rows[at]^=1;Check(!Preserved(original.data(),rows.data(),rows.size()));rows[at]^=1;
    }
    Check(!Preserved(nullptr,rows.data(),rows.size()));Check(!Preserved(original.data(),rows.data(),rows.size()-1));
    Check(!Methods());ParticipantEvent binding;LobbyAdmission admission;GoldGuestRowClaim claim;GoldGuestIdentity identity;
    SetLastError(1951);Check(!identity.Begin(binding,admission,claim) && GetLastError()==1951);Check(!identity.Applied(binding,admission,claim));
    printf("PASS %u Gold guest identity ABI/storage/scope/preservation checks; authored fixtures, no guest/world admitted.\n",checks);
    return 0;
}
