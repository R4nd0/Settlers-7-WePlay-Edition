#pragma once
#include "crossplay_event_codec.h"
#include "crossplay_session_config.h"
#include "crossplay_admission.h"

namespace CrossplayNativeCodec {
namespace SessionConfigDetail {
inline Detail::Profile Pin(Crossplay::Edition edition) {
    auto p=Detail::Pin(edition);
    p.event_vt=edition==Crossplay::Edition::Gold?0xf4e0c0:edition==Crossplay::Edition::UbisoftHistory?0x114dd0c:0x114c238;
    return p;
}
inline bool Methods(Crossplay::Edition edition) {
    const unsigned index=static_cast<unsigned>(edition)-1;if(index>=3)return false;
    const auto p=Pin(edition);
    const uintptr_t helpers[]={0x98d690,0x8405a0,0x840370};
    const size_t lengths[]={0x8e,0x86,0x86};
    const uintptr_t constructors[]={0x98d0a0,0x83ffc0,0x83fd90};
    const size_t constructor_lengths[]={0xae,0xc0,0xc0};
    const uint64_t fingerprints[]={0x4cabf486191fdfd4ull,0x68f941f1622e656eull,0xed2dfc3561ad3fffull};
    if(!Detail::Methods(p) || !Detail::Tables(p,edition) || !Detail::Readable(reinterpret_cast<void*>(p.event_vt),24)
        || !Detail::Readable(reinterpret_cast<void*>(helpers[index]),lengths[index])
        || !Detail::Readable(reinterpret_cast<void*>(constructors[index]),constructor_lengths[index]))return false;
    uint64_t hash=14695981039346656037ull;
    for(unsigned i=0;i<6;++i) {
        const auto* entry=reinterpret_cast<const unsigned char*>(p.event_vt+4*i);uintptr_t method=0;memcpy(&method,entry,4);
        if(method<0x400000 || method>=0x1500000 || !Detail::Readable(reinterpret_cast<void*>(method),8))return false;
        for(unsigned j=0;j<4;++j)hash=(hash^entry[j])*1099511628211ull;
        const auto* code=reinterpret_cast<const unsigned char*>(method);
        for(unsigned j=0;j<8;++j)hash=(hash^code[j])*1099511628211ull;
    }
    const auto* code=reinterpret_cast<const unsigned char*>(helpers[index]);
    for(size_t j=0;j<lengths[index];++j)hash=(hash^code[j])*1099511628211ull;
    code=reinterpret_cast<const unsigned char*>(constructors[index]);
    for(size_t j=0;j<constructor_lengths[index];++j)hash=(hash^code[j])*1099511628211ull;
    return hash==fingerprints[index];
}
inline bool Roster(const CrossplayNative::ParticipantEvent& binding,CrossplaySessionConfig::Connections ids,bool host) {
    if(!CrossplaySessionConfig::Distinct(ids))return false;
    CrossplayNative::LobbySnapshot snapshot;
    if(!binding.CaptureLobby(snapshot) || snapshot.members.size()!=2 || snapshot.host!=host || snapshot.local_slot!=(host?1u:2u))return false;
    unsigned seen=0;
    for(const auto& member:snapshot.members) {
        const auto& p=member.player;
        if(member.status!=2 || p.slot<1 || p.slot>2 || p.connection!=(p.slot==1?ids.host:ids.guest)
            || p.host!=(p.slot==1) || p.local!=(p.slot==(host?1u:2u)) || (seen&(1u<<p.slot)))return false;
        seen|=1u<<p.slot;
    }
    return seen==6;
}
inline bool NativeString(const unsigned char* object,std::string& result) {
    struct String {const char* data;uint32_t capacity,length,extra;};
    const auto& s=*reinterpret_cast<const String*>(object);result.clear();
    if(s.length>CrossplaySessionConfig::MaximumPath)return false;
    if(!s.length)return true;
    if(s.capacity<s.length+1 || !Detail::Readable(s.data,s.length+1) || s.data[s.length])return false;
    for(uint32_t i=0;i<s.length;++i)if(static_cast<unsigned char>(s.data[i])<32 || static_cast<unsigned char>(s.data[i])>126)return false;
    result.assign(s.data,s.length);return true;
}
template<class SlotCheck>
inline bool FieldsChecked(const Detail::Profile& p,const unsigned char* map_field,const unsigned char* save_field,
                          const unsigned char* container,SlotCheck check) {
    if(!Detail::Readable(map_field,16) || !Detail::Readable(save_field,16)
        || !Detail::Readable(container,p.gold?16:12))return false;
    std::string map,save;
    if(!NativeString(map_field,map) || !CrossplaySessionConfig::RelativeMap(map) || !NativeString(save_field,save) || !save.empty())return false;
    const auto* vector=reinterpret_cast<const uintptr_t*>(container+(p.gold?4:0));
    constexpr size_t bytes=CrossplaySessionConfig::Slots*0x18;
    if(vector[1]<vector[0] || vector[1]-vector[0]!=bytes || vector[2]<vector[1]
        || vector[2]-vector[0]>2*bytes || !Detail::Readable(reinterpret_cast<void*>(vector[0]),bytes))return false;
    for(size_t i=0;i<CrossplaySessionConfig::Slots;++i) {
        const auto* row=reinterpret_cast<const unsigned char*>(vector[0]+i*0x18);
        const uint32_t state=CrossplayChat::Word(row),slot=CrossplayChat::Word(row+8),station=CrossplayChat::Word(row+0x10);
        if(!check(i,row[4],row[0xc],slot,station,state))return false;
    }
    return true;
}
inline bool Fields(const Detail::Profile& p,const unsigned char* map_field,const unsigned char* save_field,const unsigned char* container,
                   CrossplaySessionConfig::Connections ids) {
    return CrossplaySessionConfig::Distinct(ids)
        && FieldsChecked(p,map_field,save_field,container,[&](size_t i,unsigned ready,unsigned privacy,uint32_t slot,uint32_t station,uint32_t state) {
            return CrossplaySessionConfig::SlotValid(i,ready,privacy,slot,station,state,ids,true);
        });
}
inline bool Source(const Detail::Profile& p,const void* event,CrossplaySessionConfig::Connections ids) {
    const auto* data=static_cast<const unsigned char*>(event);
    return Detail::Readable(data,p.gold?0x40:0x3c) && *reinterpret_cast<const uintptr_t*>(data)==p.event_vt
        && CrossplayChat::Word(data+4)==CrossplaySessionConfig::Type && CrossplayChat::Word(data+8)==ids.host && data[12]==1
        && Fields(p,data+0x10,data+0x20,data+0x30,ids);
}
inline bool RoomSource(const Detail::Profile& p,const void* event,const CrossplaySessionConfig::RoomConnections& routes) {
    const auto* data=static_cast<const unsigned char*>(event);
    return CrossplaySessionConfig::ValidRoomConnections(routes)
        && Detail::Readable(data,p.gold?0x40:0x3c) && *reinterpret_cast<const uintptr_t*>(data)==p.event_vt
        && CrossplayChat::Word(data+4)==CrossplaySessionConfig::Type
        && CrossplayChat::Word(data+8)==routes.host && data[12]==1
        && FieldsChecked(p,data+0x10,data+0x20,data+0x30,[&](size_t i,unsigned ready,unsigned privacy,
                uint32_t slot,uint32_t station,uint32_t state){
            return CrossplaySessionConfig::RoomSlotValid(i,ready,privacy,slot,station,state,routes,true);
        });
}
inline void* GoldConstruct(uintptr_t address,const void* map,void* output,uint32_t source,const void* save,const void* slots) {
    void* result=nullptr;
    __asm {
        mov ecx,map
        push slots
        push save
        push source
        push output
        call address
        mov result,eax
    }
    return result;
}
inline unsigned char* GoldConfiguration() {
    uintptr_t send=0;auto* transport=static_cast<unsigned char*>(Detail::Transport(Crossplay::Edition::Gold,send));
    if(!transport)return nullptr;
    const auto* head=*reinterpret_cast<const unsigned char* const*>(transport+8);
    if(!Detail::Readable(head,12))return nullptr;
    const auto* node=*reinterpret_cast<const unsigned char* const*>(head);unsigned char* result=nullptr;
    unsigned count=0;
    for(;node!=head && count<64;++count) {
        if(!Detail::Readable(node,12))return nullptr;
        auto* plugin=*reinterpret_cast<unsigned char* const*>(node+8);
        if(!Detail::Readable(plugin,4))return nullptr;
        if(*reinterpret_cast<const uintptr_t*>(plugin)==0xf4e074) {
            if(result || !Detail::Readable(plugin,0x60))return nullptr;
            result=plugin;
        }
        node=*reinterpret_cast<const unsigned char* const*>(node);
    }
    return node==head?result:nullptr;
}
struct OwnedGoldConfig {
    alignas(4) unsigned char bytes[0x40]{};bool initialized=false;
    ~OwnedGoldConfig() {
        if(initialized)reinterpret_cast<void (__thiscall*)(void*,uint32_t)>(*reinterpret_cast<const uintptr_t*>(0xf4e0c0))(bytes,0);
    }
};
struct ConfigLock {
    CRITICAL_SECTION* section=nullptr;bool acquired=false;
    explicit ConfigLock(unsigned char* config) {
        const auto* holder=*reinterpret_cast<const unsigned char* const*>(config+8);
        if(!Detail::Readable(holder,8) || *reinterpret_cast<const uintptr_t*>(holder)!=0xf30dcc)return;
        auto* candidate=*reinterpret_cast<CRITICAL_SECTION* const*>(holder+4);MEMORY_BASIC_INFORMATION memory{};
        if(!Detail::Readable(candidate,sizeof(*candidate)) || !VirtualQuery(candidate,&memory,sizeof(memory)))return;
        const DWORD access=memory.Protect&0xff;
        if(access!=PAGE_READWRITE && access!=PAGE_WRITECOPY)return;
        section=candidate;acquired=TryEnterCriticalSection(section)!=FALSE;
    }
    ~ConfigLock(){if(acquired)LeaveCriticalSection(section);}
};
template<class SourceCheck,class OwnershipCheck>
inline bool DecodeChecked(Crossplay::Edition edition,const std::vector<unsigned char>& wire,
        SourceCheck check,OwnershipCheck owned) {
    if(!Methods(edition))return false;
    const auto p=Pin(edition);Detail::OwnedStream stream(p);if(!stream.initialized)return false;
    stream.value.bytes=const_cast<unsigned char*>(wire.data());stream.value.capacity=stream.value.length=static_cast<uint32_t>(wire.size());stream.value.external=1;
    Detail::Visitor reader{p.reader_vt,1,&stream.value,0};const char* name="WePlayEvent";
    void* decoded=p.gold?Detail::GoldRead(p.read,&reader,name):
        reinterpret_cast<void* (__thiscall*)(void*,const char*,uint32_t)>(p.read)(&reader,name,0);
    if(!decoded || !Detail::Readable(decoded,p.gold?0x40:0x3c) || *static_cast<const uintptr_t*>(decoded)!=p.event_vt)return false;
    std::vector<unsigned char> repeated;
    bool okay=stream.value.position==wire.size() && check(p,decoded) && Detail::Encode(p,decoded,repeated)
        && wire==repeated && owned();
    uintptr_t send=0;void* transport=okay?Detail::Transport(edition,send):nullptr;
    if(transport)reinterpret_cast<void (__thiscall*)(void*,const void*,uint32_t,uint32_t,uint32_t,uint32_t)>(send)(transport,decoded,0,0,0,0);
    else okay=false;
    reinterpret_cast<void (__thiscall*)(void*,uint32_t)>(*reinterpret_cast<const uintptr_t*>(p.event_vt))(decoded,1);
    return okay;
}
inline bool Decode(Crossplay::Edition edition,const std::vector<unsigned char>& wire,CrossplaySessionConfig::Connections ids) {
    CrossplaySessionConfig::Layout layout;
    return CrossplaySessionConfig::Inspect(wire,ids,layout)
        && DecodeChecked(edition,wire,[&](const auto& p,const void* event){return Source(p,event,ids);},[]{return true;});
}
}
inline bool CaptureGoldSessionConfig(const CrossplayNative::ParticipantEvent& binding,CrossplaySessionConfig::Connections ids,
                                    std::vector<unsigned char>& wire) {
    wire.clear();
    if(!SessionConfigDetail::Roster(binding,ids,true) || !SessionConfigDetail::Methods(Crossplay::Edition::Gold))return false;
    auto* config=SessionConfigDetail::GoldConfiguration();if(!config)return false;
    SessionConfigDetail::ConfigLock lock(config);if(!lock.acquired)return false;
    const auto p=SessionConfigDetail::Pin(Crossplay::Edition::Gold);
    if(!SessionConfigDetail::Fields(p,config+0x24,config+0x34,config+0x44,ids))return false;
    SessionConfigDetail::OwnedGoldConfig event;
    void* result=SessionConfigDetail::GoldConstruct(0x98d0a0,config+0x24,event.bytes,ids.host,config+0x34,config+0x44);
    event.initialized=*reinterpret_cast<const uintptr_t*>(event.bytes)==p.event_vt;
    if(!event.initialized || result!=event.bytes || !SessionConfigDetail::Source(p,event.bytes,ids)
        || *reinterpret_cast<const uintptr_t*>(event.bytes+0x10)==*reinterpret_cast<const uintptr_t*>(config+0x24)
        || *reinterpret_cast<const uintptr_t*>(event.bytes+0x34)==*reinterpret_cast<const uintptr_t*>(config+0x48)
        || SessionConfigDetail::GoldConfiguration()!=config || !SessionConfigDetail::Roster(binding,ids,true))return false;
    std::vector<unsigned char> snapshot;
    return Detail::Encode(p,event.bytes,snapshot) && CrossplaySessionConfig::BindSource(snapshot,ids,wire);
}
inline bool EncodeSessionConfig(Crossplay::Edition edition,const CrossplayNative::ParticipantEvent& binding,
                                CrossplaySessionConfig::Connections ids,const void* event,std::vector<unsigned char>& wire) {
    wire.clear();
    if(!SessionConfigDetail::Roster(binding,ids,true) || !SessionConfigDetail::Methods(edition))return false;
    const auto p=SessionConfigDetail::Pin(edition);std::vector<unsigned char> snapshot;
    if(SessionConfigDetail::Source(p,event,ids) && Detail::Encode(p,event,snapshot) && CrossplaySessionConfig::BindSource(snapshot,ids,wire))return true;
    wire.clear();return false;
}
inline bool EnqueueSessionConfig(Crossplay::Edition edition,const CrossplayNative::ParticipantEvent& binding,
                                 CrossplaySessionConfig::Connections ids,const std::vector<unsigned char>& wire) {
    return SessionConfigDetail::Roster(binding,ids,false) && SessionConfigDetail::Decode(edition,wire,ids);
}
inline bool EncodeRoomSessionConfig(const CrossplayNative::RoomLobbyAdmission& admission,unsigned char own_slot,
        unsigned char capacity,const std::string& map,const void* event,CrossplaySessionConfig::RoomConnections& routes,
        std::vector<unsigned char>& wire) {
    wire.clear();
    routes={};
    CrossplayNative::RoomRoute own;
    CrossplaySessionConfig::RoomConnections current;
    if(!admission.Resolve(own_slot,false,own) || !own.local || !own.host
        || !admission.CaptureConfigurationRoutes(capacity,current) || !SessionConfigDetail::Methods(own.receiver))return false;
    const auto p=SessionConfigDetail::Pin(own.receiver);
    CrossplaySessionConfig::Layout layout;
    std::vector<unsigned char> snapshot;
    if(!SessionConfigDetail::RoomSource(p,event,current) || !Detail::Encode(p,event,snapshot)
        || !CrossplaySessionConfig::InspectRoom(snapshot,current,layout,true) || layout.map!=map
        || !CrossplaySessionConfig::ProjectRoom(snapshot,current,current,wire,true))return false;
    routes=current;
    return true;
}

// source routes accompany the observed host snapshot in the group setup
// protocol. Validate the authenticated sender and the local roster before
// asking the original engine to decode that configuration.
inline bool EnqueueRoomSessionConfig(const CrossplayNative::RoomLobbyAdmission& admission,const Crossplay::RoomEvent& event,
        const CrossplaySessionConfig::RoomConnections& source,unsigned char capacity,const std::string& map) {
    CrossplayNative::RoomRoute host;
    CrossplaySessionConfig::RoomConnections current;
    CrossplaySessionConfig::Layout layout;
    if(!admission.Resolve(event.source_slot,false,host) || !host.host || host.local
        || source.host!=host.source_cid || !admission.CaptureConfigurationRoutes(capacity,current)
        || !CrossplaySessionConfig::InspectRoom(event.bytes,source,layout) || layout.map!=map)return false;
    std::vector<unsigned char> wire;
    if(!CrossplaySessionConfig::ProjectRoom(event.bytes,source,current,wire))return false;
    return SessionConfigDetail::DecodeChecked(host.receiver,wire,
        [&](const auto& p,const void* decoded){return SessionConfigDetail::RoomSource(p,decoded,current);},
        [&]{
            CrossplaySessionConfig::RoomConnections fresh;
            return admission.CaptureConfigurationRoutes(capacity,fresh) && fresh.host==current.host
                && fresh.stations==current.stations;
        });
}
}
