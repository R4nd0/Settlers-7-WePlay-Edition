#pragma once
#include <windows.h>
#include <bcrypt.h>
#include "crossplay_resume_transfer.h"

// Hash only original native file bytes. Verification never generates a native
// acknowledgement or permits a world start. A mismatch permanently excludes it.
namespace CrossplayResumeTransfer {
class FileDigest {
    BCRYPT_ALG_HANDLE algorithm_=nullptr;
    BCRYPT_HASH_HANDLE hash_=nullptr;
    Checkpoint checkpoint_{};
    uint32_t received_=0;
    bool bound_=false,failed_=false,verified_=false;
public:
    FileDigest()=default;
    FileDigest(const FileDigest&)=delete;
    FileDigest& operator=(const FileDigest&)=delete;
    ~FileDigest(){if(hash_)BCryptDestroyHash(hash_);if(algorithm_)BCryptCloseAlgorithmProvider(algorithm_,0);}
    bool Bind(const Checkpoint& checkpoint) {
        if(bound_ || failed_ || !Valid(checkpoint))return false;
        if(BCryptOpenAlgorithmProvider(&algorithm_,BCRYPT_SHA256_ALGORITHM,nullptr,0)<0
            || BCryptCreateHash(algorithm_,&hash_,nullptr,0,nullptr,0,0)<0){failed_=true;return false;}
        checkpoint_=checkpoint;bound_=true;return true;
    }
    bool Add(const std::vector<unsigned char>& wire,uint32_t source) {
        if(!bound_ || failed_)return false;
        if(!Inspect(wire,source,checkpoint_.bytes)){failed_=true;return false;}
        const auto length=CrossplayChat::Word(wire.data()+13);
        if(length>checkpoint_.bytes-received_ || (!length && received_!=checkpoint_.bytes)
            || (verified_ && length)){failed_=true;return false;}
        if(!verified_ && length && BCryptHashData(hash_,const_cast<PUCHAR>(wire.data()+NativeHeader),length,0)<0){failed_=true;return false;}
        received_+=length;
        if(!verified_ && received_==checkpoint_.bytes){
            Crossplay::Digest actual{};
            if(BCryptFinishHash(hash_,actual.data(),static_cast<ULONG>(actual.size()),0)<0
                || actual!=checkpoint_.file){failed_=true;return false;}
            verified_=true;
        }
        return true;
    }
    bool Verified()const{return bound_ && !failed_ && verified_;}
    uint32_t ReceivedBytes()const{return received_;}
};
}
