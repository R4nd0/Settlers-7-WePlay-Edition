#pragma once
#include "crossplay_resume_neo.h"
#include "crossplay_session_config.h"
#include <sstream>
#include <memory>

// Read the selected local archive's metadata. This grants neither native Ready
// nor world-start permission; the original native stream still needs its own
// checkpoint digest and receive/consumption checks.
namespace CrossplayResumeSource {
constexpr size_t MaximumArchiveBytes=128u*1024u*1024u;
struct ArchiveIdentity {Crossplay::Digest sha256{};uint64_t bytes=0;};
inline bool ArchiveHash(const unsigned char* data,size_t size,Crossplay::Digest& output){
    output={};if(!data || size<80 || size>MaximumArchiveBytes)return false;
    BCRYPT_ALG_HANDLE algorithm=nullptr;BCRYPT_HASH_HANDLE hash=nullptr;
    bool okay=BCryptOpenAlgorithmProvider(&algorithm,BCRYPT_SHA256_ALGORITHM,nullptr,0)>=0
        && BCryptCreateHash(algorithm,&hash,nullptr,0,nullptr,0,0)>=0;
    if(okay)okay=BCryptHashData(hash,const_cast<PUCHAR>(data),static_cast<ULONG>(size),0)>=0
        && BCryptFinishHash(hash,output.data(),static_cast<ULONG>(output.size()),0)>=0;
    if(hash)BCryptDestroyHash(hash);if(algorithm)BCryptCloseAlgorithmProvider(algorithm,0);
    if(!okay)output={};return okay;
}
inline bool Valid(const ArchiveIdentity& identity){
    unsigned nonzero=0;for(auto byte:identity.sha256)nonzero|=byte;
    return nonzero && identity.bytes>=80 && identity.bytes<=MaximumArchiveBytes;
}
inline bool Inspect(const unsigned char* data,size_t size,const ArchiveIdentity& identity,
                    const CrossplayResumeTransfer::Checkpoint& checkpoint,
                    std::string& serialized,std::string& payload){
    serialized.clear();payload.clear();
    if(!data || !Valid(identity) || size!=identity.bytes || !CrossplayResumeTransfer::Valid(checkpoint))return false;
    Crossplay::Digest actual{};
    if(!ArchiveHash(data,size,actual) || actual!=identity.sha256)return false;
    try{
        const std::string bytes(reinterpret_cast<const char*>(data),size);
        std::istringstream input(bytes,std::ios::in|std::ios::binary);
        const auto members=neo_save::Reader(input).Members();
        if(neo_save::LogicTurn(members[1])!=checkpoint.logic_turn)return false;
        bool found=false;auto value=neo_save::Cache{members[0]}.Neo(found);std::string decoded;
        if(!found || !CrossplayResumeNeo::Inspect(checkpoint,value,decoded))return false;
        serialized=std::move(value);payload=std::move(decoded);return true;
    }catch(const std::exception&){serialized.clear();payload.clear();return false;}
}
inline bool ReadArchive(const std::wstring& path,std::vector<unsigned char>& output,const ArchiveIdentity* expected=nullptr,HANDLE* retained=nullptr){
    struct ErrorScope {DWORD value=GetLastError();~ErrorScope(){SetLastError(value);}} preserve;
    output.clear();if(retained)*retained=INVALID_HANDLE_VALUE;
    // The caller supplies the resolved original save location. Never interpret
    // a relative path against a game process's changing working directory.
    if(path.size()<4 || path.find(L'\0')!=std::wstring::npos
        || !((path[0]>=L'A' && path[0]<=L'Z') || (path[0]>=L'a' && path[0]<=L'z'))
        || path[1]!=L':' || (path[2]!=L'\\' && path[2]!=L'/')
        || (expected && !Valid(*expected)))return false;
    struct File {HANDLE value=INVALID_HANDLE_VALUE;~File(){if(value!=INVALID_HANDLE_VALUE)CloseHandle(value);}} file;
    // Deny writers and replacement until hashing and parsing have finished.
    file.value=CreateFileW(path.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,
                           FILE_ATTRIBUTE_NORMAL|FILE_FLAG_OPEN_REPARSE_POINT|FILE_FLAG_SEQUENTIAL_SCAN,nullptr);
    if(file.value==INVALID_HANDLE_VALUE)return false;
    BY_HANDLE_FILE_INFORMATION info{};LARGE_INTEGER size{};
    if(GetFileType(file.value)!=FILE_TYPE_DISK || !GetFileInformationByHandle(file.value,&info)
        || (info.dwFileAttributes&(FILE_ATTRIBUTE_DIRECTORY|FILE_ATTRIBUTE_REPARSE_POINT))
        || !GetFileSizeEx(file.value,&size) || size.QuadPart<0
        || size.QuadPart<80 || static_cast<uint64_t>(size.QuadPart)>MaximumArchiveBytes
        || (expected && static_cast<uint64_t>(size.QuadPart)!=expected->bytes))return false;
    try{
        std::vector<unsigned char> bytes(static_cast<size_t>(size.QuadPart));size_t at=0;
        while(at<bytes.size()){
            const auto amount=static_cast<DWORD>((std::min)(bytes.size()-at,size_t(65536)));DWORD read=0;
            if(!ReadFile(file.value,bytes.data()+at,amount,&read,nullptr) || !read || read>amount)return false;
            at+=read;
        }
        output=std::move(bytes);
        if(retained){*retained=file.value;file.value=INVALID_HANDLE_VALUE;}
        return true;
    }catch(const std::exception&){output.clear();return false;}
}
inline bool Read(const std::wstring& path,const ArchiveIdentity& identity,
                 const CrossplayResumeTransfer::Checkpoint& checkpoint,
                 std::string& serialized,std::string& payload){
    struct ErrorScope {DWORD value=GetLastError();~ErrorScope(){SetLastError(value);}} preserve;
    serialized.clear();payload.clear();std::vector<unsigned char> bytes;
    return Valid(identity) && CrossplayResumeTransfer::Valid(checkpoint) && ReadArchive(path,bytes,&identity)
        && Inspect(bytes.data(),bytes.size(),identity,checkpoint,serialized,payload);
}
struct ArchiveLease {
    HANDLE value=INVALID_HANDLE_VALUE;
    ~ArchiveLease(){if(value!=INVALID_HANDLE_VALUE)CloseHandle(value);}
    ArchiveLease()=default;
    ArchiveLease(const ArchiveLease&)=delete;
    ArchiveLease& operator=(const ArchiveLease&)=delete;
};
struct Selected {
    ArchiveIdentity archive{};
    CrossplayResumeTransfer::Checkpoint checkpoint{};
    std::string map,serialized,payload;
    std::shared_ptr<ArchiveLease> lease;
};
// Derive a checkpoint from the actual selected source rather than trusting
// launcher/test-case constants. Setup provenance is domain-separated from the
// native setup signature: it binds this complete saved archive and observed map.
// Native roster/root capture and stream-consumption checks remain mandatory.
inline bool Describe(const unsigned char* data,size_t size,const Crossplay::Digest& content,
                     const std::string& observed_map,Selected& output){
    output={};
    if(!CrossplayResumeTransfer::Nonzero(content) || !CrossplaySessionConfig::RelativeMap(observed_map))return false;
    Selected next;next.archive.bytes=size;next.map=observed_map;
    if(!ArchiveHash(data,size,next.archive.sha256))return false;
    try{
        const std::string bytes(reinterpret_cast<const char*>(data),size);
        std::istringstream input(bytes,std::ios::in|std::ios::binary);
        const auto members=neo_save::Reader(input).MembersWithWorld();
        auto& cp=next.checkpoint;cp.content=content;cp.logic_turn=neo_save::LogicTurn(members[1]);
        if(cp.logic_turn==UINT32_MAX || members[2].size()<80 || members[2].size()>CrossplayResumeTransfer::MaximumFileBytes)return false;
        cp.replay_turn=cp.logic_turn+1;cp.bytes=static_cast<uint32_t>(members[2].size());
        cp.features=CrossplayNeo::Fifo|CrossplayNeo::Intent|CrossplayNeo::PriorityCommands;
        bool found=false;next.serialized=neo_save::Cache{members[0]}.Neo(found);
        if(!found)return false;
        next.payload=neo_save::Pickle{next.serialized}.Payload();neo_logistics::SavedState saved;
        if(!neo_logistics::Decode(next.payload,saved))return false;
        cp.enabled=saved.enabled;
        if(!CrossplayResumeNeo::Hash(members[2].data(),members[2].size(),cp.file)
            || !CrossplayResumeNeo::Hash(reinterpret_cast<const unsigned char*>(next.serialized.data()),next.serialized.size(),cp.neo))return false;
        std::vector<unsigned char> provenance{'S','7','X','S','A','V','E',1};
        provenance.insert(provenance.end(),next.archive.sha256.begin(),next.archive.sha256.end());
        provenance.insert(provenance.end(),observed_map.begin(),observed_map.end());
        if(!CrossplayResumeNeo::Hash(provenance.data(),provenance.size(),cp.setup)
            || !CrossplayResumeNeo::Inspect(cp,next.serialized,next.payload))return false;
        output=std::move(next);return true;
    }catch(const std::exception&){output={};return false;}
}
inline bool CaptureSelected(const std::wstring& path,const Crossplay::Digest& content,
                            const std::string& observed_map,Selected& output){
    struct ErrorScope {DWORD value=GetLastError();~ErrorScope(){SetLastError(value);}} preserve;
    output={};std::vector<unsigned char> bytes;auto lease=std::make_shared<ArchiveLease>();
    if(!ReadArchive(path,bytes,nullptr,&lease->value) || !Describe(bytes.data(),bytes.size(),content,observed_map,output))return false;
    output.lease=std::move(lease);return true;
}
}
