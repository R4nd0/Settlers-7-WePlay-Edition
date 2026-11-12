#pragma once
#include "crossplay_chat.h"

// Host notifications for the admitted two-human Monhaim preparation lobby.
// These are neither guest requests nor an agreement to start a world.
namespace CrossplayPlayerOption {
constexpr uint32_t Color=0x0be2024e,Team=0xc773eae9;
constexpr size_t Header=48;
struct Value {uint32_t klass=0,slot=0,value=0;};
inline bool Inspect(const std::vector<unsigned char>& wire,uint32_t host,Value& result) {
    result={};
    if(host==UINT32_MAX || (wire.size()!=18 && wire.size()!=21))return false;
    const uint32_t klass=CrossplayChat::Word(wire.data());
    const bool color=klass==Color;
    if((!color && klass!=Team) || wire.size()!=(color?21u:18u)
        || CrossplayChat::Word(wire.data()+4)!=(7u|((color?0x32u:0x1eu)<<16))
        || CrossplayChat::Word(wire.data()+8)!=host || wire[12]!=1)return false;
    const uint32_t slot=CrossplayChat::Word(wire.data()+13);
    const uint32_t value=color?CrossplayChat::Word(wire.data()+17):wire[17];
    if(slot<1 || slot>2 || (color?(value<2 || value>7):(value<1 || value>4)))return false;
    result={klass,slot,value};return true;
}
inline bool Encode(const Crossplay::Peer& peer,uint32_t host,uint32_t revision,
                   const std::vector<unsigned char>& wire,std::vector<unsigned char>& frame) {
    frame.clear();Value value;
    if(!revision || (peer.edition!=Crossplay::Edition::UbisoftHistory && peer.edition!=Crossplay::Edition::SteamHistory)
        || !Inspect(wire,host,value))return false;
    const unsigned char magic[8]={'S','7','P','O','P','T',0,1};frame.assign(Header,0);
    memcpy(frame.data(),magic,8);memcpy(frame.data()+8,peer.local_id.data(),16);memcpy(frame.data()+24,peer.remote_id.data(),16);
    CrossplayChat::Put(frame.data()+40,revision);CrossplayChat::Put(frame.data()+44,static_cast<uint32_t>(wire.size()));
    frame.insert(frame.end(),wire.begin(),wire.end());return true;
}
inline bool Decode(const Crossplay::Peer& peer,uint32_t source_host,uint32_t local_host,uint32_t previous,
                   const std::vector<unsigned char>& frame,std::vector<unsigned char>& wire) {
    wire.clear();const unsigned char magic[8]={'S','7','P','O','P','T',0,1};
    if(peer.edition!=Crossplay::Edition::Gold || local_host==UINT32_MAX || previous==UINT32_MAX
        || (frame.size()!=Header+18 && frame.size()!=Header+21) || memcmp(frame.data(),magic,8)
        || memcmp(frame.data()+8,peer.remote_id.data(),16) || memcmp(frame.data()+24,peer.local_id.data(),16)
        || CrossplayChat::Word(frame.data()+40)!=previous+1
        || CrossplayChat::Word(frame.data()+44)!=frame.size()-Header)return false;
    std::vector<unsigned char> decoded(frame.begin()+Header,frame.end());Value value;
    if(!Inspect(decoded,source_host,value))return false;
    CrossplayChat::Put(decoded.data()+8,local_host);wire=std::move(decoded);return true;
}
}
