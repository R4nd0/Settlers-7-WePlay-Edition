#pragma once
#include "crossplay_ai_preparation.h"

namespace CrossplayAIPreparation {
// Policy for an already range-checked original Gold playing row. Unlike human
// network-controlled rows (3), a CPU remains locally controlled (1). The caller
// separately binds the live manager buffer, session, owner and admitted root.
inline bool GoldPlayingRow(const unsigned char* row,size_t size,const Value& expected){
    return row && size==0x5c4 && Valid(expected)
        && CrossplayChat::Word(row+0x18)==expected.profile
        && CrossplayChat::Word(row+0x5c)==expected.slot
        && CrossplayChat::Word(row+0x60)==1 && CrossplayChat::Word(row+0x64)==2
        && CrossplayChat::Word(row+0x6c)==expected.difficulty && row[0x70]==expected.peaceful;
}
}
