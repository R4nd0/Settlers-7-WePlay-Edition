#pragma once
#include "crossplay_channel.h"
#include <cstring>

// Narrow lobby-chat wire gate. This does not admit setup, Ready, world or turn
// events. Only the authenticated remote sender's base CID is translated.
namespace CrossplayChat {
constexpr uint32_t Class=0x8f9bc6a9, Type=7u|(20u<<16);
constexpr size_t MaximumString=1024, Header=44, MaximumWire=2087;
inline uint32_t Word(const unsigned char* p) {
    return uint32_t(p[0])|(uint32_t(p[1])<<8)|(uint32_t(p[2])<<16)|(uint32_t(p[3])<<24);
}
inline void Put(unsigned char* p,uint32_t v) {
    for(unsigned i=0;i<4;++i)p[i]=static_cast<unsigned char>(v>>(8*i));
}
inline bool String(const std::vector<unsigned char>& wire,size_t& at) {
    if(at>wire.size() || wire.size()-at<8 || memcmp(wire.data()+at,"Str_",4))return false;
    const uint32_t count=Word(wire.data()+at+4);at+=8;
    if(count>MaximumString+1 || count>wire.size()-at || count==1)return false;
    if(count) {
        if(wire[at+count-1])return false;
        for(size_t i=0;i+1<count;++i)if(wire[at+i]<32 || wire[at+i]>126)return false;
    }
    at+=count;return true;
}
inline bool Validate(const std::vector<unsigned char>& wire,uint32_t source) {
    if(source==UINT32_MAX || wire.size()<37 || wire.size()>MaximumWire
        || Word(wire.data())!=Class || Word(wire.data()+4)!=Type
        || Word(wire.data()+8)!=source || wire[12]!=1)return false;
    size_t at=13;
    // The original native writer orders two strings, one scalar and one enum.
    return String(wire,at) && String(wire,at) && wire.size()-at==8
        && Word(wire.data()+at+4)<=3;
}
inline bool Encode(const Crossplay::Peer& peer,const std::vector<unsigned char>& wire,
                   uint32_t own,std::vector<unsigned char>& frame) {
    frame.clear();if(!Validate(wire,own))return false;
    const unsigned char magic[8]={'S','7','C','H','A','T',0,1};
    frame.assign(magic,magic+8);
    frame.insert(frame.end(),peer.local_id.begin(),peer.local_id.end());
    frame.insert(frame.end(),peer.remote_id.begin(),peer.remote_id.end());
    frame.resize(Header);Put(frame.data()+40,static_cast<uint32_t>(wire.size()));
    frame.insert(frame.end(),wire.begin(),wire.end());return true;
}
inline bool Decode(const Crossplay::Peer& peer,const std::vector<unsigned char>& frame,
                   uint32_t source,uint32_t local,std::vector<unsigned char>& wire) {
    wire.clear();const unsigned char magic[8]={'S','7','C','H','A','T',0,1};
    if(local==UINT32_MAX || frame.size()<Header || frame.size()>Header+MaximumWire
        || memcmp(frame.data(),magic,8) || memcmp(frame.data()+8,peer.remote_id.data(),16)
        || memcmp(frame.data()+24,peer.local_id.data(),16) || Word(frame.data()+40)!=frame.size()-Header)return false;
    std::vector<unsigned char> decoded(frame.begin()+Header,frame.end());
    if(!Validate(decoded,source))return false;
    Put(decoded.data()+8,local);wire=std::move(decoded);return true;
}
}
