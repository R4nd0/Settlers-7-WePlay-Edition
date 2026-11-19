#pragma once
#include "crossplay_session_config.h"
#include "crossplay_resume_start.h"

// A distinct saved-setup envelope. The unchanged fresh configuration parser
// rejects its nonempty save token. Receiving setup is not file completion.
namespace CrossplaySavedSessionConfig {
using Connections=CrossplaySessionConfig::Connections;
using Layout=CrossplaySessionConfig::Layout;
constexpr size_t Header=CrossplayResumeTransfer::ContextHeader;
constexpr unsigned char Magic[8]={'S','7','R','C','F','G',0,1};
enum class Scope:uint32_t { Pair=1,Room=2 };

inline bool EncodeBound(const Crossplay::Peer& peer,const CrossplayResumeTransfer::Checkpoint& checkpoint,
                        Scope scope,const std::vector<unsigned char>& wire,std::vector<unsigned char>& frame) {
    frame.clear();
    if(!CrossplayResumeTransfer::Valid(checkpoint))return false;
    frame.resize(Header);memcpy(frame.data(),Magic,8);
    memcpy(frame.data()+8,peer.local_id.data(),16);memcpy(frame.data()+24,peer.remote_id.data(),16);
    CrossplayResumeTransfer::PutCheckpoint(frame.data()+40,checkpoint);
    CrossplayChat::Put(frame.data()+188,static_cast<uint32_t>(scope));
    CrossplayChat::Put(frame.data()+192,static_cast<uint32_t>(wire.size()));
    frame.insert(frame.end(),wire.begin(),wire.end());return true;
}

inline bool ReadFrame(const Crossplay::Peer& peer,const CrossplayResumeTransfer::Checkpoint& checkpoint,
                      Scope scope,const std::vector<unsigned char>& frame,std::vector<unsigned char>& wire) {
    wire.clear();
    if(!CrossplayResumeTransfer::Valid(checkpoint) || frame.size()<Header+13
        || frame.size()>Header+CrossplaySessionConfig::MaximumWire)return false;
    unsigned char expected[148]{};
    CrossplayResumeTransfer::PutCheckpoint(expected,checkpoint);
    if(memcmp(frame.data(),Magic,8) || memcmp(frame.data()+8,peer.remote_id.data(),16)
        || memcmp(frame.data()+24,peer.local_id.data(),16) || memcmp(frame.data()+40,expected,sizeof(expected))
        || CrossplayChat::Word(frame.data()+188)!=static_cast<uint32_t>(scope)
        || CrossplayChat::Word(frame.data()+192)!=frame.size()-Header)return false;
    wire.assign(frame.begin()+Header,frame.end());return true;
}

inline bool Inspect(const std::vector<unsigned char>& wire,Connections ids,const std::string& map,
                    const std::string& save,Layout& result,bool local_snapshot=false) {
    result={};
    if(!CrossplaySessionConfig::Distinct(ids) || !CrossplaySessionConfig::RelativeMap(map)
        || !CrossplayResumeStart::SlotName(save) || wire.size()<13 || wire.size()>CrossplaySessionConfig::MaximumWire
        || CrossplayChat::Word(wire.data())!=CrossplaySessionConfig::Class
        || CrossplayChat::Word(wire.data()+4)!=CrossplaySessionConfig::Type
        || CrossplayChat::Word(wire.data()+8)!=ids.host || wire[12]!=1)return false;
    size_t at=13;std::string actual_map,actual_save;
    if(!CrossplaySessionConfig::String(wire,at,actual_map) || actual_map!=map
        || !CrossplaySessionConfig::String(wire,at,actual_save) || actual_save!=save
        || wire.size()-at!=4+CrossplaySessionConfig::Slots*CrossplaySessionConfig::SlotBytes
        || CrossplayChat::Word(wire.data()+at)!=CrossplaySessionConfig::Slots)return false;
    at+=4;Layout layout;layout.map=actual_map;layout.slots_at=at;
    for(size_t i=0;i<CrossplaySessionConfig::Slots;++i,at+=CrossplaySessionConfig::SlotBytes) {
        if(!CrossplaySessionConfig::SlotValid(i,wire[at],wire[at+5],CrossplayChat::Word(wire.data()+at+1),
            CrossplayChat::Word(wire.data()+at+6),wire[at+10],ids,local_snapshot))return false;
    }
    result=std::move(layout);return true;
}
inline bool BindSource(const std::vector<unsigned char>& snapshot,Connections ids,const std::string& map,
                       const std::string& save,std::vector<unsigned char>& wire) {
    wire.clear();Layout layout;
    if(!Inspect(snapshot,ids,map,save,layout,true))return false;
    auto projected=snapshot;
    CrossplayChat::Put(projected.data()+layout.slots_at+6,ids.host);
    CrossplayChat::Put(projected.data()+layout.slots_at+CrossplaySessionConfig::SlotBytes+6,ids.guest);
    if(!Inspect(projected,ids,map,save,layout))return false;
    wire=std::move(projected);return true;
}
inline bool Encode(const Crossplay::Peer& peer,const CrossplayResumeTransfer::Checkpoint& checkpoint,
                   Connections ids,const std::string& map,const std::string& save,
                   const std::vector<unsigned char>& wire,std::vector<unsigned char>& frame) {
    frame.clear();Layout layout;
    return Inspect(wire,ids,map,save,layout) && EncodeBound(peer,checkpoint,Scope::Pair,wire,frame);
}
inline bool Decode(const Crossplay::Peer& peer,const CrossplayResumeTransfer::Checkpoint& checkpoint,
                   Connections source,Connections target,const std::string& map,const std::string& save,
                   const std::vector<unsigned char>& frame,std::vector<unsigned char>& wire) {
    wire.clear();std::vector<unsigned char> decoded;Layout layout;
    if(!CrossplaySessionConfig::Distinct(target) || !ReadFrame(peer,checkpoint,Scope::Pair,frame,decoded)
        || !Inspect(decoded,source,map,save,layout))return false;
    // Only original routing CIDs change. All saved context, Ready, privacy and
    // original AI/closed slot state bytes remain untouched.
    CrossplayChat::Put(decoded.data()+8,target.host);
    CrossplayChat::Put(decoded.data()+layout.slots_at+6,target.host);
    CrossplayChat::Put(decoded.data()+layout.slots_at+CrossplaySessionConfig::SlotBytes+6,target.guest);
    if(!Inspect(decoded,target,map,save,layout))return false;
    wire=std::move(decoded);return true;
}
inline bool EncodeRoom(const Crossplay::Peer& peer,const CrossplayResumeTransfer::Checkpoint& checkpoint,
                       const CrossplaySessionConfig::RoomConnections& source,const std::string& map,const std::string& save,
                       const std::vector<unsigned char>& wire,std::vector<unsigned char>& frame) {
    frame.clear();Layout layout;
    return CrossplayResumeStart::SlotName(save) && CrossplaySessionConfig::RoomDetail::Inspect(wire,source,layout,false,save)
        && layout.map==map && EncodeBound(peer,checkpoint,Scope::Room,wire,frame);
}
inline bool DecodeRoom(const Crossplay::Peer& peer,const CrossplayResumeTransfer::Checkpoint& checkpoint,
                       const CrossplaySessionConfig::RoomConnections& source,
                       const CrossplaySessionConfig::RoomConnections& target,const std::string& map,const std::string& save,
                       const std::vector<unsigned char>& frame,std::vector<unsigned char>& wire) {
    wire.clear();std::vector<unsigned char> decoded;Layout layout;
    return CrossplayResumeStart::SlotName(save) && ReadFrame(peer,checkpoint,Scope::Room,frame,decoded)
        && CrossplaySessionConfig::RoomDetail::Inspect(decoded,source,layout,false,save) && layout.map==map
        && CrossplaySessionConfig::RoomDetail::Project(decoded,source,target,wire,false,save);
}
// Learn the host's advertised checkpoint from this separate saved namespace.
// The channel authenticates its sender; this does NOT validate the actual file
// or grant Ready/start. The original file receiver, complete hashes, NEO parser
// and native consumption receipts must still independently confirm this offer.
template<class DecodeConfiguration>
inline bool DiscoverBound(const Crossplay::Digest& content,const std::string& expected_map,
                          const std::vector<unsigned char>& frame,DecodeConfiguration decode,
                          CrossplayResumeTransfer::Checkpoint& checkpoint,std::string& save,
                          std::vector<unsigned char>& wire){
    checkpoint={};save.clear();wire.clear();
    if(!CrossplayResumeTransfer::Nonzero(content) || !CrossplaySessionConfig::RelativeMap(expected_map)
        || frame.size()<Header+13 || frame.size()>Header+CrossplaySessionConfig::MaximumWire
        || CrossplayChat::Word(frame.data()+184)>1)return false;
    CrossplayResumeTransfer::Checkpoint advertised{};const auto* p=frame.data()+40;
    memcpy(advertised.content.data(),p,32);memcpy(advertised.setup.data(),p+32,32);
    memcpy(advertised.file.data(),p+64,32);memcpy(advertised.neo.data(),p+96,32);
    advertised.logic_turn=CrossplayChat::Word(p+128);advertised.replay_turn=CrossplayChat::Word(p+132);
    advertised.bytes=CrossplayChat::Word(p+136);advertised.features=CrossplayChat::Word(p+140);
    advertised.enabled=CrossplayChat::Word(p+144)!=0;
    if(advertised.content!=content || !CrossplayResumeTransfer::Valid(advertised))return false;
    const std::vector<unsigned char> native(frame.begin()+Header,frame.end());
    size_t at=13;std::string map,token;std::vector<unsigned char> decoded;
    if(!CrossplaySessionConfig::String(native,at,map) || map!=expected_map
        || !CrossplaySessionConfig::String(native,at,token) || !CrossplayResumeStart::SlotName(token)
        || !decode(advertised,token,decoded))return false;
    checkpoint=advertised;save=std::move(token);wire=std::move(decoded);return true;
}
inline bool Discover(const Crossplay::Peer& peer,const Crossplay::Digest& content,
                     Connections source,Connections target,const std::string& expected_map,
                     const std::vector<unsigned char>& frame,
                     CrossplayResumeTransfer::Checkpoint& checkpoint,std::string& save,
                     std::vector<unsigned char>& wire){
    return DiscoverBound(content,expected_map,frame,[&](const auto& advertised,const auto& token,auto& decoded){
        return peer.edition==Crossplay::Edition::Gold
            && Decode(peer,advertised,source,target,expected_map,token,frame,decoded);
    },checkpoint,save,wire);
}
inline bool DiscoverRoom(const Crossplay::Peer& host,const Crossplay::Digest& content,
                         const CrossplaySessionConfig::RoomConnections& source,
                         const CrossplaySessionConfig::RoomConnections& target,const std::string& expected_map,
                         const std::vector<unsigned char>& frame,CrossplayResumeTransfer::Checkpoint& checkpoint,
                         std::string& save,std::vector<unsigned char>& wire){
    return DiscoverBound(content,expected_map,frame,[&](const auto& advertised,const auto& token,auto& decoded){
        return DecodeRoom(host,advertised,source,target,expected_map,token,frame,decoded);
    },checkpoint,save,wire);
}
}
