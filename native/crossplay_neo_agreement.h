#pragma once
#include "crossplay_channel.h"
#include <cstring>

// An agreement on the authenticated channel, never native world-start authority.
// Feature 1 is the experimental shared FIFO port, not History's complete NEO5.
namespace CrossplayNeo {
inline constexpr unsigned char Magic[8]={'S','7','N','E','O','A',0,1};
inline constexpr uint32_t Fifo=1,Intent=2,Distance=4,Pickup=8,PriorityCommands=16,All=31;
inline constexpr size_t FrameSize=152;
struct Selection {
    uint32_t features=0;bool enabled=false;
    Crossplay::Digest content{},setup{},saved_state{};
};
inline bool Valid(const Selection& value) {
    return (value.features&Fifo) && !(value.features&~All)
        && (!(value.features&PriorityCommands) || (value.features&Intent));
}
inline bool Same(const Selection& a,const Selection& b) {
    return a.features==b.features && a.enabled==b.enabled && a.content==b.content
        && a.setup==b.setup && a.saved_state==b.saved_state;
}
inline void Put(std::vector<unsigned char>& frame,size_t at,uint32_t value) {
    for(unsigned i=0;i<4;++i)frame[at+i]=static_cast<unsigned char>(value>>(i*8));
}
inline uint32_t Get(const std::vector<unsigned char>& frame,size_t at) {
    uint32_t value=0;for(unsigned i=0;i<4;++i)value|=static_cast<uint32_t>(frame[at+i])<<(i*8);return value;
}
inline bool Encode(const Crossplay::Peer& peer,uint32_t kind,const Selection& value,std::vector<unsigned char>& output) {
    if(!Valid(value) || (kind!=1 && kind!=2))return false;
    std::vector<unsigned char> frame(FrameSize,0);
    memcpy(frame.data(),Magic,8);memcpy(frame.data()+8,peer.local_id.data(),16);memcpy(frame.data()+24,peer.remote_id.data(),16);
    Put(frame,40,kind);Put(frame,44,value.features);Put(frame,48,value.enabled?1u:0u);
    memcpy(frame.data()+56,value.content.data(),32);memcpy(frame.data()+88,value.setup.data(),32);memcpy(frame.data()+120,value.saved_state.data(),32);
    output=std::move(frame);return true;
}
inline bool Decode(const Crossplay::Peer& peer,const std::vector<unsigned char>& frame,uint32_t& kind,Selection& output) {
    if(frame.size()!=FrameSize || memcmp(frame.data(),Magic,8) || memcmp(frame.data()+8,peer.remote_id.data(),16)
        || memcmp(frame.data()+24,peer.local_id.data(),16) || Get(frame,52) || Get(frame,48)>1)return false;
    const auto parsed_kind=Get(frame,40);if(parsed_kind!=1 && parsed_kind!=2)return false;
    Selection value{};value.features=Get(frame,44);value.enabled=Get(frame,48)==1;
    memcpy(value.content.data(),frame.data()+56,32);memcpy(value.setup.data(),frame.data()+88,32);memcpy(value.saved_state.data(),frame.data()+120,32);
    if(!Valid(value))return false;kind=parsed_kind;output=value;return true;
}
class Agreement {
    Crossplay::Peer peer_{};Selection selection_{};
    bool bound_=false,offered_=false,received_=false,acknowledged_=false;
public:
    bool Bind(const Crossplay::Peer& peer,const Selection& selection) {
        if(bound_ || !Valid(selection) || peer.local_id==peer.remote_id)return false;
        peer_=peer;selection_=selection;bound_=true;return true;
    }
    bool Offer(std::vector<unsigned char>& output) {
        if(!bound_ || offered_ || !Encode(peer_,1,selection_,output))return false;
        offered_=true;return true;
    }
    bool Consume(const std::vector<unsigned char>& frame,std::vector<unsigned char>& reply) {
        reply.clear();uint32_t kind=0;Selection value{};
        if(!bound_ || !offered_ || !Decode(peer_,frame,kind,value) || !Same(selection_,value))return false;
        if(kind==1) {
            if(received_ || !Encode(peer_,2,selection_,reply))return false;
            received_=true;return true;
        }
        if(!received_ || acknowledged_)return false;acknowledged_=true;return true;
    }
    bool Ready()const{return bound_ && offered_ && received_ && acknowledged_;}
    const Selection& Value()const{return selection_;}
};
}
