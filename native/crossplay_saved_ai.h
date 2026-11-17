#pragma once
#include "crossplay_ai_preparation_source.h"
#include "crossplay_saved_session_config.h"
#include "crossplay_saved_players.h"

// Original saved preparation rows, bound by the caller to its retained source.
// Fresh chooser/catalog permissions are deliberately not reused for a load.
namespace CrossplaySavedAI {
struct Row {uint32_t slot=0,profile=0,difficulty=0,peaceful=0;};
inline bool Same(const Row& a,const Row& b){
    return a.slot==b.slot && a.profile==b.profile && a.difficulty==b.difficulty && a.peaceful==b.peaceful;
}
inline bool Valid(const Row& row){return row.slot>=3 && row.slot<=4 && row.profile>=1 && row.profile<=255 && row.difficulty<=2 && row.peaceful<=1;}
// AI authority comes from the retained file's actual saved players. A newly
// created lobby has closed slots and unrelated default preparation profiles.
inline bool FromArchive(const CrossplaySavedPlayers::Snapshot& source,unsigned& mask,std::array<Row,2>& rows){
    mask=0;rows={};if(!source.bytes)return false;unsigned observed=0;std::array<Row,2> result{};
    for(unsigned slot=0;slot<25;++slot){
        const auto& value=source.rows[slot];
        if(slot==1 || slot==2){if(value.kind!=1 || value.controller!=3 || value.index!=slot)return false;continue;}
        if(value.kind==2){
            Row row{slot,value.profile,value.difficulty,value.peaceful};
            if(!Valid(row) || value.controller!=1 || value.index!=slot)return false;
            result[slot-3]=row;observed|=1u<<slot;
        }else if((value.kind!=4 && !(slot>=21 && value.kind==8)) || value.controller || value.index!=UINT32_MAX)return false;
    }
    mask=observed;rows=result;return true;
}
// Preparation-only diagnostic view; never used as saved-file AI authority.
inline bool Describe(const std::vector<unsigned char>& wire,CrossplaySessionConfig::Connections ids,
                     const std::string& map,const std::string& token,
                     const CrossplayAIPreparationSource::Snapshot& source,unsigned& mask,std::array<Row,2>& rows){
    mask=0;rows={};CrossplaySessionConfig::Layout layout;
    if(!source.same || !CrossplaySavedSessionConfig::Inspect(wire,ids,map,token,layout))return false;
    for(unsigned slot=3;slot<=4;++slot){
        const auto at=layout.slots_at+(slot-1)*CrossplaySessionConfig::SlotBytes;
        if(wire[at+10]!=4 && wire[at+10]!=2)continue;
        const auto& getter=source.getter[slot];const auto& writer=source.writer[slot];
        Row row{getter.slot,getter.profile,getter.difficulty,getter.peaceful};
        if(!Valid(row) || row.slot!=slot || getter.controller!=1 || getter.kind!=2
            || writer.slot!=getter.slot || writer.profile!=getter.profile || writer.difficulty!=getter.difficulty
            || writer.peaceful!=getter.peaceful || writer.controller!=1 || writer.kind!=2)return false;
        rows[slot-3]=row;mask|=1u<<slot;
    }
    return true;
}
inline bool Playing(const unsigned char* player,size_t size,const Row& row){
    return player && size==0x5c4 && Valid(row) && CrossplayChat::Word(player+0x18)==row.profile
        && CrossplayChat::Word(player+0x5c)==row.slot && CrossplayChat::Word(player+0x60)==1
        && CrossplayChat::Word(player+0x64)==2 && CrossplayChat::Word(player+0x6c)==row.difficulty && player[0x70]==row.peaceful;
}
}
