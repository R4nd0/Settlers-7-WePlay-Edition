#pragma once
#include "crossplay_player_option_codec.h"
#include "crossplay_preparation_positions.h"

namespace CrossplayNativeCodec {
namespace HistoryPositionDetail {
inline bool Methods(Crossplay::Edition edition) {
    if(edition!=Crossplay::Edition::UbisoftHistory && edition!=Crossplay::Edition::SteamHistory)return false;
    const bool steam=edition==Crossplay::Edition::SteamHistory;
    const uintptr_t entries[]={steam?0x585ea0u:0x5863e0u,steam?0x5856e0u:0x585c20u};
    const size_t lengths[]={0x57,0x79};uint64_t hash=14695981039346656037ull;
    for(unsigned i=0;i<2;++i) {
        const auto* bytes=reinterpret_cast<const unsigned char*>(entries[i]);
        if(!Detail::Readable(bytes,lengths[i]))return false;
        for(size_t n=0;n<lengths[i];++n)hash=(hash^bytes[n])*1099511628211ull;
    }
    return hash==0x0ba2772359b20879ull;
}
}
// Preparation projection only. The caller must first authenticate a genuine
// host Position notification and enqueue its original native event clone.
// This is not a world start, guest request handler or active-game mutation.
inline bool ProjectHistoryCastlePosition(Crossplay::Edition edition,const CrossplayNative::ParticipantEvent& binding,
                                         CrossplaySessionConfig::Connections ids,uint32_t target,uint32_t castle) {
    if(target<1 || target>2 || castle<1 || castle>4 || !HistoryPositionDetail::Methods(edition)
        || !PlayerOptionDetail::Context(edition,binding,ids,false))return false;
    const auto pin=Detail::Pin(edition);
    if(!Detail::Methods(pin) || !Detail::Tables(pin,edition))return false;
    CrossplayNative::LobbySnapshot original;unsigned char* rows=nullptr;
    if(!ReadyStateDetail::Preparation(edition,binding,ids,original,rows))return false;
    auto read=[](uint32_t at,void* output,size_t size) {
        const auto* bytes=reinterpret_cast<const void*>(at);
        if(!Detail::Readable(bytes,size))return false;memcpy(output,bytes,size);return true;
    };
    CrossplayPreparationPositions::Snapshot before,expected;
    if(!CrossplayPreparationPositions::Capture(reinterpret_cast<uint32_t>(rows),read,before)
        || !CrossplayPreparationPositions::Plan(before,target,castle,expected))return false;
    for(uint32_t index=0;index<CrossplayPreparationPositions::Count;++index)
        if(expected.positions[index]!=before.positions[index]
            && !CrossplayNative::HistoryRoleDetail::Writable(rows+index*0x5a8+0x58,1))return false;
    const bool steam=edition==Crossplay::Edition::SteamHistory;
    const uintptr_t pointer=steam?0x13a2300:0x13a5218;
    const auto* manager=*reinterpret_cast<const unsigned char* const*>(pointer);const uint8_t mode=manager[4];
    auto* config=*reinterpret_cast<unsigned char* const*>(manager+((mode-1)&1)*0xc4+0x98);
    CrossplayNative::LobbySnapshot current;unsigned char* current_rows=nullptr;CrossplayPreparationPositions::Snapshot reread;
    if(!ReadyStateDetail::Preparation(edition,binding,ids,current,current_rows) || current.instance!=original.instance || current_rows!=rows
        || !CrossplayPreparationPositions::Capture(reinterpret_cast<uint32_t>(rows),read,reread)
        || !CrossplayPreparationPositions::Equal(before,reread) || !HistoryPositionDetail::Methods(edition)
        || *reinterpret_cast<const unsigned char* const*>(pointer)!=manager || manager[4]!=mode
        || *reinterpret_cast<unsigned char* const*>(manager+((mode-1)&1)*0xc4+0x98)!=config
        || !Detail::Readable(config,8) || *reinterpret_cast<unsigned char* const*>(config)!=rows
        || *reinterpret_cast<unsigned char* const*>(config+4)!=rows)return false;
    reinterpret_cast<void (__thiscall*)(void*,uint32_t,uint32_t)>(steam?0x585ea0:0x5863e0)(config,target,castle);
    CrossplayPreparationPositions::Snapshot after;
    return *reinterpret_cast<const unsigned char* const*>(pointer)==manager && manager[4]==mode
        && *reinterpret_cast<unsigned char* const*>(manager+((mode-1)&1)*0xc4+0x98)==config
        && ReadyStateDetail::Preparation(edition,binding,ids,current,current_rows) && current.instance==original.instance && current_rows==rows
        && PlayerOptionDetail::Context(edition,binding,ids,false)
        && CrossplayPreparationPositions::Capture(reinterpret_cast<uint32_t>(rows),read,after)
        && CrossplayPreparationPositions::Equal(expected,after);
}
}
