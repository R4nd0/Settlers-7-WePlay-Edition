#pragma once
#include "crossplay_history_claim.h"
#include "crossplay_ai_preparation_source.h"
#include "crossplay_load_progress_codec.h"

// Reuse History's original prepare/claim ABI for an already admitted remote
// human. Only row2's controller/network slot change. No identity, castle, Ready,
// start, backend or save result is supplied by this partial preparation step.
namespace CrossplayNative {
namespace HistoryHostGuestRowDetail {
inline bool Methods(){
    if(!HistoryClaimDetail::Methods(Crossplay::Edition::SteamHistory))return false;
    const struct Region {uintptr_t at;size_t bytes;uint64_t hash;} regions[]={
        {0x586460,27,0xc8f8701aa81f92a9ull},{0x5864d0,59,0x6c189df081f35eedull}};
    for(const auto& region:regions){
        const auto* bytes=reinterpret_cast<const unsigned char*>(region.at);
        if(!CrossplayNativeCodec::Detail::Readable(bytes,region.bytes))return false;
        uint64_t hash=14695981039346656037ull;
        for(size_t i=0;i<region.bytes;++i)hash=(hash^bytes[i])*1099511628211ull;
        if(hash!=region.hash)return false;
    }
    return true;
}
inline void PrepareWith(uintptr_t method,void* configuration,uint32_t slot){
    reinterpret_cast<void (__thiscall*)(void*,uint32_t)>(method)(configuration,slot);
}
inline bool ClaimWith(uintptr_t method,void* configuration,uint32_t slot,uint32_t network_slot){
    return reinterpret_cast<bool (__thiscall*)(void*,uint32_t,uint32_t)>(method)(configuration,slot,network_slot);
}
inline bool Preserved(const unsigned char* before,const unsigned char* after,size_t size){
    if(!before || !after || size!=0x8dc0)return false;
    constexpr size_t changed=2*0x5a8+0x5c;
    for(size_t i=0;i<size;++i)if((i<changed || i>=changed+8) && before[i]!=after[i])return false;
    return CrossplayChat::Word(after+changed)==2 && CrossplayChat::Word(after+changed+4)==3;
}
inline bool Capture(const ParticipantEvent& binding,LobbyAdmission& admission,HistoryClaimDetail::Interval& output){
    output={};constexpr auto edition=Crossplay::Edition::SteamHistory;
    if(admission.GetState()!=AdmissionState::RosterBound || !admission.Poll() || !Methods()
        || !CrossplayNativeCodec::SessionConfigDetail::Roster(binding,{admission.OwnConnection(),admission.PeerLocalConnection()},true))return false;
    LobbySnapshot lobby;HistoryClaimDetail::Pin pin;CrossplayAIPreparationSource::Snapshot source;unsigned stage=0;
    if(!binding.CaptureLobby(lobby) || !HistoryClaimDetail::Layout(edition,pin)
        || !CrossplayAIPreparationSource::Capture(edition,source,stage) || !source.same
        || !HistoryClaimDetail::Capture(pin,lobby,output))return false;
    const auto* session=reinterpret_cast<const unsigned char*>(lobby.instance);
    return CrossplayNativeCodec::Detail::Readable(session,0x2c) && CrossplayChat::Word(session+0x28)==2;
}
}
class HistoryHostGuestRowClaim {
    DWORD owner=0;HistoryClaimDetail::Interval interval;
public:
    bool Begin(const ParticipantEvent& binding,LobbyAdmission& admission){
        struct ErrorScope {DWORD value=GetLastError();~ErrorScope(){SetLastError(value);}} error;
        CrossplayNativeCodec::LoadProgressDetail::FpScope fp;using namespace HistoryHostGuestRowDetail;
        HistoryClaimDetail::Interval before,after;
        if(owner || !Capture(binding,admission,before))return false;
        const auto* rows=reinterpret_cast<const unsigned char*>(before.rows);const auto* row=rows+2*0x5a8;
        const auto* host=rows+0x5a8;
        if(CrossplayChat::Word(host+0x5c)!=1 || CrossplayChat::Word(host+0x60)!=3 || CrossplayChat::Word(host+0x64)!=1
            || CrossplayChat::Word(row+0x5c)!=UINT32_MAX || CrossplayChat::Word(row+0x60)!=1 || CrossplayChat::Word(row+0x64)!=1
            || !HistoryRoleDetail::Writable(row+0x5c,8))return false;
        const std::vector<unsigned char> original(rows,rows+0x8dc0);
        HistoryClaimDetail::Pin pin;if(!HistoryClaimDetail::Layout(Crossplay::Edition::SteamHistory,pin))return false;
        auto* configuration=reinterpret_cast<void*>(before.configuration);
        PrepareWith(pin.prepare,configuration,2);
        if(CrossplayChat::Word(row+0x60)!=2 || !ClaimWith(pin.claim,configuration,2,2)
            || !Capture(binding,admission,after) || !HistoryClaimDetail::Same(before,after)
            || !Preserved(original.data(),rows,original.size()))return false;
        owner=GetCurrentThreadId();interval=before;return true;
    }
    bool Applied(const ParticipantEvent& binding,LobbyAdmission& admission)const{
        using namespace HistoryHostGuestRowDetail;HistoryClaimDetail::Interval current;
        if(!owner || owner!=GetCurrentThreadId() || !Capture(binding,admission,current)
            || !HistoryClaimDetail::Same(interval,current))return false;
        const auto* row=reinterpret_cast<const unsigned char*>(current.rows)+2*0x5a8;
        return CrossplayChat::Word(row+0x5c)==2 && CrossplayChat::Word(row+0x60)==3 && CrossplayChat::Word(row+0x64)==1;
    }
};
}
