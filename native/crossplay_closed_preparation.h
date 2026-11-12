#pragma once
#include "crossplay_closed_slot_codec.h"
#include <array>

// Opt-in projection after the authenticated closed-slot event's original cache
// consumer has completed. Only unused Monhaim positions 3/4 in the owned fresh
// two-human lobby are eligible. No world/start acknowledgement is supplied.
namespace CrossplayClosedPreparation {
constexpr size_t RowSize=0x5a8;
struct Pin {uintptr_t name=0,slot=0,text=0,kind=0;uint64_t name_hash=0,text_hash=0;};
inline bool Layout(Crossplay::Edition edition,Pin& pin) {
    pin={};
    if(edition==Crossplay::Edition::UbisoftHistory)pin={0x546450,0x5862e0,0x4e29f0,0x546480,0x5f948da8c9eff08dull,0x8daf8fdbc424424full};
    else if(edition==Crossplay::Edition::SteamHistory)pin={0x545aa0,0x585da0,0x4e2880,0x545ad0,0x85cf9c0ffb604195ull,0x86d9b57cf2079bfeull};
    else return false;
    return true;
}
inline bool Methods(Crossplay::Edition edition,const Pin& pin) {
    Pin expected{};
    if(!Layout(edition,expected) || pin.name!=expected.name || pin.slot!=expected.slot || pin.text!=expected.text || pin.kind!=expected.kind
        || pin.name_hash!=expected.name_hash || pin.text_hash!=expected.text_hash)return false;
    struct Region {uintptr_t address;size_t bytes;uint64_t hash;};
    for(const auto& region:{Region{pin.name,25,pin.name_hash},Region{pin.slot,13,0x0bed4880da5fee7aull},Region{pin.text,0x70,pin.text_hash},
                           Region{pin.kind,13,0xe540581fdd5511b2ull}}) {
        const auto* bytes=reinterpret_cast<const unsigned char*>(region.address);
        if(!CrossplayNativeCodec::Detail::Readable(bytes,region.bytes))return false;
        uint64_t hash=14695981039346656037ull;
        for(size_t i=0;i<region.bytes;++i)hash=(hash^bytes[i])*1099511628211ull;
        if(hash!=region.hash)return false;
    }
    return true;
}
namespace Detail {
struct Text {char* bytes;uint32_t capacity,length,extra;};
inline bool Eligible(unsigned char* row,uint32_t target) {
    using CrossplayNativeCodec::Detail::Readable;
    using CrossplayNative::HistoryRoleDetail::Writable;
    if(target<3 || target>4 || !Readable(row,RowSize) || !Writable(row+0x1c,16) || !Writable(row+0x5c,4) || !Writable(row+0x64,4))return false;
    const uint32_t slot=CrossplayChat::Word(row+0x5c),profile=CrossplayChat::Word(row+0x18);
    const uint32_t kind=CrossplayChat::Word(row+0x64);
    if(!profile || profile==UINT32_MAX || (slot!=UINT32_MAX && slot!=target)
        || CrossplayChat::Word(row+0x60)!=1 || (kind!=1 && kind!=4))return false;
    const auto& name=*reinterpret_cast<const Text*>(row+0x1c);
    if(name.length>64 || (name.length && name.capacity<=name.length))return false;
    if(!name.bytes)return !name.length && !name.capacity;
    return Readable(name.bytes,name.length+1) && Writable(name.bytes,name.length+1) && !name.bytes[name.length];
}
inline bool Applied(const unsigned char* row,uint32_t target) {
    const auto& name=*reinterpret_cast<const Text*>(row+0x1c);
    return CrossplayChat::Word(row+0x5c)==target && CrossplayChat::Word(row+0x64)==4 && !name.length && (!name.bytes || !name.bytes[0]);
}
inline bool Preserved(const unsigned char* before,const unsigned char* after) {
    for(size_t i=0;i<RowSize;++i) {
        if((i>=0x1c && i<0x2c) || (i>=0x5c && i<0x60) || (i>=0x64 && i<0x68))continue;
        if(before[i]!=after[i])return false;
    }
    return true;
}
inline bool Apply(Crossplay::Edition edition,const Pin& pin,unsigned char* row,uint32_t target) {
    if(!Methods(edition,pin) || !Eligible(row,target))return false;
    if(Applied(row,target))return true;
    std::array<unsigned char,RowSize> before{};memcpy(before.data(),row,before.size());
    const Text empty{};
    // These are original thiscall leaf setters. The empty CString path neither
    // allocates nor frees; its complete original helper is fingerprinted too.
    reinterpret_cast<void (__thiscall*)(void*,const Text*)>(pin.name)(row,&empty);
    reinterpret_cast<void (__thiscall*)(void*,uint32_t)>(pin.slot)(row,target);
    reinterpret_cast<void (__thiscall*)(void*,uint32_t)>(pin.kind)(row,4);
    return Eligible(row,target) && Applied(row,target) && Preserved(before.data(),row) && Methods(edition,pin);
}
}
inline bool Project(Crossplay::Edition edition,const CrossplayNative::ParticipantEvent& binding,
                    CrossplaySessionConfig::Connections ids,uint32_t target) {
    using namespace CrossplayNativeCodec;
    Pin pin{};uintptr_t session=0,cache=0;
    if(!Layout(edition,pin) || !Methods(edition,pin) || !CrossplayClosedSlot::Methods(edition)
        || !CrossplayClosedSlot::Context(edition,binding,ids,target,true,session,cache))return false;
    CrossplayNative::LobbySnapshot before;unsigned char* rows=nullptr;
    if(!ReadyStateDetail::Preparation(edition,binding,ids,before,rows) || before.instance!=session)return false;
    auto* row=rows+target*RowSize;
    if(!Detail::Eligible(row,target))return false;
    const uint32_t profile=CrossplayChat::Word(row+0x18);
    for(const auto& member:before.members)if(profile==(member.player.principal?member.player.principal:member.player.slot))return false;
    if(!Detail::Apply(edition,pin,row,target))return false;
    CrossplayNative::LobbySnapshot after;unsigned char* next=nullptr;uintptr_t next_session=0,next_cache=0;
    return ReadyStateDetail::Preparation(edition,binding,ids,after,next) && after.instance==session && next==rows
        && CrossplayClosedSlot::Context(edition,binding,ids,target,true,next_session,next_cache)
        && next_session==session && next_cache==cache && Detail::Applied(row,target);
}
}
