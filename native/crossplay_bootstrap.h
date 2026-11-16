#pragma once
#include "crossplay_session_config.h"
// The authenticated host's observed initial map permits local guest session
// preparation only. It grants no roster, native Ready, save-file or world start.
namespace CrossplayBootstrap {
constexpr unsigned char Magic[8]={'S','7','X','B','O','O','T',1};
constexpr size_t Header=48,MaximumMap=CrossplaySessionConfig::MaximumPath;
struct Context {std::string map;bool saved=false;};
inline bool Valid(const Context& value){
    if(!CrossplaySessionConfig::RelativeMap(value.map))return false;
    for(unsigned char c:value.map)if(c<32 || c>126)return false;
    return true;
}
inline bool Encode(const Crossplay::Peer& peer,const Context& context,std::vector<unsigned char>& output){
    output.clear();if(!Valid(context) || peer.local_id==peer.remote_id)return false;
    output.resize(Header);memcpy(output.data(),Magic,8);memcpy(output.data()+8,peer.local_id.data(),16);memcpy(output.data()+24,peer.remote_id.data(),16);
    CrossplayChat::Put(output.data()+40,context.saved?1u:0u);CrossplayChat::Put(output.data()+44,static_cast<uint32_t>(context.map.size()));
    output.insert(output.end(),context.map.begin(),context.map.end());return true;
}
inline bool Decode(const Crossplay::Peer& peer,const std::vector<unsigned char>& frame,Context& output){
    output={};
    if(peer.local_id==peer.remote_id || frame.size()<=Header || frame.size()>Header+MaximumMap || memcmp(frame.data(),Magic,8)
        || memcmp(frame.data()+8,peer.remote_id.data(),16) || memcmp(frame.data()+24,peer.local_id.data(),16)
        || CrossplayChat::Word(frame.data()+40)>1 || CrossplayChat::Word(frame.data()+44)!=frame.size()-Header)return false;
    Context next{std::string(frame.begin()+Header,frame.end()),CrossplayChat::Word(frame.data()+40)==1};
    if(!Valid(next))return false;output=std::move(next);return true;
}
}
