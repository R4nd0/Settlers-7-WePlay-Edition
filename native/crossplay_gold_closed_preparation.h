#pragma once
#include "crossplay_closed_slot_codec.h"
#include "crossplay_gold_guest_host_identity.h"
#include "crossplay_closed_config_receipt.h"

// Native Gold preparation only after the authenticated ClosedSlot event was
// consumed by the original cache. Original scalar leaf setters use EAX/ECX;
// original CString assignment owns name storage. No Ready/world result supplied.
namespace CrossplayGoldClosedPreparation {
inline bool Methods(){
    using CrossplayNativeCodec::Detail::Readable;
    const unsigned char slot[]={0x89,0x48,0x5c,0xc3},kind[]={0x89,0x48,0x64,0xc3};
    return CrossplayNative::GoldGuestIdentityDetail::Methods()
        && Readable(reinterpret_cast<void*>(0x5a8e00),sizeof(slot)) && !memcmp(reinterpret_cast<void*>(0x5a8e00),slot,sizeof(slot))
        && Readable(reinterpret_cast<void*>(0x5b2000),sizeof(kind)) && !memcmp(reinterpret_cast<void*>(0x5b2000),kind,sizeof(kind));
}
inline void SetWith(uintptr_t method,void* row,uint32_t value){__asm{
    mov eax,row
    mov ecx,value
    call method
}}
inline bool Preserved(const unsigned char* before,const unsigned char* after,size_t size,uint32_t slot){
    if(!before || !after || size!=25*0x5c4 || slot<3 || slot>4)return false;
    const size_t at=slot*0x5c4;
    for(size_t i=0;i<size;++i){
        if((i>=at+0x1c && i<at+0x2c) || (i>=at+0x5c && i<at+0x60) || (i>=at+0x64 && i<at+0x68))continue;
        if(before[i]!=after[i])return false;
    }
    return true;
}
inline bool Applied(const unsigned char* row,uint32_t slot){
    const auto& name=*reinterpret_cast<const CrossplayNative::GoldGuestIdentityDetail::Text*>(row+0x1c);
    return CrossplayChat::Word(row+0x5c)==slot && CrossplayChat::Word(row+0x60)==1
        && CrossplayChat::Word(row+0x64)==4 && !name.length && (!name.data || !name.data[0]);
}
inline bool Project(const CrossplayNative::ParticipantEvent& binding,CrossplaySessionConfig::Connections ids,uint32_t slot,
                    const CrossplayConfigurationReceipt::Interval* interval=nullptr,const CrossplayConfigurationReceipt::Receipt* receipt=nullptr){
    struct ErrorScope {DWORD value=GetLastError();~ErrorScope(){SetLastError(value);}} error;
    CrossplayNativeCodec::LoadProgressDetail::FpScope fp;
    using namespace CrossplayNative;using namespace CrossplayNativeCodec;
    uintptr_t session=0,cache=0;LobbySnapshot lobby;GoldGuestRowDetail::Interval before,after;
    if(slot<3 || slot>4 || !Methods() || !CrossplayClosedSlot::Methods(Crossplay::Edition::Gold)
        || (receipt?(!interval || !CrossplayClosedConfigReceipt::Context(Crossplay::Edition::Gold,binding,ids,slot,*interval,*receipt,session,cache)):
            !CrossplayClosedSlot::Context(Crossplay::Edition::Gold,binding,ids,slot,true,session,cache))
        || !binding.CaptureLobby(lobby) || !GoldGuestRowDetail::Guest(lobby,ids.guest,ids.host)
        || !GoldGuestRowDetail::Capture(lobby,before) || before.session!=session)return false;
    auto* rows=reinterpret_cast<unsigned char*>(before.rows);auto* row=rows+slot*0x5c4;
    for(const auto& member:lobby.members){
        const auto& p=member.player;const auto* human=rows+p.slot*0x5c4;std::string name;
        if(CrossplayChat::Word(human+0x18)!=(p.principal?p.principal:p.slot) || CrossplayChat::Word(human+0x5c)!=p.slot
            || CrossplayChat::Word(human+0x60)!=3 || CrossplayChat::Word(human+0x64)!=1
            || !SessionConfigDetail::NativeString(human+0x1c,name) || name!=p.name)return false;
    }
    const auto prior_slot=CrossplayChat::Word(row+0x5c),kind=CrossplayChat::Word(row+0x64),id=CrossplayChat::Word(row+0x18);
    if((prior_slot!=UINT32_MAX && prior_slot!=slot) || CrossplayChat::Word(row+0x60)!=1 || (kind!=1 && kind!=4)
        || !id || id==UINT32_MAX || !HistoryRoleDetail::Writable(row+0x1c,16) || !HistoryRoleDetail::Writable(row+0x5c,4)
        || !HistoryRoleDetail::Writable(row+0x64,4))return false;
    for(const auto& member:lobby.members)if(id==(member.player.principal?member.player.principal:member.player.slot))return false;
    const auto old=*reinterpret_cast<const GoldGuestIdentityDetail::Text*>(row+0x1c);
    if(!GoldGuestIdentityDetail::Storage(old))return false;
    if(old.data)for(unsigned i=0;i<25;++i)if(i!=slot && *reinterpret_cast<const char* const*>(rows+i*0x5c4+0x1c)==old.data)return false;
    const std::vector<unsigned char> original(rows,rows+25*0x5c4);
    const GoldGuestIdentityDetail::Text empty{};
    if(GoldGuestIdentityDetail::CopyWith(0x415ef0,row+0x1c,&empty)!=row+0x1c)return false;
    SetWith(0x5a8e00,row,slot);SetWith(0x5b2000,row,4);
    uintptr_t final_session=0,final_cache=0;LobbySnapshot current;
    return Methods() && Applied(row,slot) && Preserved(original.data(),rows,original.size(),slot)
        && binding.CaptureLobby(current) && GoldGuestRowDetail::Guest(current,ids.guest,ids.host)
        && GoldGuestRowDetail::Capture(current,after) && GoldGuestRowDetail::Same(before,after)
        && (receipt?CrossplayClosedConfigReceipt::Context(Crossplay::Edition::Gold,binding,ids,slot,*interval,*receipt,final_session,final_cache):
            CrossplayClosedSlot::Context(Crossplay::Edition::Gold,binding,ids,slot,true,final_session,final_cache))
        && final_session==session && final_cache==cache;
}
}
