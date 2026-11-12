#pragma once
#include "crossplay_player_option_codec.h"
#include "crossplay_session_map_observer.h"
#include "crossplay_closed_slot_pins.h"
// Experimental four-slot fresh-lobby primitive; ordinary adapters exclude it.
// The caller owns ordering, interval/deadline and complete setup agreement.
namespace CrossplayClosedSlot {
constexpr uint32_t Class=0xd32e9949,Type=7u|(55u<<16);
inline constexpr unsigned char Magic[8]={'S','7','N','S','L','O','T',1};
inline bool Wire(const std::vector<unsigned char>& wire,uint32_t host,uint32_t& slot) {
    slot=0;
    if(host==UINT32_MAX || wire.size()!=17 || CrossplayChat::Word(wire.data())!=Class
        || CrossplayChat::Word(wire.data()+4)!=Type || CrossplayChat::Word(wire.data()+8)!=host || wire[12]!=1)return false;
    const auto value=CrossplayChat::Word(wire.data()+13);if(value<3 || value>4)return false;
    slot=value;return true;
}
inline uintptr_t Vtable(Crossplay::Edition edition) {
    return edition==Crossplay::Edition::Gold?0xf4df88:edition==Crossplay::Edition::UbisoftHistory?0x1140488:
        edition==Crossplay::Edition::SteamHistory?0x113e9cc:0;
}
inline bool Methods(Crossplay::Edition edition) {
    using namespace CrossplayNativeCodec;
    const unsigned index=static_cast<unsigned>(edition)-1;if(index>=3 || !Detail::Methods(Detail::Pin(edition)))return false;
    uint64_t hash=14695981039346656037ull;
    for(const auto& range:Pins[index]) {
        if(!range.length)continue;
        const auto* bytes=reinterpret_cast<const unsigned char*>(range.address);
        if(!Detail::Readable(bytes,range.length))return false;
        for(size_t i=0;i<range.length;++i)hash=(hash^bytes[i])*1099511628211ull;
    }
    return hash==Hashes[index];
}
inline bool Frame(const Crossplay::Peer& peer,uint32_t revision,const std::vector<unsigned char>& wire,uint32_t host,std::vector<unsigned char>& frame) {
    frame.clear();uint32_t slot=0;if(revision<1 || revision>2 || !Wire(wire,host,slot) || slot!=revision+2)return false;
    frame.assign(Magic,Magic+8);frame.insert(frame.end(),peer.local_id.begin(),peer.local_id.end());frame.insert(frame.end(),peer.remote_id.begin(),peer.remote_id.end());
    frame.resize(44);CrossplayChat::Put(frame.data()+40,revision);frame.insert(frame.end(),wire.begin(),wire.end());return true;
}
inline bool Receive(const Crossplay::Peer& peer,uint32_t previous,uint32_t source,uint32_t local,const std::vector<unsigned char>& frame,std::vector<unsigned char>& wire) {
    wire.clear();if(previous>=2 || source==UINT32_MAX || local==UINT32_MAX || frame.size()!=61 || memcmp(frame.data(),Magic,8)
        || memcmp(frame.data()+8,peer.remote_id.data(),16) || memcmp(frame.data()+24,peer.local_id.data(),16)
        || CrossplayChat::Word(frame.data()+40)!=previous+1)return false;
    std::vector<unsigned char> value(frame.begin()+44,frame.end());uint32_t slot=0;
    if(!Wire(value,source,slot) || slot!=previous+3)return false;
    CrossplayChat::Put(value.data()+8,local);if(!Wire(value,local,slot))return false;wire=std::move(value);return true;
}
inline bool Capture(const CrossplayNative::ParticipantEvent& binding,CrossplaySessionConfig::Connections ids,const void* event,std::vector<unsigned char>& wire) {
    using namespace CrossplayNativeCodec;wire.clear();
    if(!PlayerOptionDetail::Context(Crossplay::Edition::Gold,binding,ids,true) || !Methods(Crossplay::Edition::Gold)
        || !Detail::Readable(event,20))return false;
    const auto* p=static_cast<const unsigned char*>(event);const auto slot=CrossplayChat::Word(p+16);
    if(CrossplayChat::Word(p)!=Vtable(Crossplay::Edition::Gold) || CrossplayChat::Word(p+4)!=Type
        || CrossplayChat::Word(p+8)!=ids.host || p[12]!=1 || slot<3 || slot>4)return false;
    auto profile=Detail::Pin(Crossplay::Edition::Gold);profile.event_vt=Vtable(Crossplay::Edition::Gold);uint32_t parsed=0;
    if(Detail::Encode(profile,event,wire) && Wire(wire,ids.host,parsed) && parsed==slot)return true;
    wire.clear();return false;
}
inline bool Context(Crossplay::Edition edition,const CrossplayNative::ParticipantEvent& binding,CrossplaySessionConfig::Connections ids,
                    uint32_t target,bool closed,uintptr_t& session,uintptr_t& cache_address) {
    using namespace CrossplayNativeCodec;session=cache_address=0;
    if(target<3 || target>4 || !PlayerOptionDetail::Context(edition,binding,ids,false))return false;
    CrossplayNative::LobbySnapshot snapshot;
    auto* cache=ReadyStateDetail::HistoryConfiguration(edition);const auto p=Detail::Pin(edition);
    if(!cache || !binding.CaptureLobby(snapshot) || !SessionConfigDetail::Fields(p,cache+0x24,cache+0x34,cache+0x44,ids))return false;
    const auto* rows=*reinterpret_cast<const unsigned char* const*>(cache+0x44);
    for(size_t i=0;i<8;++i) {
        const auto* row=rows+i*0x18;const auto state=CrossplayChat::Word(row);
        if(row[12])return false;
        if(i<2){if(row[4]!=1 || (state!=3 && state!=4))return false;}
        else if(i<4) {
            const bool expected_closed=i+1<target || (i+1==target && closed);
            if(state!=(expected_closed?2u:1u) || row[4]!=(expected_closed?1u:0u)
                || CrossplayChat::Word(row+8)!=(expected_closed?static_cast<uint32_t>(i+1):UINT32_MAX)
                || CrossplayChat::Word(row+16) || (expected_closed && CrossplayChat::Word(row+20)))return false;
        } else if(state!=0)return false;
    }
    session=snapshot.instance;cache_address=reinterpret_cast<uintptr_t>(cache);return session!=0;
}
inline bool Deliver(Crossplay::Edition edition,const CrossplayNative::ParticipantEvent& binding,CrossplaySessionConfig::Connections ids,
                    const std::vector<unsigned char>& wire,uintptr_t& session,uintptr_t& cache) {
    using namespace CrossplayNativeCodec;uint32_t slot=0;
    if(!Wire(wire,ids.host,slot) || !Methods(edition) || !Context(edition,binding,ids,slot,false,session,cache))return false;
    auto p=Detail::Pin(edition);p.event_vt=Vtable(edition);Detail::OwnedStream stream(p);if(!stream.initialized)return false;
    stream.value.bytes=const_cast<unsigned char*>(wire.data());stream.value.capacity=stream.value.length=static_cast<uint32_t>(wire.size());stream.value.external=1;
    Detail::Visitor reader{p.reader_vt,1,&stream.value,0};
    void* decoded=reinterpret_cast<void* (__thiscall*)(void*,const char*,uint32_t)>(p.read)(&reader,"WePlayEvent",0);
    if(!decoded || !Detail::Readable(decoded,20) || *static_cast<const uintptr_t*>(decoded)!=p.event_vt)return false;
    std::vector<unsigned char> repeated;uintptr_t s=0,c=0;
    bool okay=stream.value.position==wire.size() && Detail::Encode(p,decoded,repeated) && repeated==wire
        && Context(edition,binding,ids,slot,false,s,c) && s==session && c==cache;
    uintptr_t sender=0;void* transport=okay?Detail::Transport(edition,sender):nullptr;
    if(transport)reinterpret_cast<void (__thiscall*)(void*,const void*,uint32_t,uint32_t,uint32_t,uint32_t)>(sender)(transport,decoded,0,0,0,0);
    else okay=false;
    reinterpret_cast<void (__thiscall*)(void*,uint32_t)>(*reinterpret_cast<const uintptr_t*>(p.event_vt))(decoded,1);return okay;
}
}
