#pragma once
#include "crossplay_ready_state_codec.h"

// An enqueued configuration clone is not an applied configuration. The Lua
// guest initializer must wait for this read-only map postcondition before it
// captures preparation rows or calls MapSetup.Load. This does not establish
// template/rules agreement, complete event consumption, or permission to start.
namespace CrossplayNativeCodec {
struct PendingHistorySessionMap {
    Crossplay::Edition edition{};
    CrossplaySessionConfig::Connections ids{};
    uintptr_t session=0,cache=0;
    DWORD owner=0;
    ULONGLONG started=0;
    std::string expected_map;
};
inline bool EnqueueHistorySessionMap(Crossplay::Edition edition,
                                    const CrossplayNative::ParticipantEvent& binding,
                                    CrossplaySessionConfig::Connections ids,
                                    const std::vector<unsigned char>& wire,
                                    PendingHistorySessionMap& pending) {
    pending={};
    if(edition!=Crossplay::Edition::UbisoftHistory && edition!=Crossplay::Edition::SteamHistory)return false;
    CrossplaySessionConfig::Layout layout;CrossplayNative::LobbySnapshot snapshot;
    auto* cache=ReadyStateDetail::HistoryConfiguration(edition);
    if(!cache || !binding.CaptureLobby(snapshot) || !snapshot.instance
        || !CrossplaySessionConfig::Inspect(wire,ids,layout)
        || !EnqueueSessionConfig(edition,binding,ids,wire))return false;
    pending={edition,ids,snapshot.instance,reinterpret_cast<uintptr_t>(cache),
             GetCurrentThreadId(),GetTickCount64(),std::move(layout.map)};
    return true;
}
inline ReadyObservation ObserveHistorySessionMap(const CrossplayNative::ParticipantEvent& binding,
                                                 const PendingHistorySessionMap& pending) {
    if(!pending.session || !pending.cache || pending.owner!=GetCurrentThreadId()
        || (pending.edition!=Crossplay::Edition::UbisoftHistory && pending.edition!=Crossplay::Edition::SteamHistory)
        || !CrossplaySessionConfig::RelativeMap(pending.expected_map)
        || GetTickCount64()-pending.started>10000)return ReadyObservation::Invalid;
    CrossplayNative::LobbySnapshot snapshot;
    auto* cache=ReadyStateDetail::HistoryConfiguration(pending.edition);std::string current;
    if(!SessionConfigDetail::Roster(binding,pending.ids,false)
        || !binding.CaptureLobby(snapshot) || snapshot.instance!=pending.session
        || !cache || reinterpret_cast<uintptr_t>(cache)!=pending.cache
        || !SessionConfigDetail::NativeString(cache+0x24,current)
        || !CrossplaySessionConfig::RelativeMap(current))return ReadyObservation::Invalid;
    return current==pending.expected_map?ReadyObservation::Applied:ReadyObservation::Pending;
}
}
