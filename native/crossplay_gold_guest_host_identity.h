#pragma once
#include "crossplay_gold_guest_identity.h"

// Reuse Gold's verified CString assignment for the actual admitted remote host
// in row1. The existing local guest row2 identity stays unchanged. No castle,
// Ready, team, start or world state is supplied by this metadata binding.
namespace CrossplayNative {
namespace GoldGuestHostIdentityDetail {
inline const Participant* Host(const LobbySnapshot& lobby){
    for(const auto& member:lobby.members)if(!member.player.local && member.player.host && member.player.slot==1)return &member.player;
    return nullptr;
}
inline uint32_t Identity(const Participant& host){return host.principal?host.principal:1u;}
inline bool Matches(const unsigned char* row,const Participant& host){
    std::string name;
    return CrossplayChat::Word(row+0x18)==Identity(host) && CrossplayChat::Word(row+0x5c)==1
        && CrossplayChat::Word(row+0x60)==3 && CrossplayChat::Word(row+0x64)==1
        && CrossplayNativeCodec::SessionConfigDetail::NativeString(row+0x1c,name) && name==host.name;
}
inline bool Preserved(const unsigned char* before,const unsigned char* after,size_t size){
    if(!before || !after || size!=25*0x5c4)return false;
    for(size_t i=0;i<size;++i){
        if(i>=0x5c4+0x18 && i<0x5c4+0x2c)continue;
        if(before[i]!=after[i])return false;
    }
    return true;
}
}
class GoldGuestHostIdentity {
    DWORD owner=0;GoldGuestRowDetail::Interval interval;Participant expected;
public:
    bool Begin(ParticipantEvent& binding,LobbyAdmission& admission,const GoldGuestRowClaim& claim,const GoldGuestIdentity& own_identity){
        struct ErrorScope {DWORD value=GetLastError();~ErrorScope(){SetLastError(value);}} error;
        CrossplayNativeCodec::LoadProgressDetail::FpScope fp;using namespace GoldGuestHostIdentityDetail;
        LobbySnapshot before,after;GoldGuestRowDetail::Interval first,last;
        if(owner || !GoldGuestIdentityDetail::Methods() || !admission.Poll() || !own_identity.Applied(binding,admission,claim)
            || !binding.CaptureLobby(before) || !GoldGuestRowDetail::Guest(before,admission.OwnConnection(),admission.PeerLocalConnection())
            || !GoldGuestRowDetail::Capture(before,first))return false;
        const auto* host=Host(before);const auto* own=GoldGuestIdentityDetail::Own(before);
        if(!host || !own || Identity(*host)==own->principal)return false;
        for(unsigned char c:host->name)if(c<32 || c>126)return false;
        auto* rows=reinterpret_cast<unsigned char*>(first.rows);auto* row=rows+0x5c4;
        if(CrossplayChat::Word(row+0x5c)!=1 || CrossplayChat::Word(row+0x60)!=3 || CrossplayChat::Word(row+0x64)!=1
            || !HistoryRoleDetail::Writable(row+0x18,20))return false;
        const auto old=*reinterpret_cast<const GoldGuestIdentityDetail::Text*>(row+0x1c);
        if(!GoldGuestIdentityDetail::Storage(old))return false;
        if(old.data)for(unsigned i=0;i<25;++i)
            if(i!=1 && *reinterpret_cast<const char* const*>(rows+i*0x5c4+0x1c)==old.data)return false;
        const std::vector<unsigned char> original(rows,rows+25*0x5c4);
        const GoldGuestIdentityDetail::Text source{host->name.c_str(),static_cast<uint32_t>(host->name.size()+1),static_cast<uint32_t>(host->name.size()),0};
        if(GoldGuestIdentityDetail::CopyWith(0x415ef0,row+0x1c,&source)!=row+0x1c)return false;
        const auto copied=*reinterpret_cast<const GoldGuestIdentityDetail::Text*>(row+0x1c);
        if(!GoldGuestIdentityDetail::Storage(copied) || copied.data==source.data || copied.length!=source.length
            || memcmp(copied.data,source.data,source.length+1))return false;
        *reinterpret_cast<uint32_t*>(row+0x18)=Identity(*host);
        if(!binding.CaptureLobby(after) || !GoldGuestRowDetail::Guest(after,admission.OwnConnection(),admission.PeerLocalConnection())
            || !GoldGuestRowDetail::Capture(after,last) || !GoldGuestRowDetail::Same(first,last)
            || !GoldGuestIdentityDetail::Methods() || !own_identity.Applied(binding,admission,claim)
            || !Preserved(original.data(),rows,original.size()))return false;
        const auto* actual=Host(after);
        if(!actual || actual->connection!=host->connection || actual->principal!=host->principal || actual->station!=host->station
            || actual->name!=host->name || !Matches(row,*actual))return false;
        owner=GetCurrentThreadId();interval=first;expected=*actual;return true;
    }
    bool Applied(const ParticipantEvent& binding,LobbyAdmission& admission,const GoldGuestRowClaim& claim,const GoldGuestIdentity& own_identity)const{
        using namespace GoldGuestHostIdentityDetail;LobbySnapshot lobby;GoldGuestRowDetail::Interval current;
        if(!owner || owner!=GetCurrentThreadId() || !admission.Poll() || !GoldGuestIdentityDetail::Methods()
            || !own_identity.Applied(binding,admission,claim) || !binding.CaptureLobby(lobby)
            || !GoldGuestRowDetail::Guest(lobby,admission.OwnConnection(),admission.PeerLocalConnection())
            || !GoldGuestRowDetail::Capture(lobby,current) || !GoldGuestRowDetail::Same(interval,current))return false;
        const auto* actual=Host(lobby);
        return actual && actual->connection==expected.connection && actual->principal==expected.principal && actual->station==expected.station
            && actual->name==expected.name && Matches(reinterpret_cast<const unsigned char*>(current.rows)+0x5c4,*actual);
    }
};
}
