#pragma once
#include "crossplay_ai_profile.h"
#include "crossplay_ai_profile_pins.h"
#include "crossplay_ready_state_codec.h"

// Opt-in original lobby event codec. CPU-cache observation alone does not
// establish AI preparation/profile agreement or grant world-start permission.
namespace CrossplayAIProfile {
struct CacheLock {
    CRITICAL_SECTION* section=nullptr;bool acquired=false;
    CacheLock(Crossplay::Edition edition,unsigned char* cache) {
        using CrossplayNativeCodec::Detail::Readable;
        const unsigned index=static_cast<unsigned>(edition)-1;
        if(index>=3 || !Readable(cache,12))return;
        const auto* holder=*reinterpret_cast<const unsigned char* const*>(cache+8);
        if(!Readable(holder,8) || CrossplayChat::Word(holder)!=CrossplayAIProfileGuards::MutexTables[index])return;
        section=*reinterpret_cast<CRITICAL_SECTION* const*>(holder+4);MEMORY_BASIC_INFORMATION memory{};
        if(!Readable(section,sizeof(*section)) || !VirtualQuery(section,&memory,sizeof(memory)))return;
        const DWORD access=memory.Protect&0xff;
        if(access!=PAGE_READWRITE && access!=PAGE_WRITECOPY)return;
        acquired=TryEnterCriticalSection(section)!=FALSE;
    }
    ~CacheLock(){if(acquired)LeaveCriticalSection(section);}
};
struct ContextSnapshot {uintptr_t session=0,cache=0,slots=0;};
inline bool Same(const ContextSnapshot& a,const ContextSnapshot& b) {
    return a.session && a.session==b.session && a.cache==b.cache && a.slots==b.slots;
}
inline CrossplayNativeCodec::Detail::Profile Pin(Crossplay::Edition edition) {
    auto p=CrossplayNativeCodec::Detail::Pin(edition);
    const unsigned index=static_cast<unsigned>(edition)-1;
    p.event_vt=index<3?CrossplayAIProfileGuards::Tables[index]:0;return p;
}
inline bool Methods(Crossplay::Edition edition) {
    using namespace CrossplayNativeCodec;
    const unsigned index=static_cast<unsigned>(edition)-1;if(index>=3)return false;
    const auto p=Pin(edition);
    if(!Detail::Methods(p) || !Detail::Tables(p,edition))return false;
    for(const auto& region:CrossplayAIProfileGuards::Pins[index]) {
        const auto* bytes=reinterpret_cast<const unsigned char*>(region.address);
        if(!region.bytes || !Detail::Readable(bytes,region.bytes))return false;
        uint64_t hash=14695981039346656037ull;
        for(unsigned i=0;i<region.bytes;++i)hash=(hash^bytes[i])*1099511628211ull;
        if(hash!=region.hash)return false;
    }
    return true;
}
inline bool Context(Crossplay::Edition edition,const CrossplayNative::ParticipantEvent& binding,
                    CrossplaySessionConfig::Connections ids,bool host,ContextSnapshot& output,unsigned* stage=nullptr,bool require_ready=true) {
    using namespace CrossplayNativeCodec;output={};
    if(stage)*stage=1;
    const unsigned index=static_cast<unsigned>(edition)-1;
    if(index>=3 || !SessionConfigDetail::Roster(binding,ids,host))return false;
    if(stage)*stage=2;
    CrossplayNative::LobbySnapshot lobby;
    if(!binding.CaptureLobby(lobby))return false;
    if(stage)*stage=3;
    auto* cache=edition==Crossplay::Edition::Gold?SessionConfigDetail::GoldConfiguration():ReadyStateDetail::HistoryConfiguration(edition);
    if(!cache)return false;
    if(stage)*stage=4;
    CacheLock lock(edition,cache);if(!lock.acquired)return false;
    if(stage)*stage=5;
    std::string map,save;
    if(!SessionConfigDetail::NativeString(cache+0x24,map) || map!="Maps\\Skirmish\\s04_m06_Monhaim_Valley"
        || !SessionConfigDetail::NativeString(cache+0x34,save) || !save.empty())return false;
    if(stage)*stage=6;
    const auto* vector=reinterpret_cast<const uintptr_t*>(cache+(edition==Crossplay::Edition::Gold?0x48:0x44));
    constexpr size_t size=8*0x18;
    if(vector[1]<vector[0] || vector[1]-vector[0]!=size || vector[2]<vector[1] || vector[2]-vector[0]>2*size
        || !Detail::Readable(reinterpret_cast<void*>(vector[0]),size))return false;
    for(unsigned i=0;i<8;++i) {
        if(stage)*stage=10+i;
        const auto* row=reinterpret_cast<const unsigned char*>(vector[0]+i*0x18);
        const auto state=CrossplayChat::Word(row),slot=CrossplayChat::Word(row+8),station=CrossplayChat::Word(row+16);
        if(row[12])return false;
        if(i<2) {
            if((state!=3 && state!=4) || row[4]>1 || (require_ready && row[4]!=1)
                || slot!=i+1 || (station && station!=(i?ids.guest:ids.host)))return false;
        } else {
            if(station || CrossplayChat::Word(row+20))return false;
            if(i<4 && (state==2 || state==4)) {
                if(row[4]!=1 || slot!=i+1)return false;
            } else if((state!=0 && state!=1 && state!=5) || row[4] || slot!=UINT32_MAX)return false;
        }
    }
    output={lobby.instance,reinterpret_cast<uintptr_t>(cache),vector[0]};if(stage)*stage=99;return true;
}
inline bool Capture(const CrossplayNative::ParticipantEvent& binding,CrossplaySessionConfig::Connections ids,
                    const void* event,std::vector<unsigned char>& wire) {
    using namespace CrossplayNativeCodec;wire.clear();ContextSnapshot before{},after{};
    if(!Methods(Crossplay::Edition::Gold) || !Context(Crossplay::Edition::Gold,binding,ids,true,before)
        || !Detail::Readable(event,0x18))return false;
    const auto* bytes=static_cast<const unsigned char*>(event);const auto p=Pin(Crossplay::Edition::Gold);
    if(CrossplayChat::Word(bytes)!=p.event_vt || CrossplayChat::Word(bytes+4)!=Type
        || CrossplayChat::Word(bytes+8)!=ids.host || bytes[12]!=1)return false;
    Value value{};
    if(!Detail::Encode(p,event,wire) || !Wire(wire,ids.host,value)
        || value.slot!=CrossplayChat::Word(bytes+16) || value.profile!=CrossplayChat::Word(bytes+20)
        || !Context(Crossplay::Edition::Gold,binding,ids,true,after) || !Same(before,after)) {
        wire.clear();return false;
    }
    return true;
}
inline bool Deliver(Crossplay::Edition edition,const CrossplayNative::ParticipantEvent& binding,
                    CrossplaySessionConfig::Connections ids,const std::vector<unsigned char>& wire,ContextSnapshot& snapshot,unsigned* stage=nullptr) {
    using namespace CrossplayNativeCodec;snapshot={};Value value{};ContextSnapshot before{},after{};
    if(stage)*stage=1;
    if(edition==Crossplay::Edition::Gold || !Wire(wire,ids.host,value))return false;
    if(stage)*stage=2;
    if(!Methods(edition))return false;
    unsigned context_stage=0;
    if(!Context(edition,binding,ids,false,before,&context_stage)){if(stage)*stage=100+context_stage;return false;}
    if(stage)*stage=200;
    const auto p=Pin(edition);Detail::OwnedStream stream(p);if(!stream.initialized)return false;
    stream.value.bytes=const_cast<unsigned char*>(wire.data());
    stream.value.capacity=stream.value.length=static_cast<uint32_t>(wire.size());stream.value.external=1;
    Detail::Visitor reader{p.reader_vt,1,&stream.value,0};
    void* decoded=reinterpret_cast<void* (__thiscall*)(void*,const char*,uint32_t)>(p.read)(&reader,"WePlayEvent",0);
    if(stage)*stage=201;
    if(!decoded || !Detail::Readable(decoded,0x18) || *static_cast<const uintptr_t*>(decoded)!=p.event_vt)return false;
    std::vector<unsigned char> repeated;
    if(stage)*stage=202;
    bool okay=stream.value.position==wire.size() && Detail::Encode(p,decoded,repeated) && repeated==wire;
    if(okay){if(stage)*stage=203;okay=Context(edition,binding,ids,false,after) && Same(before,after);}
    uintptr_t sender=0;void* transport=okay?Detail::Transport(edition,sender):nullptr;
    if(okay && stage)*stage=204;
    if(transport)reinterpret_cast<void (__thiscall*)(void*,const void*,uint32_t,uint32_t,uint32_t,uint32_t)>(sender)(transport,decoded,0,0,0,0);
    else okay=false;
    // The original local sender owns its clone; release only our factory value.
    reinterpret_cast<void (__thiscall*)(void*,uint32_t)>(*reinterpret_cast<const uintptr_t*>(p.event_vt))(decoded,1);
    if(!okay)return false;
    if(stage)*stage=205;
    if(!Context(edition,binding,ids,false,after) || !Same(before,after))return false;
    snapshot=after;if(stage)*stage=999;return true;
}
inline bool CpuObserved(Crossplay::Edition edition,const CrossplayNative::ParticipantEvent& binding,
                        CrossplaySessionConfig::Connections ids,uint32_t slot,const ContextSnapshot& expected) {
    if(slot<3 || slot>4)return false;
    ContextSnapshot now{};if(!Context(edition,binding,ids,false,now) || !Same(expected,now))return false;
    CacheLock lock(edition,reinterpret_cast<unsigned char*>(now.cache));if(!lock.acquired)return false;
    const auto* row=reinterpret_cast<const unsigned char*>(now.slots+(slot-1)*0x18);
    return CrossplayChat::Word(row)==4 && row[4]==1 && CrossplayChat::Word(row+8)==slot;
}
}
