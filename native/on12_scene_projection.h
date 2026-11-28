#pragma once
#include <cstdint>
#include <cstddef>
#include <cstring>

namespace On12Graphics {
// Bounded original CTAB inspection only; no shader rewrite or binding change.
// Used once when the native runtime creates a vertex shader.
inline bool SceneProjectionShader(const void* opaque,size_t bytes){
    if(!opaque||bytes<16||bytes>65536||bytes%4)return false;
    const auto* p=static_cast<const unsigned char*>(opaque);
    auto read=[&](size_t at){std::uint32_t v=0;if(at<=bytes&&bytes-at>=4)std::memcpy(&v,p+at,4);return v;};
    const auto version=read(0);if(version!=0xfffe0300u&&version!=0xfffe0200u)return false;
    bool table=false,scene=false;
    for(size_t at=4;at<bytes;){const auto token=read(at),op=token&0xffff;
        if(token==0xffff)return at==bytes-4&&table&&scene;
        const size_t count=op==0xfffe?(token>>16)&0x7fff:(token>>24)&0xf;
        if(count>(bytes-at-4)/4)return false;
        if(op==0xfffe&&count&&read(at+4)==0x42415443){
            if(table||count<8)return false;table=true;const size_t base=at+8,length=(count-1)*4;
            auto u32=[&](size_t i){return i<=length&&length-i>=4?read(base+i):0u;};
            auto u16=[&](size_t i){std::uint16_t value=0;if(i<=length&&length-i>=2)std::memcpy(&value,p+base+i,2);return value;};
            const auto n=u32(12),offset=u32(16);
            if(u32(0)!=28||u32(8)!=version||n>256||offset<28||offset>length||n>(length-offset)/20)return false;
            for(std::uint32_t i=0;i<n;++i){const size_t item=offset+static_cast<size_t>(i)*20;const auto name=u32(item),type=u32(item+12);
                if(name<28||name>=length||type<28||type>length||length-type<16)return false;
                size_t end=name;while(end<length&&end-name<128&&p[base+end])++end;if(end==length||end-name==128)return false;
                auto same=[&](const char* text){return std::strlen(text)==end-name&&!std::memcmp(p+base+name,text,end-name);};
                const auto slot=u16(item+6);
                if((slot==12&&same("g_VSMainViewProjectionMatrix"))||(slot==64&&same("g_View2ClipTM"))){
                    const auto kind=u16(type);
                    if(u16(item+4)!=2||u16(item+8)!=4||(kind!=2&&kind!=3)||u16(type+2)!=3||u16(type+4)!=4||u16(type+6)!=4||u16(type+8)!=1||u16(type+10)!=0)return false;
                    scene=true;
                }
            }
        }at+=(count+1)*4;
    }return false;
}
}
