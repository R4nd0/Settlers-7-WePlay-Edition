#pragma once
#include "crossplay_session_config.h"
#include "crossplay_ready_state.h"

// A comparison policy, not Ready authority. Its caller must first observe the
// exact original configuration consumer in the same owned native interval.
namespace CrossplayConfigurationReadiness {
enum class Difference { Invalid, Equal, HostOnly };
struct RoomSeatReady {unsigned char player_slot=0, value=0;};
inline Difference Compare(const std::vector<unsigned char>& expected,const std::vector<unsigned char>& actual,
                          CrossplaySessionConfig::Connections ids,CrossplayReadyState::Snapshot& observed){
    observed={};CrossplaySessionConfig::Layout source,cache;
    if(!CrossplaySessionConfig::Inspect(expected,ids,source) || !CrossplaySessionConfig::Inspect(actual,ids,cache)
        || source.map!=cache.map || source.slots_at!=cache.slots_at || expected.size()!=actual.size())return Difference::Invalid;
    // Every byte other than the observed host's Ready must already match.
    // In particular, the host snapshot cannot change this guest's own Ready.
    for(size_t i=0;i<expected.size();++i)
        if(i!=source.slots_at && expected[i]!=actual[i])return Difference::Invalid;
    if(expected[source.slots_at]==actual[cache.slots_at])return Difference::Equal;
    // One initial projection per fresh configuration transaction. This revision
    // is local bookkeeping; it is not a network Ready command or start token.
    observed={1,expected[source.slots_at],actual[cache.slots_at+CrossplaySessionConfig::SlotBytes]};
    return Difference::HostOnly;
}
inline Difference CompareRoom(const std::vector<unsigned char>& expected,const std::vector<unsigned char>& actual,
                              const CrossplaySessionConfig::RoomConnections& ids,RoomSeatReady& observed) {
    observed={};
    CrossplaySessionConfig::Layout source,cache;
    if(expected.size()!=actual.size() || !CrossplaySessionConfig::InspectRoom(expected,ids,source)
        || !CrossplaySessionConfig::InspectRoom(actual,ids,cache) || source.slots_at!=cache.slots_at)return Difference::Invalid;
    const auto host=static_cast<size_t>(std::find(ids.stations.begin(),ids.stations.end(),ids.host)-ids.stations.begin());
    const size_t ready_at=source.slots_at+host*CrossplaySessionConfig::SlotBytes;
    for(size_t i=0;i<expected.size();++i)
        if(i!=ready_at && expected[i]!=actual[i])return Difference::Invalid;
    if(expected[ready_at]==actual[ready_at])return Difference::Equal;
    observed={static_cast<unsigned char>(host+1),expected[ready_at]};
    return Difference::HostOnly;
}
inline bool RoomReadyChange(const std::vector<unsigned char>& before,const std::vector<unsigned char>& after,
                            const CrossplaySessionConfig::RoomConnections& ids,RoomSeatReady& change) {
    change={};
    CrossplaySessionConfig::Layout previous,current;
    if(before.size()!=after.size() || !CrossplaySessionConfig::InspectRoom(before,ids,previous)
        || !CrossplaySessionConfig::InspectRoom(after,ids,current) || previous.slots_at!=current.slots_at)return false;
    RoomSeatReady changed;
    for(size_t i=0;i<before.size();++i){
        if(before[i]==after[i])continue;
        if(changed.player_slot || i<previous.slots_at || (i-previous.slots_at)%CrossplaySessionConfig::SlotBytes)return false;
        const auto slot=(i-previous.slots_at)/CrossplaySessionConfig::SlotBytes;
        if(slot>=ids.capacity || ids.stations[slot]==UINT32_MAX)return false;
        changed={static_cast<unsigned char>(slot+1),after[i]};
    }
    if(!changed.player_slot)return false;
    change=changed;return true;
}
}
