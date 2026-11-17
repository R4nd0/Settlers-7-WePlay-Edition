#pragma once
#include "crossplay_ai_preparation_source.h"
#include "crossplay_admission.h"
#include "crossplay_load_progress_codec.h"

// Original Gold row-claim primitive for a separately admitted reverse guest.
// This does not bind the guest's castle/name, project a backend, consume host
// configuration, grant Ready/start or admit the reverse launcher role.
namespace CrossplayNative {
namespace GoldGuestRowDetail {
inline bool Methods(){
    struct Region {uintptr_t address;size_t bytes;uint64_t hash;};
    const Region regions[]={{0x9dfec0,20,0x3f20491b4f6d3567ull},{0x9dff10,62,0x5f680f5ec37cc84cull}};
    for(const auto& region:regions){
        const auto* code=reinterpret_cast<const unsigned char*>(region.address);
        if(!CrossplayNativeCodec::Detail::Readable(code,region.bytes))return false;
        uint64_t hash=14695981039346656037ull;
        for(size_t i=0;i<region.bytes;++i)hash=(hash^code[i])*1099511628211ull;
        if(hash!=region.hash)return false;
    }
    return true;
}
// Gold uses EDX/EAX for prepare, and ECX/AL plus one callee-cleaned stack
// argument for claim. Neither routine has the History __thiscall signature.
inline void PrepareWith(uintptr_t address,void* configuration,uint32_t slot){
    __asm {
        mov edx,configuration
        mov eax,slot
        call address
    }
}
inline bool ClaimWith(uintptr_t address,void* configuration,uint32_t slot,uint32_t network_slot){
    unsigned char result=0;
    __asm {
        mov ecx,configuration
        mov eax,slot
        push network_slot
        call address
        mov result,al
    }
    return result!=0;
}
inline bool Guest(const LobbySnapshot& snapshot,uint32_t own,uint32_t remote){
    if(!snapshot.instance || snapshot.host || snapshot.local_slot!=2 || snapshot.members.size()!=2
        || !own || !remote || own==UINT32_MAX || remote==UINT32_MAX || own==remote)return false;
    unsigned seen=0;
    for(const auto& member:snapshot.members){
        const auto& p=member.player;
        if(member.status!=2 || p.slot<1 || p.slot>2 || (seen&(1u<<p.slot))
            || p.connection!=(p.slot==1?remote:own) || p.host!=(p.slot==1) || p.local!=(p.slot==2)
            || p.name.empty() || p.name.size()>64 || p.name.find('\0')!=std::string::npos
            || p.principal==UINT32_MAX || !p.station || p.station==UINT32_MAX)return false;
        if(p.local && (!p.principal || p.principal>INT32_MAX))return false;
        seen|=1u<<p.slot;
    }
    return seen==6;
}
struct Interval {uintptr_t session=0,manager=0,configuration=0,rows=0;unsigned mode=0;};
inline bool Same(const Interval& a,const Interval& b){
    return a.session && a.session==b.session && a.manager && a.manager==b.manager
        && a.configuration && a.configuration==b.configuration && a.rows && a.rows==b.rows && a.mode==b.mode;
}
inline bool Capture(const LobbySnapshot& lobby,Interval& output){
    using namespace CrossplayNativeCodec;output={};
    CrossplayAIPreparationSource::Snapshot source{};unsigned stage=0;
    if(!lobby.instance || !CrossplayAIPreparationSource::Capture(Crossplay::Edition::Gold,source,stage) || !source.same)return false;
    const auto* manager=*reinterpret_cast<const unsigned char* const*>(0x10f834c);
    if(!Detail::Readable(manager,8+2*0xd4) || CrossplayChat::Word(manager)!=0xf51af0 || manager[4]>1)return false;
    const auto mode=manager[4];const auto* buffer=manager+((mode-1)&1)*0xd4;
    const auto* configuration=reinterpret_cast<const unsigned char*>(CrossplayChat::Word(buffer+0xa4));
    if(!Detail::Readable(configuration,8))return false;
    const auto rows=CrossplayChat::Word(configuration+4);
    if(CrossplayChat::Word(configuration)!=rows || !Detail::Readable(reinterpret_cast<const void*>(rows),0x9079)
        || *reinterpret_cast<const unsigned char*>(rows+0x9078)!=4)return false;
    output={lobby.instance,reinterpret_cast<uintptr_t>(manager),reinterpret_cast<uintptr_t>(configuration),rows,mode};
    return *reinterpret_cast<const unsigned char* const*>(0x10f834c)==manager && manager[4]==mode
        && CrossplayChat::Word(buffer+0xa4)==output.configuration;
}
inline bool Preserved(const unsigned char* before,const unsigned char* after,size_t size){
    if(!before || !after || size!=25*0x5c4)return false;
    for(size_t i=0;i<size;++i){
        if(i>=2*0x5c4+0x5c && i<2*0x5c4+0x64)continue;
        if(before[i]!=after[i])return false;
    }
    return CrossplayChat::Word(after+2*0x5c4+0x5c)==2 && CrossplayChat::Word(after+2*0x5c4+0x60)==3;
}
}
class GoldGuestRowClaim {
    DWORD owner=0;GoldGuestRowDetail::Interval interval;
public:
    bool Begin(ParticipantEvent& binding,LobbyAdmission& admission){
        struct ErrorScope {DWORD value=GetLastError();~ErrorScope(){SetLastError(value);}} error;
        CrossplayNativeCodec::LoadProgressDetail::FpScope fp;
        using namespace GoldGuestRowDetail;
        LobbySnapshot lobby;Interval before,after;
        if(owner || !Methods() || admission.GetState()!=AdmissionState::RosterBound || !admission.Poll()
            || !binding.CaptureLobby(lobby) || !Guest(lobby,admission.OwnConnection(),admission.PeerLocalConnection())
            || !Capture(lobby,before))return false;
        const auto* rows=reinterpret_cast<const unsigned char*>(before.rows);const auto* row=rows+2*0x5c4;
        if(CrossplayChat::Word(row+0x5c)!=UINT32_MAX || CrossplayChat::Word(row+0x60)!=1
            || CrossplayChat::Word(row+0x64)!=1
            || !HistoryRoleDetail::Writable(row+0x5c,8))return false;
        const std::vector<unsigned char> original(rows,rows+25*0x5c4);
        auto* configuration=reinterpret_cast<void*>(before.configuration);
        PrepareWith(0x9dfec0,configuration,2);
        if(CrossplayChat::Word(row+0x60)!=2 || !ClaimWith(0x9dff10,configuration,2,2))return false;
        LobbySnapshot current;
        if(!binding.CaptureLobby(current) || !Guest(current,admission.OwnConnection(),admission.PeerLocalConnection())
            || !Capture(current,after) || !Same(before,after) || !Methods()
            || !Preserved(original.data(),rows,original.size()))return false;
        owner=GetCurrentThreadId();interval=before;return true;
    }
    bool Applied(ParticipantEvent& binding,LobbyAdmission& admission)const{
        using namespace GoldGuestRowDetail;
        LobbySnapshot lobby;Interval current;
        if(!owner || owner!=GetCurrentThreadId() || !Methods()
            || admission.GetState()!=AdmissionState::RosterBound || !admission.Poll()
            || !binding.CaptureLobby(lobby) || !Guest(lobby,admission.OwnConnection(),admission.PeerLocalConnection())
            || !Capture(lobby,current) || !Same(interval,current))return false;
        const auto* row=reinterpret_cast<const unsigned char*>(current.rows)+2*0x5c4;
        return CrossplayChat::Word(row+0x5c)==2 && CrossplayChat::Word(row+0x60)==3 && CrossplayChat::Word(row+0x64)==1;
    }
};
}
