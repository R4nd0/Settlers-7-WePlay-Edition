#pragma once
#include "crossplay_session_config.h"
#include <algorithm>

namespace CrossplayRoomConfiguration {
inline constexpr unsigned char Offer[]={ 'S','7','R','C','F','G',0,1 };
inline constexpr unsigned char Applied[]={ 'S','7','R','C','F','G',1,1 };
inline constexpr unsigned char Committed[]={ 'S','7','R','C','F','G',2,1 };
constexpr size_t Header=8+1+4+4*CrossplaySessionConfig::Slots;
using Connections=CrossplaySessionConfig::RoomConnections;

inline bool PackRoutes(const unsigned char (&kind)[8],const Connections& routes,
                       const std::vector<unsigned char>& payload,std::vector<unsigned char>& frame) {
    frame.clear();
    if(!CrossplaySessionConfig::ValidRoomConnections(routes) || payload.empty())return false;
    frame.assign(kind,kind+8);
    frame.resize(Header);
    frame[8]=routes.capacity;
    CrossplayChat::Put(frame.data()+9,routes.host);
    for(size_t i=0;i<routes.stations.size();++i)
        CrossplayChat::Put(frame.data()+13+i*4,routes.stations[i]);
    frame.insert(frame.end(),payload.begin(),payload.end());
    return true;
}
inline bool UnpackRoutes(const unsigned char (&kind)[8],const std::vector<unsigned char>& frame,
                         size_t payload_limit,Connections& routes,std::vector<unsigned char>& payload) {
    routes={};
    payload.clear();
    if(frame.size()<=Header || frame.size()-Header>payload_limit || memcmp(frame.data(),kind,8))return false;
    Connections candidate;
    candidate.capacity=frame[8];
    candidate.host=CrossplayChat::Word(frame.data()+9);
    for(size_t i=0;i<candidate.stations.size();++i)
        candidate.stations[i]=CrossplayChat::Word(frame.data()+13+i*4);
    if(!CrossplaySessionConfig::ValidRoomConnections(candidate))return false;
    routes=candidate;
    payload.assign(frame.begin()+Header,frame.end());return true;
}
inline bool Encode(const Connections& routes,const std::vector<unsigned char>& wire,
        std::vector<unsigned char>& frame) {
    frame.clear();CrossplaySessionConfig::Layout layout;
    return CrossplaySessionConfig::InspectRoom(wire,routes,layout) && PackRoutes(Offer,routes,wire,frame);
}
inline bool Decode(const std::vector<unsigned char>& frame,Connections& routes,std::vector<unsigned char>& wire) {
    routes={};wire.clear();
    Connections candidate;
    std::vector<unsigned char> bytes;
    CrossplaySessionConfig::Layout layout;
    if(!UnpackRoutes(Offer,frame,CrossplaySessionConfig::MaximumWire,candidate,bytes)
        || !CrossplaySessionConfig::InspectRoom(bytes,candidate,layout))return false;
    routes=candidate;
    wire=std::move(bytes);
    return true;
}
inline bool Acknowledge(const std::vector<unsigned char>& offer,const Connections& observed_routes,
        const std::vector<unsigned char>& observed,std::vector<unsigned char>& receipt) {
    receipt.clear();
    Connections routes;
    std::vector<unsigned char> expected,projected;
    if(!Decode(offer,routes,expected)
        || !CrossplaySessionConfig::ProjectRoom(observed,observed_routes,routes,projected)
        || projected!=expected)return false;
    receipt=offer;
    std::copy(std::begin(Applied),std::end(Applied),receipt.begin());
    return true;
}
inline bool Matches(const std::vector<unsigned char>& offer,const std::vector<unsigned char>& frame,
        const unsigned char (&kind)[8]) {
    return offer.size()>=Header && frame.size()==offer.size()
        && !memcmp(offer.data(),Offer,8) && !memcmp(frame.data(),kind,8)
        && std::equal(frame.begin()+8,frame.end(),offer.begin()+8);
}
inline bool MatchesReceipt(const std::vector<unsigned char>& offer,const std::vector<unsigned char>& frame) {
    return Matches(offer,frame,Applied);
}
inline bool Commit(const std::vector<unsigned char>& offer,const std::vector<unsigned char>& checked,
        std::vector<unsigned char>& frame) {
    if(!MatchesReceipt(offer,checked))return false;
    frame=checked;
    std::copy(std::begin(Committed),std::end(Committed),frame.begin());
    return true;
}
inline bool MatchesCommit(const std::vector<unsigned char>& offer,const std::vector<unsigned char>& frame) {
    return Matches(offer,frame,Committed);
}
}
