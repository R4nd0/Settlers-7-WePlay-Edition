#pragma once
#include "crossplay_ai_preparation_codec.h"

namespace CrossplayRoomAI {
constexpr unsigned char Offer[]={'S','7','R','A','I','P',0,1};
constexpr size_t Header=sizeof(Offer)+Crossplay::Digest{}.size();
constexpr size_t Size=Header+16+Crossplay::Digest{}.size();
using CrossplayAIPreparation::Value;

inline bool Seat(const std::vector<unsigned char>& config,const CrossplaySessionConfig::RoomConnections& routes,
                 uint32_t& slot) {
    slot=0;
    CrossplaySessionConfig::Layout layout;
    if(!CrossplaySessionConfig::InspectRoom(config,routes,layout))return false;
    for(size_t i=0;i<routes.capacity;++i){
        if(routes.stations[i]!=UINT32_MAX || config[layout.slots_at+i*CrossplaySessionConfig::SlotBytes+10]!=4)continue;
        if(slot || i<2 || i>3)return false;
        slot=static_cast<uint32_t>(i+1);
    }
    return true;
}

inline bool Frame(const Value& value,const Crossplay::Digest& config,std::vector<unsigned char>& frame) {
    frame.clear();
    if(!CrossplayAIPreparation::Valid(value))return false;
    frame.resize(Size);
    memcpy(frame.data(),Offer,sizeof(Offer));
    memcpy(frame.data()+sizeof(Offer),config.data(),config.size());
    CrossplayChat::Put(frame.data()+Header,value.slot);
    CrossplayChat::Put(frame.data()+Header+4,value.profile);
    CrossplayChat::Put(frame.data()+Header+8,value.difficulty);
    CrossplayChat::Put(frame.data()+Header+12,value.peaceful);
    memcpy(frame.data()+Header+16,value.catalog.data(),value.catalog.size());
    return true;
}

inline bool Decode(const std::vector<unsigned char>& frame,const Crossplay::Digest& config,uint32_t slot,Value& value) {
    value={};
    if(frame.size()!=Size || memcmp(frame.data(),Offer,sizeof(Offer))
        || memcmp(frame.data()+sizeof(Offer),config.data(),config.size()))return false;
    Value candidate{CrossplayChat::Word(frame.data()+Header),CrossplayChat::Word(frame.data()+Header+4),
        CrossplayChat::Word(frame.data()+Header+8),CrossplayChat::Word(frame.data()+Header+12),{}};
    memcpy(candidate.catalog.data(),frame.data()+Header+16,candidate.catalog.size());
    if(candidate.slot!=slot || !CrossplayAIPreparation::Valid(candidate))return false;
    value=candidate;
    return true;
}

inline bool ReadSelected(Crossplay::Edition edition,unsigned capacity,uint32_t slot,Value& value) {
    value={};
    if(slot<3 || slot>4 || slot>capacity)return false;
    CrossplayAIPreparationSource::View view;
    unsigned stage=0;
    if(!CrossplayAIPreparationSource::CaptureView(edition,capacity,view,stage) || view.getrows!=view.rows)return false;
    const auto row=CrossplayAIPreparationSource::ReadRow(view,slot,true);
    if(row.slot!=slot || row.kind!=2 || row.controller!=1)return false;
    const auto* native=view.rows+slot*view.row_size;
    Value selected{slot,row.profile,row.difficulty,row.peaceful,{}};
    if(!CrossplayAIPreparation::Detail::Catalog(row.profile,CrossplayChat::Word(native+0x30),native+0x1c,selected.catalog)
        || !CrossplayAIPreparation::Valid(selected) || !view.Stable())return false;
    value=selected;
    return true;
}

inline bool Same(const Value& first,const Value& second) {
    return first.slot==second.slot && first.profile==second.profile && first.difficulty==second.difficulty
        && first.peaceful==second.peaceful && first.catalog==second.catalog;
}
}
