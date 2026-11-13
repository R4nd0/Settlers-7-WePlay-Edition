#pragma once
#include "crossplay_setup_signature_codec.h"

// Original nonempty rule bodies and their CString/catalogue helpers. Whole
// executable identity and the enclosing writer/rules guards remain required.
namespace CrossplaySetupRootWriter {
inline bool RuleMethods(Crossplay::Edition edition) {
    struct Region {uintptr_t address;size_t bytes;uint64_t hash;};
    static const Region regions[3][6]={
        {{0x9ce320,0xc0,0x8c41f0725fcab4a6ull},{0x9ce3f0,0x3b0,0x094d23847376b605ull},
         {0x9cdab0,0x50,0xc022582cf2976861ull},{0x429470,0x100,0x1d76850a66aff2acull},
         {0x4281d0,0x100,0xb408bce0414c2a0aull},{0x415e00,0xa00,0xb1334ddc9a39c5cdull}},
        {{0x5f5240,0xc0,0xb57386ff12630b34ull},{0x5f5310,0x480,0xb91fc926ba9dd6d3ull},
         {0x5ef990,0x50,0xd0685a4513ab0b77ull},{0x4f30b0,0x100,0x6fc045215f8ca030ull},
         {0x4f2230,0x100,0xb561d66b04f087f5ull},{0x4e2300,0x800,0x46621900dae8b1fdull}},
        {{0x5f4da0,0xc0,0xed384101e4c8bd9dull},{0x5f4e70,0x480,0xb5e96721a2611e8aull},
         {0x5ef3f0,0x50,0xb003cb4168d70f71ull},{0x4f2d30,0x100,0x339edd1f041c763full},
         {0x4f1eb0,0x100,0xb561d66b04f087f5ull},{0x4e2100,0x800,0x7f33764068b1a7b8ull}}
    };
    size_t index=0;
    if(edition==Crossplay::Edition::Gold)index=0;
    else if(edition==Crossplay::Edition::UbisoftHistory)index=1;
    else if(edition==Crossplay::Edition::SteamHistory)index=2;
    else return false;
    for(const auto& region:regions[index]) {
        const auto* code=reinterpret_cast<const unsigned char*>(region.address);
        if(!CrossplayNativeCodec::Detail::Readable(code,region.bytes))return false;
        uint64_t hash=14695981039346656037ull;
        for(size_t i=0;i<region.bytes;++i)hash=(hash^code[i])*1099511628211ull;
        if(hash!=region.hash)return false;
    }
    return true;
}
}
