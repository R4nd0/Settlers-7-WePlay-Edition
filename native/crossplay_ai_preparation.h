#pragma once
#include "crossplay_ai_profile.h"
#include <algorithm>

// Narrow four-slot preparation record. The caller authenticates the channel;
// framing alone supplies no ownership, setup agreement or start permission.
namespace CrossplayAIPreparation {
inline constexpr unsigned char Magic[8]={'S','7','A','I','P','R','E',1};
struct Value {uint32_t slot=0,profile=0,difficulty=0,peaceful=0;Crossplay::Digest catalog{};};
inline bool Valid(const Value& v) {
 return v.slot>=3 && v.slot<=4 && v.profile>=1 && v.profile<=255 && v.difficulty<=2 && v.peaceful<=1
  && std::any_of(v.catalog.begin(),v.catalog.end(),[](auto b){return b!=0;});
}
inline bool Frame(const Crossplay::Peer& peer,uint32_t revision,const Value& v,std::vector<unsigned char>& frame) {
 frame.clear();if(!Valid(v) || revision<1 || revision>2 || v.slot!=revision+2)return false;
 frame.resize(92);memcpy(frame.data(),Magic,8);memcpy(frame.data()+8,peer.local_id.data(),16);memcpy(frame.data()+24,peer.remote_id.data(),16);
 CrossplayChat::Put(frame.data()+40,revision);CrossplayChat::Put(frame.data()+44,v.slot);CrossplayChat::Put(frame.data()+48,v.profile);
 CrossplayChat::Put(frame.data()+52,v.difficulty);CrossplayChat::Put(frame.data()+56,v.peaceful);memcpy(frame.data()+60,v.catalog.data(),32);return true;
}
inline bool Receive(const Crossplay::Peer& peer,uint32_t previous,const std::vector<unsigned char>& frame,Value& out) {
 out={};if(previous>=2 || frame.size()!=92 || memcmp(frame.data(),Magic,8) || memcmp(frame.data()+8,peer.remote_id.data(),16)
  || memcmp(frame.data()+24,peer.local_id.data(),16) || CrossplayChat::Word(frame.data()+40)!=previous+1)return false;
 Value v{CrossplayChat::Word(frame.data()+44),CrossplayChat::Word(frame.data()+48),CrossplayChat::Word(frame.data()+52),CrossplayChat::Word(frame.data()+56),{}};
 memcpy(v.catalog.data(),frame.data()+60,32);if(!Valid(v) || v.slot!=previous+3)return false;out=v;return true;
}
}
