#pragma once
#include "crossplay_ai_profile_codec.h"
#include "crossplay_configuration_receipt.h"
#include "crossplay_bootstrap.h"
#include "crossplay_load_progress_codec.h"

// Read actual History session choices after native ownership is established.
// This neither applies bootstrap defaults nor grants setup/world authority.
namespace CrossplayHistoryChoices {
enum class Observation { Pending,Invalid,Captured };
struct Pin {uintptr_t create=0,backend=0,handler=0,descriptor=0,property=0;uint64_t hash=0;};
inline bool Layout(Crossplay::Edition edition,Pin& pin){
    pin={};
    if(edition==Crossplay::Edition::UbisoftHistory)
        pin={0x842c50,0x1379e14,0x113fe90,0x1140150,0x1086d3c,0xca3ed694819f113dull};
    else if(edition==Crossplay::Edition::SteamHistory)
        pin={0x842a20,0x1376f04,0x113e3ac,0x113e678,0x1084d6c,0x29f199830dc2bdf9ull};
    else return false;
    return true;
}
inline bool Methods(Crossplay::Edition edition){
    Pin pin;if(!Layout(edition,pin) || !CrossplayNativeCodec::SessionConfigDetail::Methods(edition)
        || !CrossplayNativeCodec::Detail::Readable(reinterpret_cast<const void*>(pin.create),0x4ec))return false;
    uint64_t hash=14695981039346656037ull;
    for(size_t i=0;i<0x4ec;++i)hash=(hash^reinterpret_cast<const unsigned char*>(pin.create)[i])*1099511628211ull;
    return hash==pin.hash;
}
inline bool Flags(unsigned positions,unsigned teams,unsigned coop,uint32_t& flags){
    flags=0;if(positions>1 || teams>1 || coop>1)return false;
    flags=positions|(teams<<1)|(coop<<2);return true;
}
struct SmallString {unsigned char storage[16];uint32_t length,capacity;};
static_assert(sizeof(SmallString)==24,"Original History template string layout");
inline bool Template(const SmallString& text,std::string& value){
    value.clear();
    if(text.length>CrossplayBootstrap::MaximumTemplate || text.capacity<15 || text.capacity<text.length
        || text.capacity>0x100000 || (text.capacity<16 && text.length>=16))return false;
    const auto* data=text.capacity<16?reinterpret_cast<const char*>(text.storage):*reinterpret_cast<const char* const*>(text.storage);
    if(!CrossplayNativeCodec::Detail::Readable(data,text.length+1) || data[text.length] || memchr(data,0,text.length))return false;
    std::string copied(data,text.length);if(!CrossplayBootstrap::Template(copied))return false;
    value=std::move(copied);return true;
}
struct Value {
    uintptr_t cache=0,backend=0,handler=0,implementation=0,descriptor=0;
    std::string map,map_template;uint32_t flags=0;
};
inline bool Same(const Value& a,const Value& b){
    return a.cache && a.cache==b.cache && a.backend && a.backend==b.backend && a.handler && a.handler==b.handler
        && a.implementation && a.implementation==b.implementation && a.descriptor && a.descriptor==b.descriptor
        && a.map==b.map && a.map_template==b.map_template && a.flags==b.flags;
}
inline Observation SnapshotCache(Crossplay::Edition edition,const Pin& pin,CrossplaySessionConfig::Connections ids,bool host,
                            CrossplayConfigurationReceipt::Interval interval,const std::string& expected_save,Value& output){
    using namespace CrossplayNativeCodec;output={};
    if(!expected_save.empty() && expected_save!="Savegames/MPSave")return Observation::Invalid;
    auto* cache=ReadyStateDetail::HistoryConfiguration(edition);
    if(!cache || reinterpret_cast<uintptr_t>(cache)!=interval.cache || !Detail::Readable(cache,0x54))return Observation::Invalid;
    CrossplayAIProfile::CacheLock lock(edition,cache);if(!lock.acquired)return Observation::Pending;
    Value next;next.cache=reinterpret_cast<uintptr_t>(cache);std::string save;
    if(!SessionConfigDetail::NativeString(cache+0x24,next.map) || !CrossplaySessionConfig::RelativeMap(next.map)
        || !SessionConfigDetail::NativeString(cache+0x34,save) || save!=expected_save
        || !Flags(cache[0x50],cache[0x51],cache[0x52],next.flags)
        || !Detail::Readable(reinterpret_cast<const void*>(pin.backend),4))return Observation::Invalid;
    const auto* backend=*reinterpret_cast<const unsigned char* const*>(pin.backend);
    if(!Detail::Readable(backend,0xa50))return Observation::Invalid;
    const auto* handler=*reinterpret_cast<const unsigned char* const*>(backend+0xa4c);
    if(!Detail::Readable(handler,0x28) || CrossplayChat::Word(handler)!=pin.handler
        || CrossplayChat::Word(handler+0x20)!=(host?ids.host:ids.guest)
        || CrossplayChat::Word(handler+0x24)!=ids.host)return Observation::Invalid;
    const auto* implementation=*reinterpret_cast<const unsigned char* const*>(handler+0x1c);
    if(!Detail::Readable(implementation,0x14))return Observation::Invalid;
    const auto* descriptor=*reinterpret_cast<const unsigned char* const*>(implementation+0x10);
    if(!Detail::Readable(descriptor,0x210) || CrossplayChat::Word(descriptor)!=pin.descriptor
        || CrossplayChat::Word(descriptor+0x1ec)!=pin.property)return Observation::Invalid;
    const auto text=*reinterpret_cast<const SmallString*>(descriptor+0x1f8);
    if(!Template(text,next.map_template) || memcmp(descriptor+0x1f8,&text,sizeof(text)))return Observation::Invalid;
    next.backend=reinterpret_cast<uintptr_t>(backend);next.handler=reinterpret_cast<uintptr_t>(handler);
    next.implementation=reinterpret_cast<uintptr_t>(implementation);next.descriptor=reinterpret_cast<uintptr_t>(descriptor);
    if(*reinterpret_cast<const unsigned char* const*>(pin.backend)!=backend
        || *reinterpret_cast<const unsigned char* const*>(backend+0xa4c)!=handler
        || *reinterpret_cast<const unsigned char* const*>(handler+0x1c)!=implementation
        || *reinterpret_cast<const unsigned char* const*>(implementation+0x10)!=descriptor
        || ReadyStateDetail::HistoryConfiguration(edition)!=cache)return Observation::Invalid;
    output=std::move(next);return Observation::Captured;
}
inline Observation Snapshot(Crossplay::Edition edition,const Pin& pin,CrossplaySessionConfig::Connections ids,bool host,
                            CrossplayConfigurationReceipt::Interval interval,Value& output){
    return SnapshotCache(edition,pin,ids,host,interval,"",output);
}
inline Observation Capture(Crossplay::Edition edition,const CrossplayNative::ParticipantEvent& binding,
                           CrossplaySessionConfig::Connections ids,bool host,
                           CrossplayConfigurationReceipt::Interval interval,Value& output){
    struct ErrorScope {DWORD value=GetLastError();~ErrorScope(){SetLastError(value);}} error;
    CrossplayNativeCodec::LoadProgressDetail::FpScope fp;output={};Pin pin;
    CrossplayNative::LobbySnapshot before,after;
    if(interval.owner!=GetCurrentThreadId() || !interval.session || !interval.cache || !Layout(edition,pin) || !Methods(edition)
        || !CrossplayNativeCodec::SessionConfigDetail::Roster(binding,ids,host)
        || !binding.CaptureLobby(before) || before.instance!=interval.session)return Observation::Invalid;
    Value first,second;auto result=Snapshot(edition,pin,ids,host,interval,first);if(result!=Observation::Captured)return result;
    result=Snapshot(edition,pin,ids,host,interval,second);if(result!=Observation::Captured)return result;
    if(!binding.CaptureLobby(after) || after.instance!=before.instance
        || !CrossplayNativeCodec::SessionConfigDetail::Roster(binding,ids,host) || !Methods(edition))return Observation::Invalid;
    if(!Same(first,second))return Observation::Pending;
    output=std::move(second);return Observation::Captured;
}
// Separate saved UI view. Callers have already verified the complete native
// resident stream against their selected checkpoint. Require that same stream
// identity before/after reading the actual original flags/template; do not
// advertise bootstrap values as applied guest state or relax the fresh view.
inline bool SavedStream(Crossplay::Edition edition,CrossplayConfigurationReceipt::Interval interval,
                        uintptr_t stream_object,uintptr_t data,uint32_t bytes){
    using namespace CrossplayNativeCodec;
    if(interval.owner!=GetCurrentThreadId() || !interval.session || !interval.cache
        || !stream_object || !data || bytes<80 || bytes>64u*1024u*1024u)return false;
    auto* cache=ReadyStateDetail::HistoryConfiguration(edition);
    const auto* stream=reinterpret_cast<const unsigned char*>(stream_object);
    return cache && reinterpret_cast<uintptr_t>(cache)==interval.cache && Detail::Readable(cache,0x54)
        && reinterpret_cast<uintptr_t>(*reinterpret_cast<const unsigned char* const*>(cache+0x1c))==stream_object
        && Detail::Readable(stream,0x28) && CrossplayChat::Word(stream)==Detail::Pin(edition).stream_vt
        && CrossplayChat::Word(stream+4)==data && CrossplayChat::Word(stream+0xc)==bytes
        && Detail::Readable(reinterpret_cast<const void*>(data),bytes);
}
inline Observation CaptureSaved(Crossplay::Edition edition,const CrossplayNative::ParticipantEvent& binding,
                                 CrossplaySessionConfig::Connections ids,CrossplayConfigurationReceipt::Interval interval,
                                 uintptr_t stream,uintptr_t data,uint32_t bytes,Value& output){
    struct ErrorScope {DWORD value=GetLastError();~ErrorScope(){SetLastError(value);}} error;
    CrossplayNativeCodec::LoadProgressDetail::FpScope fp;output={};Pin pin;
    CrossplayNative::LobbySnapshot before,after;
    if(interval.owner!=GetCurrentThreadId() || !interval.session || !interval.cache || !Layout(edition,pin) || !Methods(edition)
        || !CrossplayNativeCodec::SessionConfigDetail::Roster(binding,ids,false)
        || !binding.CaptureLobby(before) || before.instance!=interval.session || !SavedStream(edition,interval,stream,data,bytes))return Observation::Invalid;
    Value first,second;
    auto result=SnapshotCache(edition,pin,ids,false,interval,"Savegames/MPSave",first);if(result!=Observation::Captured)return result;
    result=SnapshotCache(edition,pin,ids,false,interval,"Savegames/MPSave",second);if(result!=Observation::Captured)return result;
    if(!binding.CaptureLobby(after) || after.instance!=before.instance || !SavedStream(edition,interval,stream,data,bytes)
        || !CrossplayNativeCodec::SessionConfigDetail::Roster(binding,ids,false) || !Methods(edition))return Observation::Invalid;
    if(!Same(first,second))return Observation::Pending;output=std::move(second);return Observation::Captured;
}
}
