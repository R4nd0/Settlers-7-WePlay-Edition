#pragma once
#include "crossplay_history_choices.h"
#include "crossplay_resume_start.h"

// Read-only reverse-path foundation. This is not launcher admission, a Gold
// guest implementation, file authority, Ready acknowledgement or start request.
// Ordinary mixed-role parsing remains restricted to the qualified forward path.
namespace CrossplayHistoryHostChoices {
using Observation=CrossplayHistoryChoices::Observation;
struct Value {CrossplayHistoryChoices::Value choices;std::string save;};
inline bool Same(const Value& a,const Value& b){
    return CrossplayHistoryChoices::Same(a.choices,b.choices) && a.save==b.save;
}
inline bool Local(const CrossplayNative::LobbySnapshot& snapshot,uintptr_t session,
                  const CrossplayNative::Participant& expected){
    if(!session || snapshot.instance!=session || !snapshot.host || snapshot.local_slot!=1
        || snapshot.members.size()!=1 || expected.connection==UINT32_MAX || expected.principal==UINT32_MAX
        || expected.station==UINT32_MAX || expected.name.empty() || expected.name.size()>64)return false;
    const auto& member=snapshot.members[0];const auto& actual=member.player;
    return member.status==2 && actual.local && actual.host && actual.slot==1
        && actual.connection==expected.connection && actual.principal==expected.principal
        && actual.station==expected.station && actual.name==expected.name;
}
inline bool SaveToken(const std::string& save){
    // A selected host slot is distinct from the receiver's Savegames/MPSave
    // token. Even an observed valid slot still needs an independently admitted
    // archive from this host's actual owned profile before any file is sent.
    return save.empty() || CrossplayResumeStart::SlotName(save);
}
inline Observation Snapshot(Crossplay::Edition edition,uint32_t local,Value& output){
    using namespace CrossplayNativeCodec;
    output={};CrossplayHistoryChoices::Pin pin;
    if(edition!=Crossplay::Edition::SteamHistory || local==UINT32_MAX
        || !CrossplayHistoryChoices::Layout(edition,pin))return Observation::Invalid;
    auto* cache=ReadyStateDetail::HistoryConfiguration(edition);if(!cache)return Observation::Pending;
    if(!Detail::Readable(cache,0x54))return Observation::Invalid;
    CrossplayAIProfile::CacheLock lock(edition,cache);if(!lock.acquired)return Observation::Pending;
    Value next;next.choices.cache=reinterpret_cast<uintptr_t>(cache);
    auto& value=next.choices;
    if(!SessionConfigDetail::NativeString(cache+0x24,value.map)
        || !SessionConfigDetail::NativeString(cache+0x34,next.save) || !SaveToken(next.save)
        || !CrossplayHistoryChoices::Flags(cache[0x50],cache[0x51],cache[0x52],value.flags))return Observation::Invalid;
    if(value.map.empty())return Observation::Pending;
    if(!CrossplaySessionConfig::RelativeMap(value.map) || !CrossplayBootstrap::Printable(value.map)
        || !Detail::Readable(reinterpret_cast<const void*>(pin.backend),4))return Observation::Invalid;
    const auto* backend=*reinterpret_cast<const unsigned char* const*>(pin.backend);
    if(!Detail::Readable(backend,0xa50))return Observation::Invalid;
    const auto* handler=*reinterpret_cast<const unsigned char* const*>(backend+0xa4c);
    if(!Detail::Readable(handler,0x28) || CrossplayChat::Word(handler)!=pin.handler
        || CrossplayChat::Word(handler+0x20)!=local || CrossplayChat::Word(handler+0x24)!=local)return Observation::Invalid;
    const auto* implementation=*reinterpret_cast<const unsigned char* const*>(handler+0x1c);
    if(!Detail::Readable(implementation,0x14))return Observation::Invalid;
    const auto* descriptor=*reinterpret_cast<const unsigned char* const*>(implementation+0x10);
    if(!Detail::Readable(descriptor,0x210) || CrossplayChat::Word(descriptor)!=pin.descriptor
        || CrossplayChat::Word(descriptor+0x1ec)!=pin.property)return Observation::Invalid;
    const auto text=*reinterpret_cast<const CrossplayHistoryChoices::SmallString*>(descriptor+0x1f8);
    if(!CrossplayHistoryChoices::Template(text,value.map_template)
        || memcmp(descriptor+0x1f8,&text,sizeof(text)))return Observation::Invalid;
    value.backend=reinterpret_cast<uintptr_t>(backend);value.handler=reinterpret_cast<uintptr_t>(handler);
    value.implementation=reinterpret_cast<uintptr_t>(implementation);value.descriptor=reinterpret_cast<uintptr_t>(descriptor);
    if(*reinterpret_cast<const unsigned char* const*>(pin.backend)!=backend
        || *reinterpret_cast<const unsigned char* const*>(backend+0xa4c)!=handler
        || CrossplayChat::Word(handler+0x20)!=local || CrossplayChat::Word(handler+0x24)!=local
        || *reinterpret_cast<const unsigned char* const*>(handler+0x1c)!=implementation
        || *reinterpret_cast<const unsigned char* const*>(implementation+0x10)!=descriptor
        || ReadyStateDetail::HistoryConfiguration(edition)!=cache)return Observation::Invalid;
    output=std::move(next);return Observation::Captured;
}
inline Observation Capture(Crossplay::Edition edition,const CrossplayNative::ParticipantEvent& binding,
                           DWORD owner,uintptr_t session,const CrossplayNative::Participant& expected,
                           CrossplayBootstrap::Context& output){
    struct ErrorScope {DWORD value=GetLastError();~ErrorScope(){SetLastError(value);}} error;
    CrossplayNativeCodec::LoadProgressDetail::FpScope fp;output={};
    CrossplayNative::LobbySnapshot before,after;
    if(edition!=Crossplay::Edition::SteamHistory || !owner || owner!=GetCurrentThreadId()
        || !CrossplayHistoryChoices::Methods(edition) || !binding.CaptureLobby(before)
        || !Local(before,session,expected))return Observation::Invalid;
    Value first,second;
    auto state=Snapshot(edition,expected.connection,first);if(state!=Observation::Captured)return state;
    state=Snapshot(edition,expected.connection,second);if(state!=Observation::Captured)return state;
    if(!binding.CaptureLobby(after) || !Local(after,session,expected)
        || !CrossplayHistoryChoices::Methods(edition))return Observation::Invalid;
    if(!Same(first,second))return Observation::Pending;
    CrossplayBootstrap::Context result{first.choices.map,!first.save.empty(),first.choices.flags,first.choices.map_template,true};
    if(!CrossplayBootstrap::Valid(result))return Observation::Invalid;
    output=std::move(result);return Observation::Captured;
}
}
