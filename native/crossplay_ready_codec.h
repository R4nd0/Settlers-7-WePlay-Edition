#pragma once
#include "crossplay_session_config_codec.h"
#include "crossplay_ready.h"

namespace CrossplayNativeCodec {
namespace ReadyDetail {
inline bool GoldStateMethods();
inline Detail::Profile Pin(Crossplay::Edition edition) {
    auto p=Detail::Pin(edition);p.event_vt=edition==Crossplay::Edition::Gold?0xf562a8:
        edition==Crossplay::Edition::UbisoftHistory?0x11404a4:0x113e9b0;return p;
}
inline bool Methods(Crossplay::Edition edition) {
    const unsigned index=static_cast<unsigned>(edition)-1;if(index>=3)return false;
    const auto p=Pin(edition);
    const uintptr_t visitors[3][2]={{0xa49590,0xa494f0},{0xee4d90,0x69c440},{0xee4480,0x69c020}};
    const size_t lengths[3][2]={{0x94,0x94},{0x34,0xc7},{0x34,0xc7}};
    const uint64_t fingerprints[]={0x51ec306d40217242ull,0x99c0aa8acf1faf59ull,0xfdb344211af01e05ull};
    if(!Detail::Methods(p) || !Detail::Tables(p,edition) || !Detail::Readable(reinterpret_cast<void*>(p.event_vt),24))return false;
    uint64_t hash=14695981039346656037ull;
    for(unsigned i=0;i<6;++i) {
        const auto* entry=reinterpret_cast<const unsigned char*>(p.event_vt+4*i);uintptr_t method=0;memcpy(&method,entry,4);
        if(method<0x400000 || method>=0x1500000 || !Detail::Readable(reinterpret_cast<void*>(method),8))return false;
        for(unsigned j=0;j<4;++j)hash=(hash^entry[j])*1099511628211ull;
        const auto* code=reinterpret_cast<const unsigned char*>(method);
        for(unsigned j=0;j<8;++j)hash=(hash^code[j])*1099511628211ull;
    }
    for(unsigned i=0;i<2;++i) {
        const auto* code=reinterpret_cast<const unsigned char*>(visitors[index][i]);
        if(!Detail::Readable(code,lengths[index][i]))return false;
        for(size_t j=0;j<lengths[index][i];++j)hash=(hash^code[j])*1099511628211ull;
    }
    return hash==fingerprints[index] && (edition!=Crossplay::Edition::Gold || GoldStateMethods());
}
inline bool Source(const Detail::Profile& p,const void* event,uint32_t guest) {
    const auto* bytes=static_cast<const unsigned char*>(event);
    return guest!=UINT32_MAX && Detail::Readable(bytes,0x10) && *reinterpret_cast<const uintptr_t*>(bytes)==p.event_vt
        && CrossplayChat::Word(bytes+4)==CrossplayReady::Type && CrossplayChat::Word(bytes+8)==guest
        && bytes[12]==1;
}
inline bool GoldStateMethods() {
    const uintptr_t addresses[]={0x98e303,0xa51980};const size_t lengths[]={0xaf,0x105};
    uint64_t hash=14695981039346656037ull;
    for(unsigned i=0;i<2;++i) {
        const auto* code=reinterpret_cast<const unsigned char*>(addresses[i]);
        if(!Detail::Readable(code,lengths[i]))return false;
        for(size_t j=0;j<lengths[i];++j)hash=(hash^code[j])*1099511628211ull;
    }
    return hash==0x60c9979341fde4a4ull;
}
inline bool NativeStateSource(const Detail::Profile& p,const void* event,uint32_t guest) {
    const auto* bytes=static_cast<const unsigned char*>(event);
    return Source(p,event,guest) && Detail::Readable(bytes,0x18)
        && CrossplayChat::Word(bytes+0x10)==2 && bytes[0x14]<=1;
}
inline bool EnqueueState(Crossplay::Edition edition,const std::vector<unsigned char>& wire,uint32_t guest) {
    if(edition!=Crossplay::Edition::Gold || !CrossplayReady::NativeState(wire,guest) || !Methods(edition) || !GoldStateMethods())return false;
    const auto p=Pin(edition);Detail::OwnedStream stream(p);if(!stream.initialized)return false;
    stream.value.bytes=const_cast<unsigned char*>(wire.data());stream.value.capacity=stream.value.length=static_cast<uint32_t>(wire.size());stream.value.external=1;
    Detail::Visitor reader{p.reader_vt,1,&stream.value,0};const char* name="WePlayEvent";
    void* decoded=Detail::GoldRead(p.read,&reader,name);
    if(!decoded || !Detail::Readable(decoded,0x18) || *static_cast<const uintptr_t*>(decoded)!=p.event_vt)return false;
    std::vector<unsigned char> repeated;
    bool okay=stream.value.position==wire.size() && NativeStateSource(p,decoded,guest) && Detail::Encode(p,decoded,repeated) && wire==repeated;
    uintptr_t send=0;void* transport=okay?Detail::Transport(edition,send):nullptr;
    if(transport)reinterpret_cast<void (__thiscall*)(void*,const void*,uint32_t,uint32_t,uint32_t,uint32_t)>(send)(transport,decoded,0,0,0,0);
    else okay=false;
    reinterpret_cast<void (__thiscall*)(void*,uint32_t)>(*reinterpret_cast<const uintptr_t*>(p.event_vt))(decoded,1);
    return okay;
}
}
inline bool EncodeReadyRequest(Crossplay::Edition edition,const CrossplayNative::ParticipantEvent& binding,
                               CrossplaySessionConfig::Connections ids,const void* event,std::vector<unsigned char>& wire) {
    wire.clear();
    if(edition==Crossplay::Edition::Gold || !SessionConfigDetail::Roster(binding,ids,false) || !ReadyDetail::Methods(edition))return false;
    const auto p=ReadyDetail::Pin(edition);
    if(!ReadyDetail::Source(p,event,ids.guest))return false;
    wire.resize(CrossplayReady::WireSize);CrossplayChat::Put(wire.data(),CrossplayReady::Class);
    memcpy(wire.data()+4,static_cast<const unsigned char*>(event)+4,9);
    return CrossplayReady::Validate(wire,ids.guest);
}
struct PendingReadyToggle {
    uintptr_t configuration=0,session=0;CrossplaySessionConfig::Connections ids{};unsigned char before=0,after=0;
};
enum class ReadyObservation { Invalid, Pending, Applied };
inline bool GoldReadySlot(const CrossplayNative::ParticipantEvent& binding,CrossplaySessionConfig::Connections ids,
                          unsigned char*& config,unsigned char*& slot,uintptr_t& session) {
    if(!SessionConfigDetail::Roster(binding,ids,true))return false;
    CrossplayNative::LobbySnapshot snapshot;if(!binding.CaptureLobby(snapshot))return false;
    session=snapshot.instance;
    // This candidate is restricted to the original manually created lobby mode.
    // Original automatic-start mode 3 is excluded before any Ready event.
    const auto* object=reinterpret_cast<const unsigned char*>(session);
    if(!Detail::Readable(object,0x34) || *reinterpret_cast<const uintptr_t*>(object)!=0xf569bc
        || CrossplayChat::Word(object+0x30)!=2)return false;
    config=SessionConfigDetail::GoldConfiguration();if(!config)return false;
    const auto p=SessionConfigDetail::Pin(Crossplay::Edition::Gold);
    if(!SessionConfigDetail::Fields(p,config+0x24,config+0x34,config+0x44,ids))return false;
    slot=*reinterpret_cast<unsigned char**>(config+0x48)+0x18;return true;
}
inline bool EnqueueReadyToggle(Crossplay::Edition edition,const CrossplayNative::ParticipantEvent& binding,
                               CrossplaySessionConfig::Connections ids,const std::vector<unsigned char>& wire,PendingReadyToggle& pending) {
    pending={};
    if(edition!=Crossplay::Edition::Gold || !CrossplayReady::Validate(wire,ids.guest)
        || !SessionConfigDetail::Roster(binding,ids,true) || !ReadyDetail::Methods(edition) || !ReadyDetail::GoldStateMethods())return false;
    auto* config=SessionConfigDetail::GoldConfiguration();if(!config)return false;
    SessionConfigDetail::ConfigLock lock(config);if(!lock.acquired)return false;
    unsigned char* current=nullptr;unsigned char* slot=nullptr;uintptr_t session=0;
    if(!GoldReadySlot(binding,ids,current,slot,session) || current!=config)return false;
    const auto before=slot[4];const auto after=static_cast<unsigned char>(!before);
    std::vector<unsigned char> state=wire;state.resize(CrossplayReady::NativeStateSize);
    CrossplayChat::Put(state.data()+13,2);state[17]=after;
    if(!ReadyDetail::EnqueueState(edition,state,ids.guest))return false;
    pending={reinterpret_cast<uintptr_t>(config),session,ids,before,after};return true;
}
inline ReadyObservation ObserveReadyToggle(const CrossplayNative::ParticipantEvent& binding,const PendingReadyToggle& pending) {
    if(!pending.configuration || !SessionConfigDetail::Roster(binding,pending.ids,true))return ReadyObservation::Invalid;
    auto* config=SessionConfigDetail::GoldConfiguration();if(reinterpret_cast<uintptr_t>(config)!=pending.configuration)return ReadyObservation::Invalid;
    SessionConfigDetail::ConfigLock lock(config);if(!lock.acquired)return ReadyObservation::Pending;
    unsigned char* current=nullptr;unsigned char* slot=nullptr;uintptr_t session=0;
    if(!GoldReadySlot(binding,pending.ids,current,slot,session) || current!=config || session!=pending.session)return ReadyObservation::Invalid;
    return slot[4]==pending.after?ReadyObservation::Applied:slot[4]==pending.before?ReadyObservation::Pending:ReadyObservation::Invalid;
}
}
