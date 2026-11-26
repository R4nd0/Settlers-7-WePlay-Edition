// Authored compatibility edit only; original program supplied by owned device.
#pragma once
#include <cstdint>
#include <vector>
#include <cstring>

namespace WaterOn12PowCap {
inline bool Generate(const std::vector<std::uint32_t>& source,std::vector<std::uint32_t>& result) {
    const std::uint32_t first[]={0x03000020,0x80080002,0x80ff0000,0xa0ff0077};
    const std::uint32_t second[]={0x03000020,0x80010005,0x80aa0000,0xa0ff0077};
    const std::uint32_t products[]={0x03000005,0x80040000,0x80ff0000,0x80000005,0x03000005,0x80080000,0x80ff0002,0x80ff0000};
    if(source.size()!=648||source.front()!=0xfffe0300||source.back()!=0xffff
        ||std::memcmp(source.data()+469,first,sizeof(first))||std::memcmp(source.data()+486,second,sizeof(second))
        ||std::memcmp(source.data()+490,products,sizeof(products)))return false;
    // Caller first verifies the entire exact original SHA256. Its CTAB and
    // direct operands and CTAB use no c255. Existing relative loads, their
    // bases and indices remain unchanged. DEF is local to this shader.
    result=source;
    // Cap after multiplication by 1+cosine^2, which could overflow a capped
    // POW again. Do this before the original zero scattering multipliers.
    result.insert(result.begin()+498,{0x0300000a,0x80040000,0x80aa0000,0xa00000ff,
                                     0x0300000a,0x80080000,0x80ff0000,0xa00000ff});
    result.insert(result.begin()+240,{0x05000051,0xa00f00ff,0x7f7fffff,0,0,0});
    return result.size()==662;
}
}
