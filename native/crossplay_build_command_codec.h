#pragma once
#include "crossplay_load_progress_codec.h"
#include "crossplay_shared_engine_mode.h"
#include "crossplay_build_command.h"

// Opt-in fresh-world storehouse command only. Caller retains executable and
// authenticated owner/session binding, and FIFO order relative to EOT batches.
namespace CrossplayNativeCodec::BuildCommandDetail {
struct Region {uintptr_t at;unsigned size;uint64_t hash;};
struct LogicProfile {uintptr_t outer,nested;Region regions[20];};
inline LogicProfile LogicPin(Crossplay::Edition edition) {
    static const LogicProfile pins[]={
        {0xf557c8,0xf3739c,{
            {0xf557c8,24,0xcd40ee49c9b4ee80ull},
            {0xa3b1e0,8,0xf0b143e68766ae34ull},
            {0xa48390,8,0x6291c062f60af45full},
            {0x4244d0,8,0xd49a243d1287aeb0ull},
            {0xa48d40,8,0xcbf9b83f56bc9483ull},
            {0xa483a0,8,0xe1d901e8a18f6d3full},
            {0xa3b180,8,0x828ba3e23eb02119ull},
            {0xa48390,6,0x4f79f7e70e55cbbfull},
            {0xa48d40,208,0x8e2fba4de96bd130ull},
            {0xf3739c,24,0x1c7cc3de75f5e35cull},
            {0x5d8750,8,0xf13ba4e687db6f76ull},
            {0xa2abb0,8,0x616bcb9fa51b0d7eull},
            {0x4244d0,8,0xd49a243d1287aeb0ull},
            {0xa2b350,8,0x5e801e1d4d8d0033ull},
            {0xa2abc0,8,0xe1d901e8a18f6d3full},
            {0x5d8730,8,0x75ef81c2153819d7ull},
            {0xa2abb0,6,0x81661f5379554a6eull},
            {0xa2b350,180,0xca22e36b4b02140bull},
            {0xa2afe0,160,0xee8b53b2ec372551ull},
            {0xa2b1f0,200,0xe8efb11e3904e01full}
        }},
        {0x113f4a0,0x11501ec,{
            {0x113f4a0,24,0x49d4d1031a6b7af6ull},
            {0x65dd50,8,0xe1189a2e1ef53cd9ull},
            {0xee4ed0,8,0xe118c72e1ef58950ull},
            {0x4eeec0,8,0xd49a243d1287aeb0ull},
            {0xee4ee0,8,0x29c2abbbe90a5c9eull},
            {0xee4f80,8,0xe118c72e1ef58950ull},
            {0x65dd10,8,0x29c2abbbe90a5c9eull},
            {0xee4ed0,16,0x85861ad080306f55ull},
            {0xee4ee0,154,0xed08f3315571293eull},
            {0x11501ec,24,0x223442d1749cc1e6ull},
            {0x854a80,8,0xf13ba4e687db6f76ull},
            {0x998c70,8,0x616bcb9fa51b0d7eull},
            {0x4eeec0,8,0xd49a243d1287aeb0ull},
            {0x998fc0,8,0x5e80281d4d8d1131ull},
            {0x4f17d0,8,0xe1d901e8a18f6d3full},
            {0x854a20,8,0x1434d9343cb2e266ull},
            {0x998c70,6,0x81661f5379554a6eull},
            {0x998fc0,171,0x1482e634e0325239ull},
            {0x998e60,151,0x75be13102c72e4baull},
            {0x998f00,189,0xb021e325e03b8dcfull}
        }},
        {0x113da4c,0x114e76c,{
            {0x113da4c,24,0x0e87a0efb2f837e1ull},
            {0x65d930,8,0xe1189a2e1ef53cd9ull},
            {0xee45c0,8,0xe118c72e1ef58950ull},
            {0x4eebe0,8,0xd49a243d1287aeb0ull},
            {0xee45d0,8,0x29c2abbbe90a5c9eull},
            {0xee4670,8,0xe118c72e1ef58950ull},
            {0x65d8f0,8,0x29c2abbbe90a5c9eull},
            {0xee45c0,16,0x85861ad080306f55ull},
            {0xee45d0,154,0x80814121d1933d1aull},
            {0x114e76c,24,0x9f81823e0f52a741ull},
            {0x854850,8,0xf13ba4e687db6f76ull},
            {0x998a40,8,0x616bcb9fa51b0d7eull},
            {0x4eebe0,8,0xd49a243d1287aeb0ull},
            {0x998d90,8,0x5e80281d4d8d1131ull},
            {0x4f1450,8,0xe1d901e8a18f6d3full},
            {0x8547f0,8,0x143549343cb3a0b6ull},
            {0x998a40,6,0x81661f5379554a6eull},
            {0x998d90,171,0x784cb69a7d07151aull},
            {0x998c30,151,0x0d23737fc624cfbeull},
            {0x998cd0,189,0xe3babb694ee19229ull}
        }}
    };
    const auto i=static_cast<unsigned>(edition)-1;return i<3?pins[i]:LogicProfile{};
}
inline bool Guard(const LogicProfile& p) {
    if(!p.outer || !p.nested)return false;
    for(const auto& region:p.regions)if(!region.at || SharedEngineMode::Hash(region.at,region.size)!=region.hash)return false;
    return true;
}
inline Detail::Profile Pin(Crossplay::Edition edition){auto p=Detail::Pin(edition);p.event_vt=LogicPin(edition).outer;return p;}
inline bool Methods(Crossplay::Edition edition){const auto p=Pin(edition);return Guard(LogicPin(edition)) && Detail::Methods(p) && Detail::Tables(p,edition);}
inline bool Source(Crossplay::Edition edition,const void* event,uint32_t source,uint32_t player) {
    const auto p=Pin(edition);const auto* b=static_cast<const unsigned char*>(event);
    if(!p.event_vt || !LogicPin(edition).nested || (player!=1 && player!=2) || source==UINT32_MAX || !Detail::Readable(b,0x1c)
        || CrossplayChat::Word(b)!=p.event_vt || CrossplayChat::Word(b+4)!=CrossplayBuildCommand::Type
        || CrossplayChat::Word(b+8)!=source || b[12]>1 || CrossplayChat::Word(b+20)!=player || b[24]>1)return false;
    const auto* n=reinterpret_cast<const unsigned char*>(CrossplayChat::Word(b+16));
    return Detail::Readable(n,0x20) && CrossplayChat::Word(n)==LogicPin(edition).nested
        && CrossplayChat::Word(n+4)==CrossplayBuildCommand::PlacementType && n[8]==player
        && CrossplayBuildCommand::PositionBits(CrossplayChat::Word(n+12)) && CrossplayBuildCommand::PositionBits(CrossplayChat::Word(n+16))
        && CrossplayBuildCommand::OrientationBits(CrossplayChat::Word(n+20))
        && CrossplayChat::Word(n+24)==CrossplayBuildCommand::StorehouseType && CrossplayChat::Word(n+28)==CrossplayBuildCommand::BuildingCategory;
}
inline bool Encode(Crossplay::Edition edition,const CrossplayNative::ParticipantEvent& binding,DWORD owner,
                   uintptr_t session,uint32_t own,uint32_t other,uint32_t player,const void* event,std::vector<unsigned char>& wire) {
    SharedEngineMode::ErrorScope error;LoadProgressDetail::FpScope fp;wire.clear();
    if(!LoadProgressDetail::World(binding,owner,session,own,other) || !Methods(edition) || !Source(edition,event,own,player))return false;
    if(Detail::Encode(Pin(edition),event,wire) && CrossplayBuildCommand::Inspect(wire,own,player))return true;
    wire.clear();return false;
}
inline bool Enqueue(Crossplay::Edition edition,const CrossplayNative::ParticipantEvent& binding,DWORD owner,
                    uintptr_t session,uint32_t own,uint32_t other,uint32_t player,const std::vector<unsigned char>& wire) {
    SharedEngineMode::ErrorScope error;LoadProgressDetail::FpScope fp;
    if(!LoadProgressDetail::World(binding,owner,session,own,other) || !CrossplayBuildCommand::Inspect(wire,other,player) || !Methods(edition))return false;
    const auto p=Pin(edition);Detail::OwnedStream stream(p);if(!stream.initialized)return false;
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
