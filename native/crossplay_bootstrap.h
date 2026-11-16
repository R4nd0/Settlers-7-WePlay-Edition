#pragma once
#include "crossplay_session_config.h"
// The authenticated host's observed initial map permits local guest session
// preparation only. It grants no roster, native Ready, save-file or world start.
namespace CrossplayBootstrap {
constexpr unsigned char Magic[8]={'S','7','X','B','O','O','T',2};
constexpr size_t Header=56,MaximumMap=CrossplaySessionConfig::MaximumPath,MaximumTemplate=255;
// Bits come from the original host configuration: positions, teams, coop.
// The explicit observation marker prevents old map-only callers from silently
// supplying invented false/default choices after this protocol extension.
struct Context {std::string map;bool saved=false;uint32_t flags=0;std::string map_template;bool choices_observed=false;};
inline bool Printable(const std::string& value){for(unsigned char c:value)if(c<32 || c>126)return false;return true;}
inline bool Template(const std::string& value){
    if(value.empty())return true;
    if(value.size()>MaximumTemplate || !Printable(value))return false;
    // Gold's original getter can return its virtual Download resource path.
    // This is an engine resource name; no OS path is opened by this protocol.
    constexpr char download[]="\\Download\\Template";
    if(value==download)return true;
    if(value.compare(0,sizeof(download)-1,download)==0 && value.size()>sizeof(download)-1 && value[sizeof(download)-1]=='\\')
        return CrossplaySessionConfig::RelativeMap(value.substr(1));
    return CrossplaySessionConfig::RelativeMap(value);
}
inline bool Valid(const Context& value){
    return value.choices_observed && value.flags<=7 && CrossplaySessionConfig::RelativeMap(value.map)
        && Printable(value.map) && Template(value.map_template);
}
inline bool Encode(const Crossplay::Peer& peer,const Context& context,std::vector<unsigned char>& output){
    output.clear();if(!Valid(context) || peer.local_id==peer.remote_id)return false;
    output.resize(Header);memcpy(output.data(),Magic,8);memcpy(output.data()+8,peer.local_id.data(),16);memcpy(output.data()+24,peer.remote_id.data(),16);
    CrossplayChat::Put(output.data()+40,context.saved?1u:0u);CrossplayChat::Put(output.data()+44,static_cast<uint32_t>(context.map.size()));
    CrossplayChat::Put(output.data()+48,context.flags);CrossplayChat::Put(output.data()+52,static_cast<uint32_t>(context.map_template.size()));
    output.insert(output.end(),context.map.begin(),context.map.end());output.insert(output.end(),context.map_template.begin(),context.map_template.end());return true;
}
inline bool Decode(const Crossplay::Peer& peer,const std::vector<unsigned char>& frame,Context& output){
    output={};
    if(peer.local_id==peer.remote_id || frame.size()<=Header || frame.size()>Header+MaximumMap+MaximumTemplate || memcmp(frame.data(),Magic,8)
        || memcmp(frame.data()+8,peer.remote_id.data(),16) || memcmp(frame.data()+24,peer.local_id.data(),16)
        || CrossplayChat::Word(frame.data()+40)>1 || CrossplayChat::Word(frame.data()+48)>7)return false;
    const auto map_bytes=CrossplayChat::Word(frame.data()+44),template_bytes=CrossplayChat::Word(frame.data()+52);
    if(!map_bytes || map_bytes>MaximumMap || template_bytes>MaximumTemplate || map_bytes+template_bytes!=frame.size()-Header)return false;
    Context next{std::string(frame.begin()+Header,frame.begin()+Header+map_bytes),CrossplayChat::Word(frame.data()+40)==1,
        CrossplayChat::Word(frame.data()+48),std::string(frame.begin()+Header+map_bytes,frame.end()),true};
    if(!Valid(next))return false;output=std::move(next);return true;
}
}
