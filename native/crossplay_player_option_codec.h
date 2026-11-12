#pragma once
#include "crossplay_ready_state_codec.h"
#include "crossplay_player_option.h"
// First laboratory path: genuine host-authored Gold notifications only.
// Guest intents, AI slots and arbitrary maps remain excluded.
namespace CrossplayNativeCodec {
namespace PlayerOptionDetail {
inline const uint32_t Tables[3][2]={{0xf4dfdc,0xf4dfa4},{0x11408ac,0x1140970},{0x113ee0c,0x113edf0}};
inline const uint64_t Fingerprints[3][2]={{0x8107d165eb87a8bfull,0xd0f90d7e1051bfcdull},{0x6da8d10ce120a9afull,0x2972e17f8cc5d5a7ull},{0x879ccf0542afabbfull,0x5a3aff7a32a78a29ull}};
struct Spec {uint32_t klass,type,vt;size_t width;uint64_t hash;};
inline bool Layout(Crossplay::Edition edition,uint32_t klass,Spec& result) {
    result={};
    const unsigned profile=static_cast<unsigned>(edition)-1;
    const uint32_t classes[]={CrossplayPlayerOption::Color,CrossplayPlayerOption::Team};
    const uint32_t types[]={7u|(0x32u<<16),7u|(0x1eu<<16)};
    if(profile>=3)return false;
    for(unsigned i=0;i<2;++i)if(klass==classes[i]) {
        result={klass,types[i],Tables[profile][i],i?1u:4u,Fingerprints[profile][i]};return true;
    }
    return false;
}
inline bool Methods(Crossplay::Edition edition,const Spec& spec) {
    using namespace CrossplayNativeCodec;
    Spec expected{};
    if(!Layout(edition,spec.klass,expected) || expected.type!=spec.type || expected.vt!=spec.vt
        || expected.width!=spec.width || expected.hash!=spec.hash)return false;
    const auto p=Detail::Pin(edition);
    if(!Detail::Methods(p) || !Detail::Tables(p,edition) || !Detail::Readable(reinterpret_cast<void*>(spec.vt),24))return false;
    uint64_t hash=14695981039346656037ull;
    for(unsigned i=0;i<6;++i) {
        const auto* entry=reinterpret_cast<const unsigned char*>(spec.vt+4*i);uint32_t method=0;memcpy(&method,entry,4);
        if(method<0x400000 || method>=0x1500000 || !Detail::Readable(reinterpret_cast<void*>(method),32))return false;
        for(unsigned j=0;j<4;++j)hash=(hash^entry[j])*1099511628211ull;
        const auto* code=reinterpret_cast<const unsigned char*>(method);
        for(unsigned j=0;j<32;++j)hash=(hash^code[j])*1099511628211ull;
    }
    return hash==spec.hash;
}
inline bool Inspect(Crossplay::Edition edition,const std::vector<unsigned char>& wire,uint32_t host,Spec& spec,uint32_t& slot,uint32_t& value) {
    spec={};slot=value=0;CrossplayPlayerOption::Value parsed;
    if(!CrossplayPlayerOption::Inspect(wire,host,parsed) || !Layout(edition,parsed.klass,spec))return false;
    slot=parsed.slot;value=parsed.value;return true;
}
inline bool Context(Crossplay::Edition edition,const CrossplayNative::ParticipantEvent& binding,CrossplaySessionConfig::Connections ids,bool host) {
    using namespace CrossplayNativeCodec;
    if(!SessionConfigDetail::Roster(binding,ids,host))return false;
    auto* cache=edition==Crossplay::Edition::Gold?SessionConfigDetail::GoldConfiguration():ReadyStateDetail::HistoryConfiguration(edition);
    std::string map;const auto p=Detail::Pin(edition);
    return cache && SessionConfigDetail::Fields(p,cache+0x24,cache+0x34,cache+0x44,ids)
        && SessionConfigDetail::NativeString(cache+0x24,map) && map=="Maps\\Skirmish\\s04_m06_Monhaim_Valley";
}
inline bool ReadValue(Crossplay::Edition edition,const CrossplayNative::ParticipantEvent& binding,CrossplaySessionConfig::Connections ids,
                      bool host,const Spec& spec,uint32_t slot,uint32_t& value) {
    using namespace CrossplayNativeCodec;value=0;
    if(slot<1 || slot>2 || !Context(edition,binding,ids,host))return false;
    const bool gold=edition==Crossplay::Edition::Gold,steam=edition==Crossplay::Edition::SteamHistory;
    const uintptr_t pointer=gold?0x10f834c:steam?0x13a2300:0x13a5218,type=gold?0xf51af0:steam?0x113939c:0x113ae54;
    const size_t stride=gold?0xd4:0xc4,offset=gold?0xa4:0x98,row_size=gold?0x5c4:0x5a8;
    if(!Detail::Readable(reinterpret_cast<void*>(pointer),4))return false;
    const auto* manager=*reinterpret_cast<const unsigned char* const*>(pointer);
    if(!Detail::Readable(manager,8+2*stride) || CrossplayChat::Word(manager)!=type || manager[4]>1)return false;
    const unsigned char mode=manager[4];
    const auto* config=*reinterpret_cast<const unsigned char* const*>(manager+((mode-1)&1)*stride+offset);
    if(!Detail::Readable(config,8))return false;
    const auto* rows=*reinterpret_cast<const unsigned char* const*>(config+4);
    if(*reinterpret_cast<const unsigned char* const*>(config)!=rows || !Detail::Readable(rows,(gold?0x9078:0x8db8)+1)
        || rows[gold?0x9078:0x8db8]!=4)return false;
    CrossplayNative::LobbySnapshot snapshot;
    if(!binding.CaptureLobby(snapshot) || snapshot.members.size()!=2)return false;
    for(const auto& member:snapshot.members) {
        const auto index=member.player.slot;if(index<1 || index>2)return false;
        const auto* owned=rows+index*row_size;
        const uint32_t principal=member.player.principal?member.player.principal:index;
        if(CrossplayChat::Word(owned+0x18)!=principal || CrossplayChat::Word(owned+0x5c)!=index
            || CrossplayChat::Word(owned+0x60)!=3 || CrossplayChat::Word(owned+0x64)!=1)return false;
    }
    const auto* row=rows+slot*row_size;
    if(spec.klass==0x0be2024e)value=CrossplayChat::Word(row+0x2c);
    else if(spec.klass==CrossplayPlayerOption::Team)value=row[0x68];
    else return false;
    return *reinterpret_cast<const unsigned char* const*>(pointer)==manager && manager[4]==mode
        && *reinterpret_cast<const unsigned char* const*>(manager+((mode-1)&1)*stride+offset)==config
        && *reinterpret_cast<const unsigned char* const*>(config+4)==rows && *reinterpret_cast<const unsigned char* const*>(config)==rows
        && Context(edition,binding,ids,host);
}
inline bool ColorSetterMethods(Crossplay::Edition edition) {
    if(edition!=Crossplay::Edition::UbisoftHistory && edition!=Crossplay::Edition::SteamHistory)return false;
    const uintptr_t setter=edition==Crossplay::Edition::SteamHistory?0x545a80:0x546430;
    const unsigned char code[]={0x55,0x8b,0xec,0x8b,0x45,0x08,0x89,0x41,0x2c,0x5d,0xc2,0x04,0x00};
    return Detail::Readable(reinterpret_cast<void*>(setter),sizeof(code)) && !memcmp(reinterpret_cast<void*>(setter),code,sizeof(code));
}
inline bool ApplyColor(Crossplay::Edition edition,const CrossplayNative::ParticipantEvent& binding,
                       CrossplaySessionConfig::Connections ids,const Spec& spec,uint32_t slot,uint32_t value) {
    using namespace CrossplayNativeCodec;
    uint32_t previous=0;
    if(edition==Crossplay::Edition::Gold || spec.klass!=0x0be2024e || slot<1 || slot>2 || value<2 || value>7
        || !ReadValue(edition,binding,ids,false,spec,slot,previous))return false;
    CrossplayNative::LobbySnapshot snapshot;unsigned char* owned_rows=nullptr;
    if(!ReadyStateDetail::Preparation(edition,binding,ids,snapshot,owned_rows))return false;
    const bool steam=edition==Crossplay::Edition::SteamHistory;
    const uintptr_t pointer=steam?0x13a2300:0x13a5218,setter=steam?0x545a80:0x546430;
    if(!ColorSetterMethods(edition))return false;
    const auto* manager=*reinterpret_cast<const unsigned char* const*>(pointer);
    auto* config=*reinterpret_cast<unsigned char* const*>(manager+((manager[4]-1)&1)*0xc4+0x98);
    auto* rows=*reinterpret_cast<unsigned char* const*>(config+4);auto* row=rows+slot*0x5a8;
    if(rows!=owned_rows)return false;
    const uint32_t id=CrossplayChat::Word(row+0x18),network_slot=CrossplayChat::Word(row+0x5c);
    if(!id || id==UINT32_MAX || network_slot!=slot || CrossplayChat::Word(row+0x60)!=3 || CrossplayChat::Word(row+0x64)!=1
        || !CrossplayNative::HistoryRoleDetail::Writable(row+0x2c,4))return false;
    reinterpret_cast<void (__thiscall*)(void*,uint32_t)>(setter)(row,value);
    uint32_t observed=0;
    return *reinterpret_cast<const unsigned char* const*>(pointer)==manager
        && *reinterpret_cast<unsigned char* const*>(config+4)==rows && CrossplayChat::Word(row+0x18)==id
        && ReadValue(edition,binding,ids,false,spec,slot,observed) && observed==value;
}
inline bool Encode(Crossplay::Edition edition,const CrossplayNative::ParticipantEvent& binding,CrossplaySessionConfig::Connections ids,
                   uint32_t klass,const void* event,std::vector<unsigned char>& wire) {
    using namespace CrossplayNativeCodec;wire.clear();Spec spec{};
    if(edition!=Crossplay::Edition::Gold || !Layout(edition,klass,spec) || !Methods(edition,spec) || !Context(edition,binding,ids,true))return false;
    const auto* data=static_cast<const unsigned char*>(event);
    if(!Detail::Readable(data,0x18) || CrossplayChat::Word(data)!=spec.vt || CrossplayChat::Word(data+4)!=spec.type
        || CrossplayChat::Word(data+8)!=ids.host || data[12]!=1)return false;
    auto p=Detail::Pin(edition);p.event_vt=spec.vt;uint32_t slot=0,value=0;
    if(Detail::Encode(p,event,wire) && Inspect(edition,wire,ids.host,spec,slot,value))return true;
    wire.clear();return false;
}
inline bool Deliver(Crossplay::Edition edition,const CrossplayNative::ParticipantEvent& binding,CrossplaySessionConfig::Connections ids,
                    const std::vector<unsigned char>& wire) {
    using namespace CrossplayNativeCodec;Spec spec{};uint32_t slot=0,value=0;
    if(edition==Crossplay::Edition::Gold || !Context(edition,binding,ids,false)
        || !Inspect(edition,wire,ids.host,spec,slot,value) || !Methods(edition,spec))return false;
    auto p=Detail::Pin(edition);p.event_vt=spec.vt;Detail::OwnedStream stream(p);if(!stream.initialized)return false;
    stream.value.bytes=const_cast<unsigned char*>(wire.data());stream.value.capacity=stream.value.length=static_cast<uint32_t>(wire.size());stream.value.external=1;
    Detail::Visitor reader{p.reader_vt,1,&stream.value,0};
    void* decoded=reinterpret_cast<void* (__thiscall*)(void*,const char*,uint32_t)>(p.read)(&reader,"WePlayEvent",0);
    if(!decoded || !Detail::Readable(decoded,0x18) || CrossplayChat::Word(static_cast<unsigned char*>(decoded))!=spec.vt)return false;
    std::vector<unsigned char> repeated;bool okay=stream.value.position==wire.size() && Detail::Encode(p,decoded,repeated) && repeated==wire;
    uintptr_t send=0;void* transport=okay?Detail::Transport(edition,send):nullptr;
    if(transport)reinterpret_cast<void (__thiscall*)(void*,const void*,uint32_t,uint32_t,uint32_t,uint32_t)>(send)(transport,decoded,0,0,0,0);
    else okay=false;
    reinterpret_cast<void (__thiscall*)(void*,uint32_t)>(*reinterpret_cast<const uintptr_t*>(spec.vt))(decoded,1);return okay;
}
}
inline bool EncodeGoldPlayerOption(const CrossplayNative::ParticipantEvent& binding,CrossplaySessionConfig::Connections ids,
                                  uint32_t klass,const void* event,std::vector<unsigned char>& wire) {
    return PlayerOptionDetail::Encode(Crossplay::Edition::Gold,binding,ids,klass,event,wire);
}
// One original enqueue attempt per object, including failures. Keep it alive
// until Observe returns Applied or Invalid; never replay a timed-out event.
class HistoryPlayerOptionReceiver {
    bool attempted=false,pending=false;
    DWORD owner=GetCurrentThreadId();ULONGLONG started=0;
    Crossplay::Edition edition=Crossplay::Edition::Gold;
    CrossplaySessionConfig::Connections ids{};
    uintptr_t session=0,rows=0;
    PlayerOptionDetail::Spec spec{};uint32_t slot=0,value=0;
public:
    HistoryPlayerOptionReceiver()=default;
    HistoryPlayerOptionReceiver(const HistoryPlayerOptionReceiver&)=delete;
    HistoryPlayerOptionReceiver& operator=(const HistoryPlayerOptionReceiver&)=delete;
    bool Deliver(Crossplay::Edition target,const CrossplayNative::ParticipantEvent& binding,
                 CrossplaySessionConfig::Connections connections,const std::vector<unsigned char>& wire) {
        if(attempted || owner!=GetCurrentThreadId())return false;
        attempted=true;CrossplayNative::LobbySnapshot snapshot;unsigned char* preparation=nullptr;
        if(!PlayerOptionDetail::Inspect(target,wire,connections.host,spec,slot,value)
            || !ReadyStateDetail::Preparation(target,binding,connections,snapshot,preparation))return false;
        edition=target;ids=connections;session=snapshot.instance;rows=reinterpret_cast<uintptr_t>(preparation);started=GetTickCount64();
        if(!PlayerOptionDetail::Deliver(edition,binding,ids,wire))return false;
        // History's Color handler only invalidates its presentation. Project
        // the owned preparation row with the original leaf setter as well.
        // Team is applied asynchronously by the original native consumer.
        if(spec.klass==CrossplayPlayerOption::Color && !PlayerOptionDetail::ApplyColor(edition,binding,ids,spec,slot,value))return false;
        pending=true;return true;
    }
    ReadyObservation Observe(const CrossplayNative::ParticipantEvent& binding) {
        if(!pending || owner!=GetCurrentThreadId() || GetTickCount64()-started>10000) {
            pending=false;return ReadyObservation::Invalid;
        }
        CrossplayNative::LobbySnapshot snapshot;unsigned char* preparation=nullptr;uint32_t observed=0;
        if(!ReadyStateDetail::Preparation(edition,binding,ids,snapshot,preparation) || snapshot.instance!=session
            || reinterpret_cast<uintptr_t>(preparation)!=rows || !PlayerOptionDetail::Methods(edition,spec)
            || !PlayerOptionDetail::ReadValue(edition,binding,ids,false,spec,slot,observed)) {
            pending=false;return ReadyObservation::Invalid;
        }
        if(observed!=value)return ReadyObservation::Pending;
        pending=false;return ReadyObservation::Applied;
    }
};
}
