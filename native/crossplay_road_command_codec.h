#pragma once
#include "crossplay_build_command_codec.h"
#include "crossplay_road_command.h"

// Opt-in original normal-street command only. Caller retains the guarded
// authenticated owner/session and FIFO order relative to other commands/EOT.
namespace CrossplayNativeCodec::RoadCommandDetail {
using Region=BuildCommandDetail::Region;
struct RoadProfile {uintptr_t table;Region regions[9];};
inline RoadProfile RoadPin(Crossplay::Edition edition) {
    static const RoadProfile pins[]={
        {0xf373f0,{
            {0xf373f0,24,0xe4a30f537100e816ull},
            {0x5d89d0,8,0xf13ba4e687db6f76ull},
            {0xa2ac30,8,0xe8eceeb7e0a1ee35ull},
            {0x4244d0,8,0xd49a243d1287aeb0ull},
            {0xa2b910,8,0xcbf9ba3f56bc97e9ull},
            {0xa2ac40,8,0xe1d901e8a18f6d3full},
            {0x5d8970,8,0x143516343cb34a0dull},
            {0xa2ac30,6,0x6e5089acc09fb415ull},
            {0xa2b910,290,0x9b5957d3479b0eefull}
        }},
        {0x11501d0,{
            {0x11501d0,24,0x633130142ad7eb09ull},
            {0x854a80,8,0xf13ba4e687db6f76ull},
            {0x998cb0,8,0xe8eceeb7e0a1ee35ull},
            {0x4eeec0,8,0xd49a243d1287aeb0ull},
            {0x9993c0,8,0xcbf8d73f56bb1630ull},
            {0x4f17d0,8,0xe1d901e8a18f6d3full},
            {0x854b80,8,0x143539343cb38586ull},
            {0x998cb0,6,0x6e5089acc09fb415ull},
            {0x9993c0,263,0xcc31a5c3179ea022ull}
        }},
        {0x114e718,{
            {0x114e718,24,0x6657cd5301b06ebfull},
            {0x854850,8,0xf13ba4e687db6f76ull},
            {0x998a80,8,0xe8eceeb7e0a1ee35ull},
            {0x4eebe0,8,0xd49a243d1287aeb0ull},
            {0x999190,8,0xcbf8d73f56bb1630ull},
            {0x4f1450,8,0xe1d901e8a18f6d3full},
            {0x854950,8,0x1435a9343cb443d6ull},
            {0x998a80,6,0x6e5089acc09fb415ull},
            {0x999190,263,0xe9d4051f81bb4898ull}
        }}
    };
    const auto i=static_cast<unsigned>(edition)-1;return i<3?pins[i]:RoadProfile{};
}
inline bool Methods(Crossplay::Edition edition) {
    const auto p=RoadPin(edition);if(!p.table || !BuildCommandDetail::Methods(edition))return false;
    for(const auto& region:p.regions)if(!region.at || SharedEngineMode::Hash(region.at,region.size)!=region.hash)return false;
    return true;
}
inline bool Source(Crossplay::Edition edition,const void* event,uint32_t source,uint32_t player) {
    const auto p=BuildCommandDetail::Pin(edition);const auto road=RoadPin(edition);const auto* b=static_cast<const unsigned char*>(event);
    if(!p.event_vt || !road.table || (player!=1 && player!=2) || source==UINT32_MAX || !Detail::Readable(b,0x1c)
        || CrossplayChat::Word(b)!=p.event_vt || CrossplayChat::Word(b+4)!=CrossplayRoadCommand::Type
        || CrossplayChat::Word(b+8)!=source || b[12]>1 || CrossplayChat::Word(b+20)!=player || b[24]>1)return false;
    const auto* nested=reinterpret_cast<const unsigned char*>(CrossplayChat::Word(b+16));
    if(!Detail::Readable(nested,0x20) || CrossplayChat::Word(nested)!=road.table
        || CrossplayChat::Word(nested+4)!=CrossplayRoadCommand::PlacementType || nested[8]!=player
        || CrossplayChat::Word(nested+12)!=CrossplayRoadCommand::NormalStreetType)return false;
    for(unsigned offset:{16u,20u,24u,28u})if(!CrossplayBuildCommand::PositionBits(CrossplayChat::Word(nested+offset)))return false;
    return true;
}
inline bool Encode(Crossplay::Edition edition,const CrossplayNative::ParticipantEvent& binding,DWORD owner,
                   uintptr_t session,uint32_t own,uint32_t other,uint32_t player,const void* event,std::vector<unsigned char>& wire) {
    SharedEngineMode::ErrorScope error;LoadProgressDetail::FpScope fp;wire.clear();
    if(!LoadProgressDetail::World(binding,owner,session,own,other) || !Methods(edition) || !Source(edition,event,own,player))return false;
    if(Detail::Encode(BuildCommandDetail::Pin(edition),event,wire) && CrossplayRoadCommand::Inspect(wire,own,player))return true;
    wire.clear();return false;
}
inline bool Enqueue(Crossplay::Edition edition,const CrossplayNative::ParticipantEvent& binding,DWORD owner,
                    uintptr_t session,uint32_t own,uint32_t other,uint32_t player,const std::vector<unsigned char>& wire) {
    SharedEngineMode::ErrorScope error;LoadProgressDetail::FpScope fp;
    if(!LoadProgressDetail::World(binding,owner,session,own,other) || !CrossplayRoadCommand::Inspect(wire,other,player) || !Methods(edition))return false;
    const auto p=BuildCommandDetail::Pin(edition);Detail::OwnedStream stream(p);if(!stream.initialized)return false;
    stream.value.bytes=const_cast<unsigned char*>(wire.data());stream.value.capacity=stream.value.length=static_cast<uint32_t>(wire.size());stream.value.external=1;
    Detail::Visitor reader{p.reader_vt,1,&stream.value,0};const char* name="WePlayEvent";
    void* decoded=p.gold?Detail::GoldRead(p.read,&reader,name):reinterpret_cast<void* (__thiscall*)(void*,const char*,uint32_t)>(p.read)(&reader,name,0);
    if(!decoded || !Detail::Readable(decoded,0x1c) || *static_cast<const uintptr_t*>(decoded)!=p.event_vt)return false;
    std::vector<unsigned char> repeated;
    bool okay=stream.value.position==wire.size() && Source(edition,decoded,other,player) && Detail::Encode(p,decoded,repeated)
        && repeated==wire && LoadProgressDetail::World(binding,owner,session,own,other);
    uintptr_t send=0;void* transport=okay?Detail::Transport(edition,send):nullptr;
    if(transport)reinterpret_cast<void (__thiscall*)(void*,const void*,uint32_t,uint32_t,uint32_t,uint32_t)>(send)(transport,decoded,0,0,0,0);else okay=false;
    reinterpret_cast<void (__thiscall*)(void*,uint32_t)>(*reinterpret_cast<const uintptr_t*>(p.event_vt))(decoded,1);return okay;
}
}
