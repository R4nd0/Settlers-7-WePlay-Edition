#pragma once
#include "crossplay_closed_slot_codec.h"
#include "crossplay_configuration_native.h"
#include "crossplay_neutral_setup.h"

// A complete original configuration consumer can already install all Closed
// cache slots. That is a separate, stronger context than a sequential Open->
// Closed transition. Keep the original sequential API unchanged.
namespace CrossplayClosedConfigReceipt {
inline bool Context(Crossplay::Edition edition,const CrossplayNative::ParticipantEvent& binding,
                    CrossplaySessionConfig::Connections ids,uint32_t slot,const CrossplayConfigurationReceipt::Interval& interval,
                    const CrossplayConfigurationReceipt::Receipt& receipt,uintptr_t& session,uintptr_t& cache){
    session=cache=0;if(slot<3 || slot>4 || !receipt.ConsumedIn(interval,GetTickCount64()))return false;
    CrossplaySessionConfig::Layout layout;std::vector<unsigned char> actual;
    if(!CrossplaySessionConfig::Inspect(receipt.Expected(),ids,layout)
        || !CrossplayNeutralSetup::Closed(receipt.Expected(),ids,layout.map,layout)
        || !CrossplayConfigurationNative::Snapshot(edition,binding,ids,interval,actual) || actual!=receipt.Expected())return false;
    session=interval.session;cache=interval.cache;return true;
}
inline bool Deliver(Crossplay::Edition edition,const CrossplayNative::ParticipantEvent& binding,CrossplaySessionConfig::Connections ids,
                    const std::vector<unsigned char>& wire,const CrossplayConfigurationReceipt::Interval& interval,
                    const CrossplayConfigurationReceipt::Receipt& receipt,uintptr_t& session,uintptr_t& cache){
    using namespace CrossplayNativeCodec;uint32_t slot=0;
    if(!CrossplayClosedSlot::Wire(wire,ids.host,slot) || !CrossplayClosedSlot::Methods(edition)
        || !Context(edition,binding,ids,slot,interval,receipt,session,cache))return false;
    auto p=Detail::Pin(edition);p.event_vt=CrossplayClosedSlot::Vtable(edition);Detail::OwnedStream stream(p);if(!stream.initialized)return false;
    stream.value.bytes=const_cast<unsigned char*>(wire.data());stream.value.capacity=stream.value.length=static_cast<uint32_t>(wire.size());stream.value.external=1;
    Detail::Visitor reader{p.reader_vt,1,&stream.value,0};
    void* decoded=p.gold?Detail::GoldRead(p.read,&reader,"WePlayEvent"):
        reinterpret_cast<void* (__thiscall*)(void*,const char*,uint32_t)>(p.read)(&reader,"WePlayEvent",0);
    if(!decoded || !Detail::Readable(decoded,20) || *static_cast<const uintptr_t*>(decoded)!=p.event_vt)return false;
    std::vector<unsigned char> repeated;uintptr_t s=0,c=0;
    bool okay=stream.value.position==wire.size() && Detail::Encode(p,decoded,repeated) && repeated==wire
        && Context(edition,binding,ids,slot,interval,receipt,s,c) && s==session && c==cache;
    uintptr_t sender=0;void* transport=okay?Detail::Transport(edition,sender):nullptr;
    if(transport)reinterpret_cast<void (__thiscall*)(void*,const void*,uint32_t,uint32_t,uint32_t,uint32_t)>(sender)(transport,decoded,0,0,0,0);
    else okay=false;
    reinterpret_cast<void (__thiscall*)(void*,uint32_t)>(*static_cast<const uintptr_t*>(reinterpret_cast<const void*>(p.event_vt)))(decoded,1);
    return okay;
}
}
