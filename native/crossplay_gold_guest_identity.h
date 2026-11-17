#pragma once
#include "crossplay_gold_guest_row.h"

// Human identity metadata for an already claimed original Gold guest row.
// Values come from the actual admitted native participant, never an account
// cache or a chosen constant. No Ready/configuration/start authority is granted.
namespace CrossplayNative {
namespace GoldGuestIdentityDetail {
struct Text {const char* data=nullptr;uint32_t capacity=0,length=0,extra=0;};
static_assert(sizeof(Text)==16,"Original Gold CString layout");
inline bool Methods(){
    if(!GoldGuestRowDetail::Methods())return false;
    struct Region {uintptr_t at;size_t size;uint64_t hash;};
    const Region regions[]={{0x415ef0,18,0x35f9526d2fbaf115ull},{0x416590,108,0x9959aad0bd1c5b92ull},{0x415f50,137,0x758b6d02d79f523dull}};
    for(const auto& region:regions){
        const auto* bytes=reinterpret_cast<const unsigned char*>(region.at);
        if(!CrossplayNativeCodec::Detail::Readable(bytes,region.size))return false;
        uint64_t hash=14695981039346656037ull;
        for(size_t i=0;i<region.size;++i)hash=(hash^bytes[i])*1099511628211ull;
        if(hash!=region.hash)return false;
    }
    return true;
}
// Original CString copy assignment: ESI=destination, EAX=source, no stack
// arguments. Its original allocator owns any destination storage after copying.
inline void* CopyWith(uintptr_t address,void* destination,const Text* source){
    void* result=nullptr;
    __asm {
        mov esi,destination
        mov eax,source
        call address
        mov result,eax
    }
    return result;
}
inline bool Storage(const Text& text){
    if(text.length>64 || text.capacity>65536)return false;
    if(!text.data)return !text.length && !text.capacity;
    // Original assignment trusts capacity when reusing storage. Validate the
    // complete claimed allocation before allowing it to copy a longer name.
    return text.capacity>text.length && HistoryRoleDetail::Writable(text.data,text.capacity)
        && !text.data[text.length] && !memchr(text.data,0,text.length);
}
inline bool Preserved(const unsigned char* before,const unsigned char* after,size_t size){
    if(!before || !after || size!=25*0x5c4)return false;
    for(size_t i=0;i<size;++i){
        if(i>=2*0x5c4+0x18 && i<2*0x5c4+0x2c)continue;
        if(before[i]!=after[i])return false;
    }
    return true;
}
inline const Participant* Own(const LobbySnapshot& lobby){
    for(const auto& member:lobby.members)if(member.player.local)return &member.player;
    return nullptr;
}
inline bool RowMatches(const unsigned char* row,const Participant& own){
    std::string name;
    return CrossplayChat::Word(row+0x18)==own.principal
        && CrossplayChat::Word(row+0x5c)==2 && CrossplayChat::Word(row+0x60)==3 && CrossplayChat::Word(row+0x64)==1
        && CrossplayNativeCodec::SessionConfigDetail::NativeString(row+0x1c,name) && name==own.name;
}
}
class GoldGuestIdentity {
    DWORD owner=0;GoldGuestRowDetail::Interval interval;
public:
    bool Begin(ParticipantEvent& binding,LobbyAdmission& admission,const GoldGuestRowClaim& claim){
        struct ErrorScope {DWORD value=GetLastError();~ErrorScope(){SetLastError(value);}} error;
        CrossplayNativeCodec::LoadProgressDetail::FpScope fp;
        using namespace GoldGuestIdentityDetail;
        LobbySnapshot lobby;GoldGuestRowDetail::Interval before,after;
        if(owner || !Methods() || !claim.Applied(binding,admission) || !binding.CaptureLobby(lobby)
            || !GoldGuestRowDetail::Guest(lobby,admission.OwnConnection(),admission.PeerLocalConnection())
            || !GoldGuestRowDetail::Capture(lobby,before))return false;
        const auto* own=Own(lobby);if(!own)return false;
        auto* rows=reinterpret_cast<unsigned char*>(before.rows);auto* row=rows+2*0x5c4;
        if(!HistoryRoleDetail::Writable(row+0x18,20))return false;
        const Text old=*reinterpret_cast<const Text*>(row+0x1c);
        if(!Storage(old))return false;
        // The mutable CString must be this row's independent storage. Never
        // overwrite an aliased native row's name through a shared data pointer.
        if(old.data)for(unsigned i=0;i<25;++i)
            if(i!=2 && *reinterpret_cast<const char* const*>(rows+i*0x5c4+0x1c)==old.data)return false;
        const std::vector<unsigned char> original(rows,rows+25*0x5c4);
        const Text source{own->name.c_str(),static_cast<uint32_t>(own->name.size()+1),static_cast<uint32_t>(own->name.size()),0};
        if(CopyWith(0x415ef0,row+0x1c,&source)!=row+0x1c)return false;
        const auto copied=*reinterpret_cast<const Text*>(row+0x1c);
        if(!Storage(copied) || copied.data==source.data || copied.length!=source.length
            || memcmp(copied.data,source.data,source.length+1))return false;
        // This is the actual native local participant's opaque preparation ID,
        // separate from its routing CID. Project only this identity field; the
        // original row claim and CString assignment own the other transitions.
        *reinterpret_cast<uint32_t*>(row+0x18)=own->principal;
        LobbySnapshot current;
        if(!binding.CaptureLobby(current) || !GoldGuestRowDetail::Guest(current,admission.OwnConnection(),admission.PeerLocalConnection())
            || !GoldGuestRowDetail::Capture(current,after) || !GoldGuestRowDetail::Same(before,after)
            || !Methods() || !claim.Applied(binding,admission) || !Preserved(original.data(),rows,original.size()))return false;
        const auto* actual=Own(current);
        if(!actual || actual->principal!=own->principal || actual->name!=own->name || !RowMatches(row,*actual))return false;
        owner=GetCurrentThreadId();interval=before;return true;
    }
    bool Applied(const ParticipantEvent& binding,LobbyAdmission& admission,const GoldGuestRowClaim& claim)const{
        using namespace GoldGuestIdentityDetail;
        LobbySnapshot lobby;GoldGuestRowDetail::Interval current;
        if(!owner || owner!=GetCurrentThreadId() || !Methods() || !claim.Applied(binding,admission)
            || !binding.CaptureLobby(lobby) || !GoldGuestRowDetail::Guest(lobby,admission.OwnConnection(),admission.PeerLocalConnection())
            || !GoldGuestRowDetail::Capture(lobby,current) || !GoldGuestRowDetail::Same(interval,current))return false;
        const auto* own=Own(lobby);
        return own && RowMatches(reinterpret_cast<const unsigned char*>(current.rows)+2*0x5c4,*own);
    }
};
}
