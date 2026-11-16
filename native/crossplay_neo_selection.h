#pragma once
#include "crossplay_bootstrap.h"
#include "crossplay_session_config.h"
#include "crossplay_neo_agreement.h"

// Fresh-lobby host selection, separate from full world/root NEO agreement.
// Only the actual staged host choice may be sent. The receiver must first
// capture its applied native configuration/choices and verify this context.
// No native bytes, Ready state, save source or world permission are delivered.
namespace CrossplayNeoSelection {
inline constexpr unsigned char Magic[8]={'S','7','N','S','E','L',0,1};
inline constexpr uint32_t Features=CrossplayNeo::Fifo|CrossplayNeo::Intent|CrossplayNeo::PriorityCommands;
inline constexpr size_t FrameSize=120;
struct Value {uint32_t revision=0;bool enabled=false;Crossplay::Digest content{},configuration{};};
template<size_t N> inline bool Nonzero(const std::array<unsigned char,N>& value){
    for(auto byte:value)if(byte)return true;return false;
}
inline bool Canonical(const std::vector<unsigned char>& actual,CrossplaySessionConfig::Connections ids,
                      const CrossplayBootstrap::Context& choices,std::vector<unsigned char>& output){
    output.clear();CrossplaySessionConfig::Layout layout;
    if(!CrossplayBootstrap::Valid(choices) || choices.saved
        || !CrossplaySessionConfig::Inspect(actual,ids,layout) || layout.map!=choices.map)return false;
    auto copy=actual;
    CrossplayChat::Put(copy.data()+8,1);
    for(unsigned i=0;i<2;++i){
        const auto at=layout.slots_at+i*CrossplaySessionConfig::SlotBytes;
        CrossplayChat::Put(copy.data()+at+6,i+1);
        // Readiness is independently delivered by its original native events.
        // Normalize a detached hash input only; retain every other native field.
        copy[at]=0;copy[at+10]=3;
    }
    CrossplaySessionConfig::Layout checked;
    if(!CrossplaySessionConfig::Inspect(copy,{1,2},checked))return false;
    std::vector<unsigned char> bytes={'S','7','N','C','F','G',0,1};
    auto word=[&](uint32_t value){const auto at=bytes.size();bytes.resize(at+4);CrossplayChat::Put(bytes.data()+at,value);};
    word(choices.flags);word(static_cast<uint32_t>(choices.map_template.size()));
    bytes.insert(bytes.end(),choices.map_template.begin(),choices.map_template.end());
    word(static_cast<uint32_t>(copy.size()));bytes.insert(bytes.end(),copy.begin(),copy.end());
    output=std::move(bytes);return true;
}
inline bool Valid(const Value& value){return value.revision && Nonzero(value.content) && Nonzero(value.configuration);}
inline bool Encode(const Crossplay::Peer& peer,const Value& value,std::vector<unsigned char>& output){
    output.clear();
    if(!Valid(value) || !Nonzero(peer.local_id) || !Nonzero(peer.remote_id) || peer.local_id==peer.remote_id)return false;
    std::vector<unsigned char> frame(FrameSize,0);memcpy(frame.data(),Magic,8);
    memcpy(frame.data()+8,peer.local_id.data(),16);memcpy(frame.data()+24,peer.remote_id.data(),16);
    CrossplayChat::Put(frame.data()+40,Features);CrossplayChat::Put(frame.data()+44,value.revision);
    CrossplayChat::Put(frame.data()+48,value.enabled?1u:0u);
    memcpy(frame.data()+56,value.content.data(),32);memcpy(frame.data()+88,value.configuration.data(),32);
    output=std::move(frame);return true;
}
inline bool Decode(const Crossplay::Peer& peer,const std::vector<unsigned char>& frame,uint32_t previous,
                   const Crossplay::Digest& content,const Crossplay::Digest& configuration,Value& output){
    output={};
    if(frame.size()!=FrameSize || memcmp(frame.data(),Magic,8) || previous==UINT32_MAX
        || !Nonzero(peer.local_id) || !Nonzero(peer.remote_id) || peer.local_id==peer.remote_id
        || memcmp(frame.data()+8,peer.remote_id.data(),16) || memcmp(frame.data()+24,peer.local_id.data(),16)
        || CrossplayChat::Word(frame.data()+40)!=Features || CrossplayChat::Word(frame.data()+44)!=previous+1
        || CrossplayChat::Word(frame.data()+48)>1 || CrossplayChat::Word(frame.data()+52))return false;
    Value value;value.revision=previous+1;value.enabled=frame[48]==1;
    memcpy(value.content.data(),frame.data()+56,32);memcpy(value.configuration.data(),frame.data()+88,32);
    if(!Valid(value) || value.content!=content || value.configuration!=configuration)return false;
    output=value;return true;
}
}
