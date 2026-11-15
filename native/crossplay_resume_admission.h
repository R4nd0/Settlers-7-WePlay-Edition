#pragma once
#include "crossplay_resume_transfer.h"
#include "crossplay_session_config.h"
// Adapter receipts for independently observed native file consumption and
// original start queueing. Never decoded into a game event or native flag.
namespace CrossplayResumeAdmission {
constexpr size_t FrameBytes=CrossplayResumeTransfer::ContextHeader;
constexpr unsigned char Magic[8]={'S','7','R','A','D','M',0,1};
enum class Kind:uint32_t { Prepared=1,Queued=2 };
// A range-checked saved cache is not a fresh lobby. The original Gold saved
// start retains loaded AI as state4/Ready1, while a History file consumer still
// has open slots until its native world reader runs. Human +20 is an opaque
// Gold-local field, absent from the original configuration wire; preserve it.
// This predicate grants no native readiness or start permission. The caller
// separately requires the captured original enabled host action and checkpoint.
inline bool SavedSlots(const unsigned char* rows,size_t size,CrossplaySessionConfig::Connections ids,bool host){
    if(!rows || size!=8*0x18 || !CrossplaySessionConfig::Distinct(ids))return false;
    for(size_t i=0;i<8;++i){
        const auto* row=rows+i*0x18;
        const auto state=CrossplayChat::Word(row),slot=CrossplayChat::Word(row+8),station=CrossplayChat::Word(row+16);
        if(host && i>=2 && i<4 && state==4){
            if(row[4]!=1 || row[12] || slot!=i+1 || station || CrossplayChat::Word(row+20))return false;
        }else if(!CrossplaySessionConfig::SlotValid(i,row[4],row[12],slot,station,state,ids,true))return false;
        if(host && i<2){if(row[4]!=1)return false;}
        else if(CrossplayChat::Word(row+20))return false;
    }
    return true;
}
inline bool Encode(const Crossplay::Peer& peer,const CrossplayResumeTransfer::Checkpoint& cp,Kind kind,std::vector<unsigned char>& frame){
    frame.clear();if(!CrossplayResumeTransfer::Valid(cp) || (kind!=Kind::Prepared && kind!=Kind::Queued))return false;
    frame.resize(FrameBytes);memcpy(frame.data(),Magic,8);memcpy(frame.data()+8,peer.local_id.data(),16);memcpy(frame.data()+24,peer.remote_id.data(),16);
    CrossplayResumeTransfer::PutCheckpoint(frame.data()+40,cp);CrossplayChat::Put(frame.data()+188,static_cast<uint32_t>(kind));CrossplayChat::Put(frame.data()+192,0);return true;
}
inline bool Decode(const Crossplay::Peer& peer,const CrossplayResumeTransfer::Checkpoint& cp,const std::vector<unsigned char>& frame,Kind& kind){
    unsigned char expected[148]{};if(!CrossplayResumeTransfer::Valid(cp) || frame.size()!=FrameBytes)return false;
    CrossplayResumeTransfer::PutCheckpoint(expected,cp);const auto value=CrossplayChat::Word(frame.data()+188);
    if(memcmp(frame.data(),Magic,8) || memcmp(frame.data()+8,peer.remote_id.data(),16) || memcmp(frame.data()+24,peer.local_id.data(),16)
        || memcmp(frame.data()+40,expected,sizeof(expected)) || (value!=1 && value!=2) || CrossplayChat::Word(frame.data()+192))return false;
    kind=static_cast<Kind>(value);return true;
}
}
