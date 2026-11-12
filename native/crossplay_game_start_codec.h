#pragma once
#include "crossplay_ready_state_codec.h"
#include "crossplay_game_start.h"

// Opt-in primitive only: ordinary adapters never include this header. Callers
// must finish native setup/content agreement before invoking local delivery.
namespace CrossplayNativeCodec {
namespace GameStartDetail {
inline Detail::Profile Pin(Crossplay::Edition edition) {
    auto p=Detail::Pin(edition);p.event_vt=edition==Crossplay::Edition::Gold?0xf555e4:
        edition==Crossplay::Edition::UbisoftHistory?0x113f2d8:0x113d7f8;return p;
}
inline bool Methods(Crossplay::Edition edition) {
    const unsigned index=static_cast<unsigned>(edition)-1;if(index>=3)return false;
    const auto p=Pin(edition);
    const uint64_t expected[]={0x6f31a6857c4f17c4ull,0xcae05f2dea131b5cull,0xe69772012a69d84bull};
    if(!Detail::Methods(p) || !Detail::Tables(p,edition) || !Detail::Readable(reinterpret_cast<void*>(p.event_vt),24))return false;
    uint64_t hash=14695981039346656037ull;
    for(unsigned i=0;i<6;++i) {
        const auto* entry=reinterpret_cast<const unsigned char*>(p.event_vt+4*i);uintptr_t method=0;memcpy(&method,entry,4);
        if(method<0x400000 || method>=0x1500000 || !Detail::Readable(reinterpret_cast<void*>(method),8))return false;
        for(unsigned j=0;j<4;++j)hash=(hash^entry[j])*1099511628211ull;
        const auto* code=reinterpret_cast<const unsigned char*>(method);
        for(unsigned j=0;j<8;++j)hash=(hash^code[j])*1099511628211ull;
    }
    return hash==expected[index];
}
inline bool Source(const Detail::Profile& p,const void* event,uint32_t host) {
    const auto* data=static_cast<const unsigned char*>(event);std::string save;
    return host!=UINT32_MAX && Detail::Readable(data,0x20) && *reinterpret_cast<const uintptr_t*>(data)==p.event_vt
        && CrossplayChat::Word(data+4)==CrossplayGameStart::Type && CrossplayChat::Word(data+8)==host
        && data[12]==1 && SessionConfigDetail::NativeString(data+0x10,save) && save.empty();
}
// Pure policy for an already range-checked eight-slot native cache. Closed
// neutral slots are the original start handler's state 2, not open/AI slots.
inline bool FreshReadySlots(const unsigned char* rows,size_t size,CrossplaySessionConfig::Connections ids) {
    if(!rows || size!=8*0x18 || !CrossplaySessionConfig::Distinct(ids))return false;
    for(size_t i=0;i<8;++i) {
        const auto* row=rows+i*0x18;
        const auto state=CrossplayChat::Word(row),slot=CrossplayChat::Word(row+8),station=CrossplayChat::Word(row+16);
        if(!CrossplaySessionConfig::SlotValid(i,row[4],row[12],slot,station,state,ids,true))return false;
        if(i<2){if(row[4]!=1)return false;}
        else if(state==2) {
            if(i>=4 || row[12] || CrossplayChat::Word(row+20))return false;
        } else if(state!=0 && state!=5)return false;
    }
    return true;
}
inline bool FreshReadyMap(Crossplay::Edition edition,const CrossplayNative::ParticipantEvent& binding,
                          CrossplaySessionConfig::Connections ids,bool host,const std::string& expected_map) {
    if(!SessionConfigDetail::Roster(binding,ids,host) || !CrossplaySessionConfig::RelativeMap(expected_map))return false;
    auto* cache=edition==Crossplay::Edition::Gold?SessionConfigDetail::GoldConfiguration():ReadyStateDetail::HistoryConfiguration(edition);
    std::string map,save;const auto p=Pin(edition);
    if(!cache || !SessionConfigDetail::NativeString(cache+0x24,map) || map!=expected_map
        || !SessionConfigDetail::NativeString(cache+0x34,save) || !save.empty()
        || !SessionConfigDetail::Fields(p,cache+0x24,cache+0x34,cache+0x44,ids))return false;
    const auto* slots=*reinterpret_cast<const unsigned char* const*>(cache+(p.gold?0x48:0x44));
    return FreshReadySlots(slots,8*0x18,ids);
}
inline bool DecodeAndEnqueue(Crossplay::Edition edition,const std::vector<unsigned char>& wire,uint32_t host) {
    if(!CrossplayGameStart::Inspect(wire,host) || !Methods(edition))return false;
    const auto p=Pin(edition);Detail::OwnedStream stream(p);if(!stream.initialized)return false;
    stream.value.bytes=const_cast<unsigned char*>(wire.data());
    stream.value.capacity=stream.value.length=static_cast<uint32_t>(wire.size());stream.value.external=1;
    Detail::Visitor reader{p.reader_vt,1,&stream.value,0};const char* name="WePlayEvent";
    void* decoded=p.gold?Detail::GoldRead(p.read,&reader,name):
        reinterpret_cast<void* (__thiscall*)(void*,const char*,uint32_t)>(p.read)(&reader,name,0);
    if(!decoded || !Detail::Readable(decoded,0x20) || *static_cast<const uintptr_t*>(decoded)!=p.event_vt)return false;
    std::vector<unsigned char> repeated;
    bool okay=stream.value.position==wire.size() && Source(p,decoded,host) && Detail::Encode(p,decoded,repeated) && repeated==wire;
    uintptr_t send=0;void* transport=okay?Detail::Transport(edition,send):nullptr;
    if(transport)reinterpret_cast<void (__thiscall*)(void*,const void*,uint32_t,uint32_t,uint32_t,uint32_t)>(send)(transport,decoded,0,0,0,0);
    else okay=false;
    // The original local-only send owns a clone. Release only our factory event.
    reinterpret_cast<void (__thiscall*)(void*,uint32_t)>(*reinterpret_cast<const uintptr_t*>(p.event_vt))(decoded,1);
    return okay;
}
}
inline bool EncodeFreshGameStart(Crossplay::Edition edition,const CrossplayNative::ParticipantEvent& binding,
                                 CrossplaySessionConfig::Connections ids,const std::string& map,
                                 const void* event,std::vector<unsigned char>& wire) {
    wire.clear();
    if(!GameStartDetail::Methods(edition) || !GameStartDetail::FreshReadyMap(edition,binding,ids,true,map))return false;
    const auto p=GameStartDetail::Pin(edition);
    if(GameStartDetail::Source(p,event,ids.host) && Detail::Encode(p,event,wire) && CrossplayGameStart::Inspect(wire,ids.host))return true;
    wire.clear();return false;
}
class FreshGameStartReceiver {
    bool attempted=false,bound=false;
    DWORD owner=0;uintptr_t session=0;
    ULONGLONG started=0;uint32_t source_host=UINT32_MAX;
    Crossplay::Edition edition{};Crossplay::Peer peer{};
    CrossplaySessionConfig::Connections ids{};
    CrossplayGameStart::Agreement agreement{};
    std::string map;
public:
    FreshGameStartReceiver()=default;
    FreshGameStartReceiver(const FreshGameStartReceiver&)=delete;
    FreshGameStartReceiver& operator=(const FreshGameStartReceiver&)=delete;
    bool Bind(Crossplay::Edition value,const CrossplayNative::ParticipantEvent& binding,
              CrossplaySessionConfig::Connections connections,const Crossplay::Peer& other,
              uint32_t remote_host,const CrossplayGameStart::Agreement& matched,const std::string& expected_map) {
        if(bound || attempted || remote_host==UINT32_MAX || !CrossplayGameStart::Valid(matched)
            || !GameStartDetail::Methods(value)
            || !GameStartDetail::FreshReadyMap(value,binding,connections,false,expected_map))return false;
        CrossplayNative::LobbySnapshot snapshot;
        if(!binding.CaptureLobby(snapshot) || !snapshot.instance)return false;
        edition=value;peer=other;ids=connections;agreement=matched;map=expected_map;
        session=snapshot.instance;owner=GetCurrentThreadId();source_host=remote_host;
        started=GetTickCount64();bound=true;return true;
    }
    bool Deliver(const CrossplayNative::ParticipantEvent& binding,const std::vector<unsigned char>& frame) {
        if(!bound || attempted || owner!=GetCurrentThreadId())return false;
        // One delivery attempt per owned interval, including an invalid frame
        // or native failure. Never retry a potentially enqueued start event.
        attempted=true;
        CrossplayNative::LobbySnapshot snapshot;std::vector<unsigned char> wire;
        return GetTickCount64()-started<=10000 && binding.CaptureLobby(snapshot) && snapshot.instance==session
            && GameStartDetail::FreshReadyMap(edition,binding,ids,false,map)
            && CrossplayGameStart::Decode(peer,agreement,frame,source_host,ids.host,wire)
            && GameStartDetail::DecodeAndEnqueue(edition,wire,ids.host);
    }
};
}
