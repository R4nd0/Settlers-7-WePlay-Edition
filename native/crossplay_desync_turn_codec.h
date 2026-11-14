#pragma once
#include "crossplay_load_progress_codec.h"
#include "crossplay_desync_turn.h"

namespace CrossplayNativeCodec::DesyncTurnDetail {
inline Detail::Profile Pin(Crossplay::Edition edition) {
    auto p=Detail::Pin(edition);p.event_vt=edition==Crossplay::Edition::Gold?0xf557e4:
        edition==Crossplay::Edition::UbisoftHistory?0x113f548:0x113da68;return p;
}
inline bool Methods(Crossplay::Edition edition) {
    const unsigned index=static_cast<unsigned>(edition)-1;if(index>=3)return false;
    const auto p=Pin(edition);
    if(!Detail::Methods(p) || !Detail::Tables(p,edition) || !Detail::Readable(reinterpret_cast<void*>(p.event_vt),24))return false;
    uint64_t hash=14695981039346656037ull;uintptr_t methods[6]{};
    for(unsigned i=0;i<6;++i) {
        const auto* bytes=reinterpret_cast<const unsigned char*>(p.event_vt+4*i);memcpy(&methods[i],bytes,4);
        if(methods[i]<0x400000 || methods[i]>=0x1500000 || !Detail::Readable(reinterpret_cast<void*>(methods[i]),8))return false;
        for(unsigned j=0;j<4;++j)hash=(hash^bytes[j])*1099511628211ull;
        const auto* code=reinterpret_cast<const unsigned char*>(methods[i]);
        for(unsigned j=0;j<8;++j)hash=(hash^code[j])*1099511628211ull;
    }
    const unsigned slots[]={1,3},lengths[]={p.gold?6u:16u,p.gold?148u:52u};
    for(unsigned i=0;i<2;++i) {
        const auto* bytes=reinterpret_cast<const unsigned char*>(methods[slots[i]]);
        if(!Detail::Readable(bytes,lengths[i]))return false;
        for(unsigned j=0;j<lengths[i];++j)hash=(hash^bytes[j])*1099511628211ull;
    }
    const uint64_t expected[]={0x3870c8d97e33b094ull,0x614853cf31ef1822ull,0x2721ff1dedadcf63ull};
    return hash==expected[index];
}
inline bool Source(const Detail::Profile& p,const void* event,uint32_t source) {
    const auto* data=static_cast<const unsigned char*>(event);
    return source!=UINT32_MAX && Detail::Readable(data,0x14) && *reinterpret_cast<const uintptr_t*>(data)==p.event_vt
        && CrossplayChat::Word(data+4)==CrossplayDesyncTurn::Type && CrossplayChat::Word(data+8)==source
        && data[12]<=1;
}
inline bool Encode(Crossplay::Edition edition,const CrossplayNative::ParticipantEvent& binding,DWORD owner,
                   uintptr_t session,uint32_t own,uint32_t other,const void* event,std::vector<unsigned char>& wire) {
    LoadProgressDetail::FpScope preserve;wire.clear();
    if(!LoadProgressDetail::World(binding,owner,session,own,other) || !Methods(edition))return false;
    const auto p=Pin(edition);
    if(Source(p,event,own) && Detail::Encode(p,event,wire) && CrossplayDesyncTurn::Inspect(wire,own))return true;
    wire.clear();return false;
}
inline bool Enqueue(Crossplay::Edition edition,const CrossplayNative::ParticipantEvent& binding,DWORD owner,
                    uintptr_t session,uint32_t own,uint32_t other,const std::vector<unsigned char>& wire) {
    LoadProgressDetail::FpScope preserve;
    if(!LoadProgressDetail::World(binding,owner,session,own,other) || !CrossplayDesyncTurn::Inspect(wire,other) || !Methods(edition))return false;
    const auto p=Pin(edition);Detail::OwnedStream stream(p);if(!stream.initialized)return false;
    stream.value.bytes=const_cast<unsigned char*>(wire.data());
    stream.value.capacity=stream.value.length=static_cast<uint32_t>(wire.size());stream.value.external=1;
    Detail::Visitor reader{p.reader_vt,1,&stream.value,0};const char* name="WePlayEvent";
    void* decoded=p.gold?Detail::GoldRead(p.read,&reader,name):
        reinterpret_cast<void* (__thiscall*)(void*,const char*,uint32_t)>(p.read)(&reader,name,0);
    if(!decoded || !Detail::Readable(decoded,0x14) || *static_cast<const uintptr_t*>(decoded)!=p.event_vt)return false;
    std::vector<unsigned char> repeated;
    bool okay=stream.value.position==wire.size() && Source(p,decoded,other) && Detail::Encode(p,decoded,repeated)
        && repeated==wire && LoadProgressDetail::World(binding,owner,session,own,other);
    uintptr_t send=0;void* transport=okay?Detail::Transport(edition,send):nullptr;
    if(transport)reinterpret_cast<void (__thiscall*)(void*,const void*,uint32_t,uint32_t,uint32_t,uint32_t)>(send)(transport,decoded,0,0,0,0);
    else okay=false;
    reinterpret_cast<void (__thiscall*)(void*,uint32_t)>(*reinterpret_cast<const uintptr_t*>(p.event_vt))(decoded,1);
    return okay;
}
}
