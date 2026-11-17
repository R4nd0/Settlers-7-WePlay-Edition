#pragma once
#include "crossplay_history_host_choices.h"

// Read the original Steam host cache through its original configuration event
// constructor and serializer. This grants no guest/Ready/start/save authority.
namespace CrossplayHistoryHostConfiguration {
// The original caller reserves an unused first stack word, followed by source,
// map, save and slots. ECX owns the output object; the callee removes 20 bytes.
inline void* ConstructWith(uintptr_t address,void* output,uint32_t source,
                           const void* map,const void* save,const void* slots){
    using Construct=void* (__thiscall*)(void*,uint32_t,uint32_t,const void*,const void*,const void*);
    return reinterpret_cast<Construct>(address)(output,0,source,map,save,slots);
}
struct OwnedEvent {
    alignas(4) unsigned char bytes[0x3c]{};bool initialized=false;
    ~OwnedEvent(){
        if(initialized)reinterpret_cast<void (__thiscall*)(void*,uint32_t)>(
            *reinterpret_cast<const uintptr_t*>(0x114c238))(bytes,0);
    }
};
inline bool Capture(const CrossplayNative::ParticipantEvent& binding,
                    CrossplaySessionConfig::Connections ids,std::vector<unsigned char>& wire){
    struct ErrorScope {DWORD value=GetLastError();~ErrorScope(){SetLastError(value);}} error;
    CrossplayNativeCodec::LoadProgressDetail::FpScope fp;wire.clear();
    using namespace CrossplayNativeCodec;constexpr auto edition=Crossplay::Edition::SteamHistory;
    if(!SessionConfigDetail::Roster(binding,ids,true) || !SessionConfigDetail::Methods(edition))return false;
    auto* cache=ReadyStateDetail::HistoryConfiguration(edition);if(!cache)return false;
    CrossplayAIProfile::CacheLock lock(edition,cache);if(!lock.acquired)return false;
    const auto pin=SessionConfigDetail::Pin(edition);
    if(!SessionConfigDetail::Fields(pin,cache+0x24,cache+0x34,cache+0x44,ids))return false;
    OwnedEvent event;
    auto* result=ConstructWith(0x83fd90,event.bytes,ids.host,cache+0x24,cache+0x34,cache+0x44);
    event.initialized=*reinterpret_cast<const uintptr_t*>(event.bytes)==pin.event_vt;
    if(!event.initialized || result!=event.bytes || !SessionConfigDetail::Source(pin,event.bytes,ids)
        || *reinterpret_cast<const uintptr_t*>(event.bytes+0x10)==*reinterpret_cast<const uintptr_t*>(cache+0x24)
        || *reinterpret_cast<const uintptr_t*>(event.bytes+0x30)==*reinterpret_cast<const uintptr_t*>(cache+0x44)
        || ReadyStateDetail::HistoryConfiguration(edition)!=cache || !SessionConfigDetail::Roster(binding,ids,true))return false;
    std::vector<unsigned char> original;
    return Detail::Encode(pin,event.bytes,original) && CrossplaySessionConfig::BindSource(original,ids,wire);
}
}
