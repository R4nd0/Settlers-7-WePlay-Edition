#pragma once
#include "crossplay_ai_preparation_codec.h"
#include "crossplay_gold_closed_preparation.h"

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

inline bool AddedSeat(const std::vector<unsigned char>& before,const std::vector<unsigned char>& after,
                      const CrossplaySessionConfig::RoomConnections& routes,uint32_t& slot) {
    slot=0;
    uint32_t previous=0,current=0;
    CrossplaySessionConfig::Layout old_layout,new_layout;
    if(before.size()!=after.size() || !Seat(before,routes,previous) || previous
        || !Seat(after,routes,current) || !current
        || !CrossplaySessionConfig::InspectRoom(before,routes,old_layout)
        || !CrossplaySessionConfig::InspectRoom(after,routes,new_layout)
        || old_layout.slots_at!=new_layout.slots_at)return false;
    const auto at=old_layout.slots_at+(current-1)*CrossplaySessionConfig::SlotBytes;
    for(size_t i=0;i<before.size();++i)
        if((i<at || i>=at+CrossplaySessionConfig::SlotBytes) && before[i]!=after[i])return false;
    for(size_t i=0;i<routes.capacity;++i)
        if(routes.stations[i]!=UINT32_MAX && routes.stations[i]!=routes.host
            && before[old_layout.slots_at+i*CrossplaySessionConfig::SlotBytes])return false;
    slot=current;return true;
}

inline bool ChoosingSeat(const std::vector<unsigned char>& before,const std::vector<unsigned char>& after,
                         const CrossplaySessionConfig::RoomConnections& routes) {
    using namespace CrossplaySessionConfig;
    Layout old_layout,new_layout;
    if(before.size()!=after.size() || !InspectRoom(before,routes,old_layout)
        || !InspectRoom(after,routes,new_layout) || old_layout.slots_at!=new_layout.slots_at)return false;
    for(size_t seat=2;seat<routes.capacity && seat<4;++seat){
        if(routes.stations[seat]!=UINT32_MAX)continue;
        const auto at=old_layout.slots_at+seat*SlotBytes;
        if(before[at+10]!=1 || after[at+10]!=2 || before[at+5]!=after[at+5])continue;
        return std::equal(before.begin(),before.begin()+at,after.begin())
            && std::equal(before.begin()+at+SlotBytes,before.end(),after.begin()+at+SlotBytes);
    }
    return false;
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

inline bool GoldCatalog(const Value& value) {
    using CrossplayNativeCodec::Detail::Readable;
    const auto* callback=reinterpret_cast<const unsigned char*>(0x5b4b80);
    if(!Readable(callback,0x117))return false;
    uint64_t hash=14695981039346656037ull;
    for(size_t i=0;i<0x117;++i)hash=(hash^callback[i])*1099511628211ull;
    if(hash!=0x21e0589903e40b1cull)return false;
    const auto* global=reinterpret_cast<const unsigned char*>(0x10f835c);
    if(!Readable(global,4))return false;
    const auto* root=reinterpret_cast<const unsigned char*>(CrossplayChat::Word(global));
    if(!Readable(root,0x1c8))return false;
    const auto* profiles=reinterpret_cast<const unsigned char*>(CrossplayChat::Word(root+0x1c4));
    const auto* resources=reinterpret_cast<const unsigned char*>(CrossplayChat::Word(root+0x188));
    if(!Readable(profiles,12) || !Readable(resources,0xab4))return false;
    const auto first=CrossplayChat::Word(profiles+4),last=CrossplayChat::Word(profiles+8);
    if(last<=first || (last-first)%4 || last-first>256*4 || !Readable(reinterpret_cast<void*>(first),last-first))return false;
    unsigned matches=0;
    for(auto at=first;at!=last;at+=4){
        const auto* id=reinterpret_cast<const unsigned char*>(CrossplayChat::Word(reinterpret_cast<const unsigned char*>(at)));
        if(!Readable(id,4))return false;
        if(CrossplayChat::Word(id)==value.profile)++matches;
    }
    const auto definitions=CrossplayChat::Word(resources+0xab0);
    const auto offset=value.profile*0x8c;
    if(matches!=1 || !definitions || definitions>UINT32_MAX-offset-0x8c)return false;
    const auto* entry=reinterpret_cast<const unsigned char*>(definitions+offset);
    if(!Readable(entry,0x8c) || !entry[0x6c])return false;
    Crossplay::Digest catalog{};
    return CrossplayAIPreparation::Detail::Catalog(value.profile,CrossplayChat::Word(entry+0x68),entry+0x48,catalog)
        && catalog==value.catalog;
}

inline bool GoldIdentity(const unsigned char* rows,uint32_t slot) {
    const auto* row=rows+slot*0x5c4;
    return CrossplayChat::Word(row+0x5c)==slot && CrossplayChat::Word(row+0x64)==2;
}

template<class ReadRows>
inline bool ApplyGoldIdentity(const Value& value,ReadRows read_rows) {
    if(!CrossplayAIPreparation::Valid(value) || !CrossplayGoldClosedPreparation::Methods())return false;
    unsigned char* rows=nullptr;
    CrossplayAIProfile::ContextSnapshot context;
    if(!read_rows(rows,context))return true;
    if(GoldIdentity(rows,value.slot))return true;
    constexpr size_t stride=0x5c4,bytes=25*stride;
    const size_t offset=value.slot*stride;
    auto* row=rows+offset;
    const auto slot=CrossplayChat::Word(row+0x5c),kind=CrossplayChat::Word(row+0x64);
    if((slot!=UINT32_MAX && slot!=value.slot) || (kind!=1 && kind!=2)
        || CrossplayChat::Word(row+0x60)!=1)return false;
    // Gold's original Lua profile method copies its name and home team only
    // once the target row has native AI kind. Verify the catalog first.
    if(!GoldCatalog(value))return false;
    using CrossplayNative::HistoryRoleDetail::Writable;
    if(!Writable(row+0x5c,4) || !Writable(row+0x64,4))return false;
    std::vector<unsigned char> expected(rows,rows+bytes);
    CrossplayChat::Put(expected.data()+offset+0x5c,value.slot);
    CrossplayChat::Put(expected.data()+offset+0x64,2);
    CrossplayGoldClosedPreparation::SetWith(0x5a8e00,row,value.slot);
    CrossplayGoldClosedPreparation::SetWith(0x5b2000,row,2);
    unsigned char* next=nullptr;
    CrossplayAIProfile::ContextSnapshot after;
    if(!read_rows(next,after) || next!=rows || !CrossplayAIProfile::Same(context,after)
        || memcmp(expected.data(),next,bytes))return false;
    return true;
}
}
