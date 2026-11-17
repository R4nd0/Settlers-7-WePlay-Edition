#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "crossplay_gold_guest_row.h"
#include <cstdio>
#include <cstdlib>
namespace {
unsigned checks=0;uintptr_t seen_configuration=0;uint32_t seen_slot=0,seen_network=0;unsigned char claim_result=0;
void Check(bool value){++checks;if(!value){printf("FAIL Gold guest row check %u\n",checks);std::exit(1);}}
__declspec(naked) void Prepare(){
    __asm {
        mov seen_configuration,edx
        mov seen_slot,eax
        mov ecx,dword ptr [edx+4]
        imul eax,eax,0x5c4
        mov dword ptr [ecx+eax+0x60],2
        mov eax,0xdeadbeef
        mov ecx,0x12345678
        mov edx,0x87654321
        ret
    }
}
__declspec(naked) void Claim(){
    __asm {
        mov seen_configuration,ecx
        movzx edx,al
        mov seen_slot,edx
        mov eax,dword ptr [esp+4]
        mov seen_network,eax
        mov eax,0xaabbcc00
        mov al,claim_result
        ret 4
    }
}
}
extern "C" __declspec(dllexport) int __cdecl ProductionGoldGuestRowGuardOracle(){
    return CrossplayNative::GoldGuestRowDetail::Methods()?1:0;
}
int main(){
    using namespace CrossplayNative;using namespace GoldGuestRowDetail;
    std::vector<unsigned char> rows(25*0x5c4,0);uintptr_t configuration[]={reinterpret_cast<uintptr_t>(rows.data()),reinterpret_cast<uintptr_t>(rows.data())};
    SetLastError(1791);PrepareWith(reinterpret_cast<uintptr_t>(&Prepare),configuration,2);
    Check(seen_configuration==reinterpret_cast<uintptr_t>(configuration) && seen_slot==2);
    Check(CrossplayChat::Word(rows.data()+2*0x5c4+0x60)==2 && GetLastError()==1791);
    for(unsigned value:{0u,1u}){
        claim_result=static_cast<unsigned char>(value);uintptr_t before=0,after=0;
        __asm mov before,esp
        const bool result=ClaimWith(reinterpret_cast<uintptr_t>(&Claim),configuration,0xaabbcc02,17);
        __asm mov after,esp
        Check(result==(value!=0) && before==after && GetLastError()==1791);
        Check(seen_configuration==reinterpret_cast<uintptr_t>(configuration) && seen_slot==2 && seen_network==17);
    }
    auto original=rows;CrossplayChat::Put(rows.data()+2*0x5c4+0x5c,2);CrossplayChat::Put(rows.data()+2*0x5c4+0x60,3);
    Check(Preserved(original.data(),rows.data(),rows.size()));
    for(size_t at:{size_t(0),size_t(0x5c4+0x60),size_t(2*0x5c4+0x18),size_t(2*0x5c4+0x64),size_t(2*0x5c4+0x71),rows.size()-1}){
        rows[at]^=1;Check(!Preserved(original.data(),rows.data(),rows.size()));rows[at]^=1;
    }
    Check(!Preserved(nullptr,rows.data(),rows.size()) && !Preserved(original.data(),rows.data(),rows.size()-1));
    LobbySnapshot lobby{0x20000,2,false,{{{71,1,0,71,"SteamHost",true,false},2},{{91,2,101,91,"GoldGuest",false,true},2}}};
    Check(Guest(lobby,91,71));
    for(uint32_t own:{0u,71u,UINT32_MAX})Check(!Guest(lobby,own,71));
    for(uint32_t remote:{0u,91u,UINT32_MAX})Check(!Guest(lobby,91,remote));
    auto changed=lobby;changed.host=true;Check(!Guest(changed,91,71));
    changed=lobby;changed.local_slot=1;Check(!Guest(changed,91,71));
    changed=lobby;changed.instance=0;Check(!Guest(changed,91,71));
    changed=lobby;changed.members.push_back(changed.members[0]);Check(!Guest(changed,91,71));
    for(unsigned i=0;i<2;++i){
        changed=lobby;changed.members[i].status=1;Check(!Guest(changed,91,71));
        changed=lobby;changed.members[i].player.connection^=1;Check(!Guest(changed,91,71));
        changed=lobby;changed.members[i].player.local=!changed.members[i].player.local;Check(!Guest(changed,91,71));
        changed=lobby;changed.members[i].player.host=!changed.members[i].player.host;Check(!Guest(changed,91,71));
        changed=lobby;changed.members[i].player.station=0;Check(!Guest(changed,91,71));
    }
    changed=lobby;changed.members[1].player.principal=0;Check(!Guest(changed,91,71));
    changed=lobby;changed.members[1].player.principal=0x80000000;Check(!Guest(changed,91,71));
    Check(!Methods());ParticipantEvent binding;LobbyAdmission admission;GoldGuestRowClaim claim;
    Check(!claim.Begin(binding,admission));Check(!claim.Applied(binding,admission));
    printf("PASS %u Gold guest row ABI/scope/preservation checks; synthetic calls only, no reverse session admitted.\n",checks);
    return 0;
}
