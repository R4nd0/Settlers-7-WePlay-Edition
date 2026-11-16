#pragma once
#include "crossplay_neutral_setup.h"
#include "crossplay_player_option_codec.h"
#include "crossplay_session_map_observer.h"
// Native factory/reader/clone delivery only. A controller must separately bind
// its owner/deadline, original config consumer and unchanged actual map/setup.
namespace CrossplayNeutralSetup {
inline bool Context(Crossplay::Edition edition,const CrossplayNative::ParticipantEvent& binding,CrossplaySessionConfig::Connections ids,
                    bool host,const std::string& expected_map){
    using namespace CrossplayNativeCodec;
    if(!CrossplaySessionConfig::RelativeMap(expected_map) || !SessionConfigDetail::Roster(binding,ids,host))return false;
    auto* cache=edition==Crossplay::Edition::Gold?SessionConfigDetail::GoldConfiguration():ReadyStateDetail::HistoryConfiguration(edition);
    std::string map;const auto p=Detail::Pin(edition);
    return cache && SessionConfigDetail::Fields(p,cache+0x24,cache+0x34,cache+0x44,ids)
        && SessionConfigDetail::NativeString(cache+0x24,map) && map==expected_map;
}
inline bool Capture(const CrossplayNative::ParticipantEvent& binding,CrossplaySessionConfig::Connections ids,const std::string& expected_map,const void* event,std::vector<unsigned char>& wire) {
    using namespace CrossplayNativeCodec;wire.clear();PlayerOptionDetail::Spec spec{};
    if(!Context(Crossplay::Edition::Gold,binding,ids,true,expected_map)
        || !PlayerOptionDetail::Layout(Crossplay::Edition::Gold,CrossplayPlayerOption::Team,spec)
        || !PlayerOptionDetail::Methods(Crossplay::Edition::Gold,spec) || !Detail::Readable(event,24))return false;
    const auto* p=static_cast<const unsigned char*>(event);const uint32_t slot=CrossplayChat::Word(p+16);
    if(CrossplayChat::Word(p)!=spec.vt || CrossplayChat::Word(p+4)!=spec.type || CrossplayChat::Word(p+8)!=ids.host
        || p[12]!=1 || slot<3 || slot>4 || p[20]<1 || p[20]>4)return false;
    auto profile=Detail::Pin(Crossplay::Edition::Gold);profile.event_vt=spec.vt;uint32_t parsed=0,value=0;
    if(Detail::Encode(profile,event,wire) && Wire(wire,ids.host,parsed,value) && parsed==slot && value==p[20])return true;
    wire.clear();return false;
}
inline CrossplayNativeCodec::ReadyObservation ObserveClosed(const CrossplayNative::ParticipantEvent& binding,
    const CrossplayNativeCodec::PendingHistorySessionMap& pending,const std::vector<unsigned char>& expected,const std::string& expected_map) {
    using namespace CrossplayNativeCodec;
    if(pending.expected_map!=expected_map)return ReadyObservation::Invalid;
    const auto observed=ObserveHistorySessionMap(binding,pending);if(observed!=ReadyObservation::Applied)return observed;
    auto* cache=ReadyStateDetail::HistoryConfiguration(pending.edition);const auto p=Detail::Pin(pending.edition);
    if(!cache || reinterpret_cast<uintptr_t>(cache)!=pending.cache || !SessionConfigDetail::Fields(p,cache+0x24,cache+0x34,cache+0x44,pending.ids))return ReadyObservation::Invalid;
    const auto* rows=*reinterpret_cast<const unsigned char* const*>(cache+0x44);
    return MatchesSlots(expected,pending.ids,expected_map,rows,8*0x18)?ReadyObservation::Applied:ReadyObservation::Pending;
}
inline bool TeamValue(Crossplay::Edition edition,const CrossplayNative::ParticipantEvent& binding,CrossplaySessionConfig::Connections ids,const std::string& expected_map,
    uint32_t slot,uint32_t& value,uintptr_t& session,uintptr_t& preparation) {
    using namespace CrossplayNativeCodec;value=0;session=preparation=0;
    CrossplayNative::LobbySnapshot snapshot;unsigned char* rows=nullptr;
    if(slot<3 || slot>4 || !Context(edition,binding,ids,false,expected_map)
        || !ReadyStateDetail::Preparation(edition,binding,ids,snapshot,rows) || !Detail::Readable(rows,0x8dc0) || rows[0x8db8]!=4)return false;
    auto* cache=ReadyStateDetail::HistoryConfiguration(edition);if(!cache)return false;
    const auto* slots=*reinterpret_cast<const unsigned char* const*>(cache+0x44);const auto* closed=slots+(slot-1)*0x18;
    if(!Detail::Readable(closed,24) || CrossplayChat::Word(closed)!=2 || closed[4]!=1
        || CrossplayChat::Word(closed+8)!=slot || CrossplayChat::Word(closed+16))return false;
    value=rows[slot*0x5a8+0x68];session=snapshot.instance;preparation=reinterpret_cast<uintptr_t>(rows);
    return value>=1 && value<=4;
}
inline bool Deliver(Crossplay::Edition edition,const CrossplayNative::ParticipantEvent& binding,CrossplaySessionConfig::Connections ids,const std::string& expected_map,const std::vector<unsigned char>& wire) {
    using namespace CrossplayNativeCodec;uint32_t slot=0,value=0,old=0;uintptr_t session=0,rows=0;
    PlayerOptionDetail::Spec spec{};
    if(!Wire(wire,ids.host,slot,value) || !TeamValue(edition,binding,ids,expected_map,slot,old,session,rows)
        || !PlayerOptionDetail::Layout(edition,CrossplayPlayerOption::Team,spec) || !PlayerOptionDetail::Methods(edition,spec))return false;
    auto p=Detail::Pin(edition);p.event_vt=spec.vt;Detail::OwnedStream stream(p);if(!stream.initialized)return false;
    stream.value.bytes=const_cast<unsigned char*>(wire.data());stream.value.capacity=stream.value.length=static_cast<uint32_t>(wire.size());stream.value.external=1;
    Detail::Visitor reader{p.reader_vt,1,&stream.value,0};
    void* decoded=reinterpret_cast<void* (__thiscall*)(void*,const char*,uint32_t)>(p.read)(&reader,"WePlayEvent",0);
    if(!decoded || !Detail::Readable(decoded,24) || *static_cast<const uintptr_t*>(decoded)!=p.event_vt)return false;
    std::vector<unsigned char> repeated;uintptr_t current_session=0,current_rows=0;
    bool okay=stream.value.position==wire.size() && Detail::Encode(p,decoded,repeated) && repeated==wire
        && TeamValue(edition,binding,ids,expected_map,slot,old,current_session,current_rows) && current_session==session && current_rows==rows;
    uintptr_t sender=0;void* transport=okay?Detail::Transport(edition,sender):nullptr;
    if(transport)reinterpret_cast<void (__thiscall*)(void*,const void*,uint32_t,uint32_t,uint32_t,uint32_t)>(sender)(transport,decoded,0,0,0,0);
    else okay=false;
    reinterpret_cast<void (__thiscall*)(void*,uint32_t)>(*reinterpret_cast<const uintptr_t*>(p.event_vt))(decoded,1);return okay;
}
}
