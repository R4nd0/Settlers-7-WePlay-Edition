#pragma once
#include "crossplay_load_progress_codec.h"
#include "crossplay_turn_batch.h"

namespace CrossplayNativeCodec::TurnBatchDetail {
inline Detail::Profile Pin(Crossplay::Edition edition) {
    auto p=Detail::Pin(edition);p.event_vt=edition==Crossplay::Edition::Gold?0xf5581c:
        edition==Crossplay::Edition::UbisoftHistory?0x113f510:0x113da30;return p;
}
inline uintptr_t NestedTable(Crossplay::Edition edition) {
    return edition==Crossplay::Edition::Gold?0xf55800:edition==Crossplay::Edition::UbisoftHistory?0x113f564:0x113da14;
}
inline bool Methods(Crossplay::Edition edition) {
    const unsigned index=static_cast<unsigned>(edition)-1;if(index>=3)return false;
    const auto p=Pin(edition);if(!Detail::Methods(p) || !Detail::Tables(p,edition))return false;
    uint64_t hash=14695981039346656037ull;
    const uintptr_t tables[]={p.event_vt,NestedTable(edition)};
    for(unsigned table=0;table<2;++table) {
        if(!Detail::Readable(reinterpret_cast<void*>(tables[table]),24))return false;
        uintptr_t methods[6]{};
        for(unsigned i=0;i<6;++i) {
            const auto* bytes=reinterpret_cast<const unsigned char*>(tables[table]+4*i);memcpy(&methods[i],bytes,4);
            if(methods[i]<0x400000 || methods[i]>=0x1500000 || !Detail::Readable(reinterpret_cast<void*>(methods[i]),8))return false;
            for(unsigned j=0;j<4;++j)hash=(hash^bytes[j])*1099511628211ull;
            const auto* code=reinterpret_cast<const unsigned char*>(methods[i]);
            for(unsigned j=0;j<8;++j)hash=(hash^code[j])*1099511628211ull;
        }
        const unsigned slots[]={1,3},lengths[]={p.gold?6u:16u,p.gold?(table?233u:507u):(table?160u:83u)};
        for(unsigned i=0;i<2;++i) {
            const auto* bytes=reinterpret_cast<const unsigned char*>(methods[slots[i]]);
            if(!Detail::Readable(bytes,lengths[i]))return false;
            for(unsigned j=0;j<lengths[i];++j)hash=(hash^bytes[j])*1099511628211ull;
        }
    }
    const uint64_t expected[]={0x6679eb63ff2a5150ull,0x1aa5469021a9a95full,0x61d4a9817e9be15full};
    return hash==expected[index];
}
inline bool Source(Crossplay::Edition edition,const void* event,uint32_t source) {
    const auto p=Pin(edition);const auto* bytes=static_cast<const unsigned char*>(event);
    const unsigned vector=p.gold?0x18:0x14;
    if(source==UINT32_MAX || !Detail::Readable(bytes,vector+12) || *reinterpret_cast<const uintptr_t*>(bytes)!=p.event_vt
        || CrossplayChat::Word(bytes+4)!=CrossplayTurnBatch::Type || CrossplayChat::Word(bytes+8)!=source || bytes[12]>1)return false;
    const uintptr_t begin=CrossplayChat::Word(bytes+vector),end=CrossplayChat::Word(bytes+vector+4),capacity=CrossplayChat::Word(bytes+vector+8);
    if(end<begin || capacity<end || (end-begin)%0x24 || (capacity-begin)%0x24
        || capacity-begin>CrossplayTurnBatch::MaxItems*0x24 || (begin && !Detail::Readable(reinterpret_cast<void*>(begin),capacity-begin?capacity-begin:1)))return false;
    if(!begin && (end || capacity))return false;
    for(uintptr_t at=begin;at<end;at+=0x24) {
        const auto* item=reinterpret_cast<const unsigned char*>(at);
        if(*reinterpret_cast<const uintptr_t*>(item)!=NestedTable(edition) || CrossplayChat::Word(item+4)!=CrossplayTurnBatch::NestedType
            || CrossplayChat::Word(item+8)!=source || item[12]>1)return false;
    }
    return true;
}
inline bool Encode(Crossplay::Edition edition,const CrossplayNative::ParticipantEvent& binding,DWORD owner,
                   uintptr_t session,uint32_t own,uint32_t other,const void* event,std::vector<unsigned char>& wire) {
    LoadProgressDetail::FpScope preserve;wire.clear();
    if(!LoadProgressDetail::World(binding,owner,session,own,other) || !Methods(edition) || !Source(edition,event,own))return false;
    if(Detail::Encode(Pin(edition),event,wire) && CrossplayTurnBatch::Inspect(wire,own))return true;
    wire.clear();return false;
}
inline bool Enqueue(Crossplay::Edition edition,const CrossplayNative::ParticipantEvent& binding,DWORD owner,
                    uintptr_t session,uint32_t own,uint32_t other,const std::vector<unsigned char>& wire) {
    LoadProgressDetail::FpScope preserve;
    if(!LoadProgressDetail::World(binding,owner,session,own,other) || !CrossplayTurnBatch::Inspect(wire,other) || !Methods(edition))return false;
    const auto p=Pin(edition);Detail::OwnedStream stream(p);if(!stream.initialized)return false;
    stream.value.bytes=const_cast<unsigned char*>(wire.data());stream.value.capacity=stream.value.length=static_cast<uint32_t>(wire.size());stream.value.external=1;
    Detail::Visitor reader{p.reader_vt,1,&stream.value,0};const char* name="WePlayEvent";
    void* decoded=p.gold?Detail::GoldRead(p.read,&reader,name):
        reinterpret_cast<void* (__thiscall*)(void*,const char*,uint32_t)>(p.read)(&reader,name,0);
    if(!decoded || !Detail::Readable(decoded,p.gold?0x24:0x20) || *static_cast<const uintptr_t*>(decoded)!=p.event_vt)return false;
    std::vector<unsigned char> repeated;
    bool okay=stream.value.position==wire.size() && Source(edition,decoded,other) && Detail::Encode(p,decoded,repeated)
        && repeated==wire && LoadProgressDetail::World(binding,owner,session,own,other);
    uintptr_t send=0;void* transport=okay?Detail::Transport(edition,send):nullptr;
    if(transport)reinterpret_cast<void (__thiscall*)(void*,const void*,uint32_t,uint32_t,uint32_t,uint32_t)>(send)(transport,decoded,0,0,0,0);
    else okay=false;
    reinterpret_cast<void (__thiscall*)(void*,uint32_t)>(*reinterpret_cast<const uintptr_t*>(p.event_vt))(decoded,1);
    return okay;
}
}
