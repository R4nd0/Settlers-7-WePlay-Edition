#pragma once
#include "on12_scene_projection.h"

namespace On12Graphics {
inline unsigned ProjectionRegisterForShader(const void* opaque,size_t bytes){
    if(!SceneProjectionShader(opaque,bytes))return 0;
    const auto* p=static_cast<const unsigned char*>(opaque);
    auto u32=[&](size_t at){std::uint32_t value=0;std::memcpy(&value,p+at,4);return value;};
    auto u16=[&](size_t at){std::uint16_t value=0;std::memcpy(&value,p+at,2);return value;};
    for(size_t at=4;at<bytes;){const auto token=u32(at),op=token&0xffff;if(token==0xffff)break;
        const size_t count=op==0xfffe?(token>>16)&0x7fff:(token>>24)&15;
        if(op==0xfffe&&count&&u32(at+4)==0x42415443){const size_t base=at+8;const auto total=u32(base+12),offset=u32(base+16);
            for(std::uint32_t n=0;n<total;++n){const size_t entry=base+offset+n*20;const auto name=u32(entry);
                if(u16(entry+6)==64&&!std::strcmp(reinterpret_cast<const char*>(p+base+name),"g_View2ClipTM"))return 64;
            }}at+=(count+1)*4;
    }return 12;
}
}
