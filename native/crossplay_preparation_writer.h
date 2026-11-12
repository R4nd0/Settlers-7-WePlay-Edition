#pragma once
#include "crossplay_active_player_writer.h"

// Original whole-player preparation writer ABI. Diagnostic only: the caller
// must validate the native owner, all 25 rows, positions and visitor. This does
// not capture the enclosing setup/rules/content or authorize a world start.
namespace CrossplayPreparationWriter {
struct Pin {uintptr_t entry=0;size_t size=0;uint64_t hash=0;};
inline bool Layout(Crossplay::Edition edition,Pin& p) {
    p={};
    if(edition==Crossplay::Edition::Gold)p={0x520a20,0x330,0xb0eccbf26f3c8307ull};
    else if(edition==Crossplay::Edition::UbisoftHistory)p={0x58e910,0x280,0xf6d36afbb5e9b427ull};
    else if(edition==Crossplay::Edition::SteamHistory)p={0x58e3d0,0x280,0x7a8a32bf9daeb493ull};
    else return false;
    return true;
}
inline bool Methods(Crossplay::Edition edition,const Pin& p) {
    Pin expected{};CrossplayActivePlayerWriter::Pin rows{};
    if(!Layout(edition,expected) || p.entry!=expected.entry || p.size!=expected.size || p.hash!=expected.hash
        || !CrossplayActivePlayerWriter::Layout(edition,rows) || !CrossplayActivePlayerWriter::Methods(rows))return false;
    const auto* data=reinterpret_cast<const unsigned char*>(p.entry);
    if(!CrossplayNativeCodec::Detail::Readable(data,p.size))return false;
    uint64_t hash=14695981039346656037ull;
    for(size_t i=0;i<p.size;++i)hash=(hash^data[i])*1099511628211ull;
    return hash==p.hash;
}
namespace Detail {
// Gold: EAX=visitor, stack=preparation/version, ret8; History:
// thiscall(preparation, visitor, version), ret8. No guessed argument remapping.
inline void Write(Crossplay::Edition edition,const Pin& p,void* rows,void* visitor,uint32_t version) {
    if(edition==Crossplay::Edition::Gold) {
        const uintptr_t address=p.entry;
        __asm {
            mov eax,visitor
            push version
            push rows
            call address
        }
    } else reinterpret_cast<void (__thiscall*)(void*,void*,uint32_t)>(p.entry)(rows,visitor,version);
}
}
}
