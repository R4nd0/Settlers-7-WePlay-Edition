#pragma once
#include "crossplay_history_host_guest_row.h"
#include "crossplay_closed_preparation.h"

// Bind only identity metadata from the actually admitted remote human. Reuse
// History's original setters; Ready, teams, castle and every other row byte stay
// unchanged. This grants no configuration, START or world authority.
namespace CrossplayNative {
namespace HistoryHostGuestIdentityDetail {
struct Text {const char* data=nullptr;uint32_t capacity=0,length=0,extra=0;};
static_assert(sizeof(Text)==16,"Original History CString layout");
inline bool Methods(){
    CrossplayClosedPreparation::Pin text;HistoryClaimDetail::Pin claim;
    constexpr auto edition=Crossplay::Edition::SteamHistory;
    return HistoryHostGuestRowDetail::Methods() && CrossplayClosedPreparation::Layout(edition,text)
        && CrossplayClosedPreparation::Methods(edition,text) && HistoryClaimDetail::Layout(edition,claim)
        && claim.set_name==text.name;
}
inline bool Storage(const Text& value){
    if(value.length>64 || value.capacity>65536)return false;
    if(!value.data)return !value.length && !value.capacity;
    return value.capacity>value.length && HistoryRoleDetail::Writable(value.data,value.capacity)
        && !value.data[value.length] && !memchr(value.data,0,value.length);
}
inline bool Preserved(const unsigned char* before,const unsigned char* after,size_t size){
    if(!before || !after || size!=0x8dc0)return false;
    for(size_t i=0;i<size;++i){
        if(i>=2*0x5a8+0x18 && i<2*0x5a8+0x2c)continue;
        if(before[i]!=after[i])return false;
    }
    return true;
}
inline uint32_t Identity(const Participant& remote){return remote.principal?remote.principal:2u;}
inline bool Remote(const LobbySnapshot& lobby,uint32_t connection,Participant& result){
    result={};if(!lobby.instance || !lobby.host || lobby.local_slot!=1 || lobby.members.size()!=2)return false;
    const Participant* host=nullptr;const Participant* guest=nullptr;
    for(const auto& member:lobby.members){
        const auto& p=member.player;
        if(member.status!=2 || p.connection==UINT32_MAX || p.principal==UINT32_MAX || p.name.empty() || p.name.size()>64)return false;
        for(unsigned char c:p.name)if(c<32 || c>126)return false;
        if(p.slot==1 && p.local && p.host)host=&p;
        else if(p.slot==2 && !p.local && !p.host && p.connection==connection)guest=&p;
        else return false;
    }
    if(!host || !guest || host->connection==guest->connection || (host->principal?host->principal:1)==Identity(*guest))return false;
    result=*guest;return true;
}
inline bool Same(const Participant& a,const Participant& b){
    return a.connection==b.connection && a.principal==b.principal && a.station==b.station && a.slot==b.slot
        && a.name==b.name && a.host==b.host && a.local==b.local;
}
inline bool Matches(const unsigned char* row,const Participant& remote){
    std::string name;
    return CrossplayChat::Word(row+0x18)==Identity(remote) && CrossplayChat::Word(row+0x5c)==2
        && CrossplayChat::Word(row+0x60)==3 && CrossplayChat::Word(row+0x64)==1
        && CrossplayNativeCodec::SessionConfigDetail::NativeString(row+0x1c,name) && name==remote.name;
}
inline void SetWith(uintptr_t id_method,uintptr_t name_method,void* row,uint32_t identity,const Text* text){
    reinterpret_cast<void (__thiscall*)(void*,uint32_t)>(id_method)(row,identity);
    reinterpret_cast<void (__thiscall*)(void*,const Text*)>(name_method)(row,text);
}
}
class HistoryHostGuestIdentity {
    DWORD owner=0;HistoryClaimDetail::Interval interval;Participant expected;
public:
    bool Begin(const ParticipantEvent& binding,LobbyAdmission& admission,const HistoryHostGuestRowClaim& claim){
        struct ErrorScope {DWORD value=GetLastError();~ErrorScope(){SetLastError(value);}} error;
        CrossplayNativeCodec::LoadProgressDetail::FpScope fp;using namespace HistoryHostGuestIdentityDetail;
        HistoryClaimDetail::Interval before,after;LobbySnapshot lobby;Participant remote;
        if(owner || !Methods() || !claim.Applied(binding,admission) || !binding.CaptureLobby(lobby)
            || !Remote(lobby,admission.PeerLocalConnection(),remote) || !HistoryHostGuestRowDetail::Capture(binding,admission,before))return false;
        auto* rows=reinterpret_cast<unsigned char*>(before.rows);auto* row=rows+2*0x5a8;
        if(!HistoryRoleDetail::Writable(row+0x18,20))return false;
        const auto old=*reinterpret_cast<const Text*>(row+0x1c);if(!Storage(old))return false;
        if(old.data)for(unsigned i=0;i<25;++i)
            if(i!=2 && *reinterpret_cast<const char* const*>(rows+i*0x5a8+0x1c)==old.data)return false;
        const std::vector<unsigned char> original(rows,rows+0x8dc0);
        const Text source{remote.name.c_str(),static_cast<uint32_t>(remote.name.size()+1),static_cast<uint32_t>(remote.name.size()),0};
        HistoryClaimDetail::Pin pin;if(!HistoryClaimDetail::Layout(Crossplay::Edition::SteamHistory,pin))return false;
        SetWith(pin.set_id,pin.set_name,row,Identity(remote),&source);
        const auto copied=*reinterpret_cast<const Text*>(row+0x1c);
        if(!Storage(copied) || copied.data==source.data || copied.length!=source.length
            || memcmp(copied.data,source.data,source.length+1))return false;
        LobbySnapshot current;Participant actual;
        if(!binding.CaptureLobby(current) || !Remote(current,admission.PeerLocalConnection(),actual) || !Same(remote,actual)
            || !HistoryHostGuestRowDetail::Capture(binding,admission,after) || !HistoryClaimDetail::Same(before,after)
            || !Methods() || !claim.Applied(binding,admission) || !Matches(row,actual)
            || !Preserved(original.data(),rows,original.size()))return false;
        owner=GetCurrentThreadId();interval=before;expected=std::move(remote);return true;
    }
    bool Applied(const ParticipantEvent& binding,LobbyAdmission& admission,const HistoryHostGuestRowClaim& claim)const{
        using namespace HistoryHostGuestIdentityDetail;HistoryClaimDetail::Interval current;LobbySnapshot lobby;Participant remote;
        return owner && owner==GetCurrentThreadId() && Methods() && claim.Applied(binding,admission)
            && binding.CaptureLobby(lobby) && Remote(lobby,admission.PeerLocalConnection(),remote) && Same(expected,remote)
            && HistoryHostGuestRowDetail::Capture(binding,admission,current) && HistoryClaimDetail::Same(interval,current)
            && Matches(reinterpret_cast<const unsigned char*>(current.rows)+2*0x5a8,remote);
    }
};
}
