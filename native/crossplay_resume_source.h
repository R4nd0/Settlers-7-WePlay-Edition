#pragma once
#include "crossplay_resume_neo.h"
#include <sstream>

// Read the selected local archive's metadata. This grants neither native Ready
// nor world-start permission; the original native stream still needs its own
// checkpoint digest and receive/consumption checks.
namespace CrossplayResumeSource {
constexpr size_t MaximumArchiveBytes=128u*1024u*1024u;
struct ArchiveIdentity {Crossplay::Digest sha256{};uint64_t bytes=0;};
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
    if(!CrossplayResumeNeo::Hash(data,size,actual) || actual!=identity.sha256)return false;
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
inline bool Read(const std::wstring& path,const ArchiveIdentity& identity,
                 const CrossplayResumeTransfer::Checkpoint& checkpoint,
                 std::string& serialized,std::string& payload){
    struct ErrorScope {DWORD value=GetLastError();~ErrorScope(){SetLastError(value);}} preserve;
    serialized.clear();payload.clear();
    // The caller supplies the resolved original save location. Never interpret
    // a relative path against a game process's changing working directory.
    if(path.size()<4 || path.find(L'\0')!=std::wstring::npos
        || !((path[0]>=L'A' && path[0]<=L'Z') || (path[0]>=L'a' && path[0]<=L'z'))
        || path[1]!=L':' || (path[2]!=L'\\' && path[2]!=L'/')
        || !Valid(identity) || !CrossplayResumeTransfer::Valid(checkpoint))return false;
    struct File {HANDLE value=INVALID_HANDLE_VALUE;~File(){if(value!=INVALID_HANDLE_VALUE)CloseHandle(value);}} file;
    // Deny writers and replacement until hashing and parsing have finished.
    file.value=CreateFileW(path.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,
                           FILE_ATTRIBUTE_NORMAL|FILE_FLAG_OPEN_REPARSE_POINT|FILE_FLAG_SEQUENTIAL_SCAN,nullptr);
    if(file.value==INVALID_HANDLE_VALUE)return false;
    BY_HANDLE_FILE_INFORMATION info{};LARGE_INTEGER size{};
    if(GetFileType(file.value)!=FILE_TYPE_DISK || !GetFileInformationByHandle(file.value,&info)
        || (info.dwFileAttributes&(FILE_ATTRIBUTE_DIRECTORY|FILE_ATTRIBUTE_REPARSE_POINT))
        || !GetFileSizeEx(file.value,&size) || size.QuadPart<0
        || static_cast<uint64_t>(size.QuadPart)!=identity.bytes)return false;
    try{
        std::vector<unsigned char> bytes(static_cast<size_t>(identity.bytes));size_t at=0;
        while(at<bytes.size()){
            const auto amount=static_cast<DWORD>((std::min)(bytes.size()-at,size_t(65536)));DWORD read=0;
            if(!ReadFile(file.value,bytes.data()+at,amount,&read,nullptr) || !read || read>amount)return false;
            at+=read;
        }
        return Inspect(bytes.data(),bytes.size(),identity,checkpoint,serialized,payload);
    }catch(const std::exception&){serialized.clear();payload.clear();return false;}
}
}
