#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "crossplay_history_host_guest_identity.h"
#include <cstdio>
#include <cstdlib>
namespace {
using namespace CrossplayNative;using namespace HistoryHostGuestIdentityDetail;
unsigned checks=0,name_calls=0;void* expected_row=nullptr;const Text* expected_source=nullptr;char copied[128]{};
void Check(bool value){++checks;if(!value){printf("FAIL History host guest identity check %u\n",checks);std::exit(1);}}
void __fastcall SetName(void* row,void*,const Text* source){
    Check(row==expected_row && source==expected_source);++name_calls;memcpy(copied,source->data,source->length+1);
    *reinterpret_cast<Text*>(static_cast<unsigned char*>(row)+0x1c)={copied,sizeof(copied),source->length,0};
}
}
extern "C" __declspec(dllexport) uint32_t __cdecl ProductionHistoryHostGuestIdentityGuardOracle(uint32_t edition){
    return edition==3 && HistoryHostGuestIdentityDetail::Methods()?1u:0u;
}
int main(){
    using namespace CrossplayNative;using namespace HistoryHostGuestIdentityDetail;
    std::vector<unsigned char> rows(0x8dc0,0);auto* row=rows.data()+2*0x5a8;expected_row=row;
    const std::string name="AdmittedGold";Text source{name.c_str(),static_cast<uint32_t>(name.size()+1),static_cast<uint32_t>(name.size()),0};expected_source=&source;
    CrossplayChat::Put(row+0x18,93);auto original=rows;uintptr_t before=0,after=1;
    __asm mov before,esp
    SetNameWith(reinterpret_cast<uintptr_t>(&SetName),row,&source);
    __asm mov after,esp
    Check(before==after && name_calls==1);Check(CrossplayChat::Word(row+0x18)==93);
    const auto value=*reinterpret_cast<const Text*>(row+0x1c);Check(Storage(value) && value.data!=source.data && value.length==source.length);
    Check(Preserved(original.data(),rows.data(),rows.size()));
    for(size_t offset:{size_t(2*0x5a8+0x18),size_t(0),size_t(0x5a8+0x18),size_t(2*0x5a8+0x2c),size_t(2*0x5a8+0x5c),size_t(2*0x5a8+0x60),size_t(2*0x5a8+0x64),size_t(2*0x5a8+0x68),size_t(2*0x5a8+0x71),size_t(2*0x5a8+0x55c),size_t(0x8db8)}){
        rows[offset]^=1;Check(!Preserved(original.data(),rows.data(),rows.size()));rows[offset]^=1;
    }
    Check(!Preserved(nullptr,rows.data(),rows.size()));Check(!Preserved(original.data(),rows.data(),rows.size()-1));
    Check(Storage(Text{}));auto bad=value;bad.capacity=bad.length;Check(!Storage(bad));bad=value;bad.data=nullptr;Check(!Storage(bad));
    bad=value;bad.capacity=65537;Check(!Storage(bad));bad=value;bad.length=65;Check(!Storage(bad));
    copied[1]=0;Check(!Storage(value));copied[1]='d';copied[value.length]='x';Check(!Storage(value));copied[value.length]=0;
    SYSTEM_INFO system{};GetSystemInfo(&system);size_t page=system.dwPageSize;
    auto* allocation=static_cast<char*>(VirtualAlloc(nullptr,2*page,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));Check(allocation!=nullptr);
    DWORD previous=0;Check(VirtualProtect(allocation+page,page,PAGE_NOACCESS,&previous)!=0);char* edge=allocation+page-16;memcpy(edge,"AdmittedGold",13);
    Check(!Storage(Text{edge,32,12,0}));Check(VirtualFree(allocation,0,MEM_RELEASE)!=0);
    LobbySnapshot lobby;Participant remote;Check(!Remote(lobby,7,remote));lobby.instance=0x1234;lobby.host=true;lobby.local_slot=1;
    Participant host;host.connection=1;host.principal=11;host.slot=1;host.local=true;host.host=true;host.name="Host";
    Participant guest;guest.connection=7;guest.principal=93;guest.station=9;guest.slot=2;guest.name=name;
    lobby.members={{host,2},{guest,2}};Check(Remote(lobby,7,remote) && Same(remote,guest));
    Check(!Remote(lobby,8,remote));lobby.members[1].player.local=true;Check(!Remote(lobby,7,remote));lobby.members[1].player.local=false;
    lobby.members[1].status=1;Check(!Remote(lobby,7,remote));lobby.members[1].status=2;
    lobby.members[1].player.name="";Check(!Remote(lobby,7,remote));lobby.members[1].player.name=name;
    lobby.members[1].player.name[1]='\n';Check(!Remote(lobby,7,remote));lobby.members[1].player=guest;
    Check(!Methods());ParticipantEvent binding;LobbyAdmission admission;HistoryHostGuestRowClaim claim;HistoryHostGuestIdentity identity;
    SetLastError(1977);Check(!identity.Begin(binding,admission,claim) && GetLastError()==1977);Check(!identity.Applied(binding,admission,claim));
    printf("PASS %u History host guest identity ABI/storage/scope/preservation checks; authored only, no actual identity or START qualified.\n",checks);return 0;
}
