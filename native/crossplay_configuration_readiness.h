#pragma once
#include "crossplay_session_config.h"
#include "crossplay_ready_state.h"

// A comparison policy, not Ready authority. Its caller must first observe the
// exact original configuration consumer in the same owned native interval.
namespace CrossplayConfigurationReadiness {
enum class Difference { Invalid, Equal, HostOnly };
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
}
