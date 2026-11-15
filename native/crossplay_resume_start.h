#pragma once
#include "crossplay_resume_transfer.h"
#include "crossplay_game_start.h"
#include <string>

// Separate saved-context namespace. Actual Gold sends an empty native field
// after ChangeSavegame; the nonempty source token belongs to its native cache.
// Callers must admit that cache/checkpoint and observe original file/Ready
// consumers. This envelope alone does not prove a saved-session admission.
namespace CrossplayResumeStart {
constexpr uint32_t Class=0x8bf00ae9u,Type=7u|(7u<<16);
constexpr size_t NativeHeader=21,Header=CrossplayResumeTransfer::ContextHeader;
constexpr unsigned char Magic[8]={'S','7','R','S','T','A',0,2};
inline bool SlotName(const std::string& name) {
    if(name.size()<2 || name.size()>11 || name[0]!='s')return false;
    return std::all_of(name.begin()+1,name.end(),[](char c){return c>='0' && c<='9';});
}
inline bool Inspect(const std::vector<unsigned char>& wire,uint32_t host,const std::string& name) {
    return SlotName(name) && wire.size()==NativeHeader && CrossplayGameStart::Inspect(wire,host);
}
inline bool Encode(const Crossplay::Peer& peer,const CrossplayResumeTransfer::Checkpoint& checkpoint,
                   uint32_t host,const std::string& name,const std::vector<unsigned char>& wire,
                   std::vector<unsigned char>& frame) {
    frame.clear();if(!CrossplayResumeTransfer::Valid(checkpoint) || !Inspect(wire,host,name))return false;
    frame.resize(Header);memcpy(frame.data(),Magic,8);
    memcpy(frame.data()+8,peer.local_id.data(),16);memcpy(frame.data()+24,peer.remote_id.data(),16);
    CrossplayResumeTransfer::PutCheckpoint(frame.data()+40,checkpoint);
    CrossplayChat::Put(frame.data()+188,1);CrossplayChat::Put(frame.data()+192,static_cast<uint32_t>(wire.size()));
    frame.resize(Header+4);CrossplayChat::Put(frame.data()+Header,static_cast<uint32_t>(name.size()));
    frame.insert(frame.end(),name.begin(),name.end());
    frame.insert(frame.end(),wire.begin(),wire.end());return true;
}
inline bool Decode(const Crossplay::Peer& peer,const CrossplayResumeTransfer::Checkpoint& checkpoint,
                   uint32_t source_host,uint32_t local_host,const std::string& name,
                   const std::vector<unsigned char>& frame,std::vector<unsigned char>& wire) {
    wire.clear();unsigned char expected[148]{};
    if(!CrossplayResumeTransfer::Valid(checkpoint) || !SlotName(name) || local_host==UINT32_MAX
        || frame.size()!=Header+4+NativeHeader+name.size())return false;
    CrossplayResumeTransfer::PutCheckpoint(expected,checkpoint);
    if(memcmp(frame.data(),Magic,8) || memcmp(frame.data()+8,peer.remote_id.data(),16)
        || memcmp(frame.data()+24,peer.local_id.data(),16) || memcmp(frame.data()+40,expected,sizeof(expected))
        || CrossplayChat::Word(frame.data()+188)!=1 || CrossplayChat::Word(frame.data()+192)!=NativeHeader
        || CrossplayChat::Word(frame.data()+Header)!=name.size()
        || memcmp(frame.data()+Header+4,name.data(),name.size()))return false;
    std::vector<unsigned char> decoded(frame.begin()+Header+4+name.size(),frame.end());
    if(!Inspect(decoded,source_host,name))return false;
    CrossplayChat::Put(decoded.data()+8,local_host);
    if(!Inspect(decoded,local_host,name))return false;
    wire=std::move(decoded);return true;
}
}
