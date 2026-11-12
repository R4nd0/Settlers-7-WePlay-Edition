#pragma once
#include "crossplay_event_codec.h"

// ABI primitive for the original active-player writer, excluded from ordinary
// adapters. This is not a session capture, setup agreement or start permission.
// The caller must independently own and validate the preparation and visitor.
namespace CrossplayActivePlayerWriter {
struct Pin {
    uintptr_t row,castle;uint64_t row_hash,castle_hash;size_t stride;
};
inline bool Layout(Crossplay::Edition edition,Pin& p) {
    p={};
    if(edition==Crossplay::Edition::Gold)p={0x520d50,0x5273a0,0x2c164f6707d8c358ull,0xc57f2e803b5e3c9eull,0x5c4};
    else if(edition==Crossplay::Edition::UbisoftHistory)p={0x58eb90,0x586b90,0xc3859be4b09f16a1ull,0xc404733c64731dc5ull,0x5a8};
    else if(edition==Crossplay::Edition::SteamHistory)p={0x58e650,0x586650,0x1811ccd4ab696a91ull,0xb90bf2fb50229bb7ull,0x5a8};
    else return false;
    return true;
}
inline bool Methods(const Pin& p) {
    for(const auto& region:{std::pair<uintptr_t,size_t>{p.row,0x3000},{p.castle,0x2000}}) {
        const auto* data=reinterpret_cast<const unsigned char*>(region.first);
        if(!CrossplayNativeCodec::Detail::Readable(data,region.second))return false;
        uint64_t hash=14695981039346656037ull;
        for(size_t i=0;i<region.second;++i)hash=(hash^data[i])*1099511628211ull;
        if(hash!=(region.first==p.row?p.row_hash:p.castle_hash))return false;
    }
    return true;
}
namespace Detail {
// Gold: ECX = all-row base, EDI = visitor, stack = slot/version, ret 8.
// History: thiscall(all-row base, visitor, slot, version), ret 12.
// Kept separate from Methods so the offline oracle can test the exact thunk
// with a recording visitor. Never call this helper on unvalidated live data.
inline void Write(Crossplay::Edition edition,const Pin& p,void* rows,void* visitor,uint32_t slot,uint32_t version) {
    if(edition==Crossplay::Edition::Gold) {
        const uintptr_t address=p.row;
        __asm {
            mov ecx,rows
            mov edi,visitor
            push version
            push slot
            call address
        }
    } else reinterpret_cast<void (__thiscall*)(void*,void*,uint32_t,uint32_t)>(p.row)(rows,visitor,slot,version);
}
}
}
