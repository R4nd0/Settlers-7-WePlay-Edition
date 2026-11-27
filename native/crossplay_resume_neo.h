#pragma once
#include "crossplay_resume_digest.h"
#include "neo_save_archive.h"

// Saved NEO metadata is parsed as data, never executed as Lua. Fragment receipt
// does not create native Ready or admit a saved-world start on its own.
namespace CrossplayResumeNeo {
constexpr size_t Header=CrossplayResumeTransfer::ContextHeader+8,MaximumChunk=32768;
constexpr size_t MaximumSerialized=2*neo_logistics::MaximumStateBytes+256;
constexpr unsigned char Magic[8]={'S','7','R','N','E','O',0,1};
inline bool Hash(const unsigned char* data,size_t size,Crossplay::Digest& output){
    output={};if(!data || !size || size>CrossplayResumeTransfer::MaximumFileBytes)return false;
    BCRYPT_ALG_HANDLE algorithm=nullptr;BCRYPT_HASH_HANDLE hash=nullptr;
    bool okay=BCryptOpenAlgorithmProvider(&algorithm,BCRYPT_SHA256_ALGORITHM,nullptr,0)>=0
        && BCryptCreateHash(algorithm,&hash,nullptr,0,nullptr,0,0)>=0;
    if(okay)okay=BCryptHashData(hash,const_cast<PUCHAR>(data),static_cast<ULONG>(size),0)>=0
        && BCryptFinishHash(hash,output.data(),static_cast<ULONG>(output.size()),0)>=0;
    if(hash)BCryptDestroyHash(hash);if(algorithm)BCryptCloseAlgorithmProvider(algorithm,0);
    if(!okay)output={};return okay;
}
inline bool Inspect(const CrossplayResumeTransfer::Checkpoint& cp,const std::string& serialized,std::string& payload){
    payload.clear();Crossplay::Digest actual{};
    if(!CrossplayResumeTransfer::Valid(cp) || serialized.empty() || serialized.size()>MaximumSerialized
        || !Hash(reinterpret_cast<const unsigned char*>(serialized.data()),serialized.size(),actual) || actual!=cp.neo)return false;
    try{
        auto text=neo_save::Pickle{serialized}.Payload();neo_logistics::SavedState saved;
        if(!neo_logistics::Decode(text,saved) || saved.enabled!=cp.enabled
            || !neo_logistics::MatchesReplay(saved,cp.logic_turn,cp.replay_turn))return false;
        payload=std::move(text);return true;
    }catch(const std::exception&){return false;}
}
inline bool Encode(const Crossplay::Peer& peer,const CrossplayResumeTransfer::Checkpoint& cp,uint32_t sequence,
                   uint32_t offset,const std::string& serialized,std::vector<unsigned char>& frame){
    frame.clear();if(!CrossplayResumeTransfer::Valid(cp) || !sequence || serialized.empty()
        || serialized.size()>MaximumSerialized || offset>=serialized.size())return false;
    const auto amount=(std::min)(MaximumChunk,serialized.size()-offset);
    frame.resize(Header);memcpy(frame.data(),Magic,8);memcpy(frame.data()+8,peer.local_id.data(),16);memcpy(frame.data()+24,peer.remote_id.data(),16);
    CrossplayResumeTransfer::PutCheckpoint(frame.data()+40,cp);CrossplayChat::Put(frame.data()+188,sequence);
    CrossplayChat::Put(frame.data()+192,static_cast<uint32_t>(amount));CrossplayChat::Put(frame.data()+196,offset);
    CrossplayChat::Put(frame.data()+200,static_cast<uint32_t>(serialized.size()));
    frame.insert(frame.end(),serialized.begin()+offset,serialized.begin()+offset+amount);return true;
}
class Receiver {
    CrossplayResumeTransfer::Checkpoint checkpoint_{};std::string data_;
    uint32_t sequence_=0,total_=0;bool bound_=false,failed_=false,complete_=false;
public:
    bool Bind(const CrossplayResumeTransfer::Checkpoint& cp){if(bound_ || failed_ || !CrossplayResumeTransfer::Valid(cp))return false;checkpoint_=cp;bound_=true;return true;}
    bool Accept(const Crossplay::Peer& peer,const std::vector<unsigned char>& frame,std::string& payload){
        payload.clear();if(!bound_ || failed_ || complete_)return false;
        unsigned char expected[148]{};CrossplayResumeTransfer::PutCheckpoint(expected,checkpoint_);
        auto reject=[&](){failed_=true;data_.clear();return false;};
        if(frame.size()<=Header || frame.size()>Header+MaximumChunk || memcmp(frame.data(),Magic,8)
            || memcmp(frame.data()+8,peer.remote_id.data(),16) || memcmp(frame.data()+24,peer.local_id.data(),16)
            || memcmp(frame.data()+40,expected,sizeof(expected)) || sequence_==UINT32_MAX
            || CrossplayChat::Word(frame.data()+188)!=sequence_+1 || CrossplayChat::Word(frame.data()+192)!=frame.size()-Header
            || CrossplayChat::Word(frame.data()+196)!=data_.size())return reject();
        const auto total=CrossplayChat::Word(frame.data()+200);
        if(!total || total>MaximumSerialized || (sequence_ && total!=total_) || data_.size()>total
            || frame.size()-Header>total-data_.size())return reject();
        total_=total;++sequence_;data_.append(reinterpret_cast<const char*>(frame.data()+Header),frame.size()-Header);
        if(data_.size()==total_){if(!Inspect(checkpoint_,data_,payload))return reject();complete_=true;data_.clear();}
        return true;
    }
    bool Complete()const{return bound_ && !failed_ && complete_;}
};
}
