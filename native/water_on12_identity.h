#pragma once
#include <windows.h>
#include <bcrypt.h>
#include <intrin.h>
#include <float.h>
#include <cstring>
#include <cwchar>
#include <string>

namespace WaterOn12Identity {
constexpr char Game[]="4ef51632a8a193050ebe8ef3f3fe38a9b240b4aa57ff62a4f381bc0f539df64f";
constexpr char Stub[]="b8b878e29c2931d6a8d064665c4c93edb28a0105152e7247f8f543c1603985c3";
constexpr char Wrapper[]="23a2889e0785a02f17f1ee764254bf22b937fc5bdc6ed6374201a9f50cc7b43d";
constexpr char Base[]="adaedb323c7e79abd758ac7237d9ed5eb6cdb43a95ef6329c4b88bbcfb2c7c0d";
constexpr char Vision[]="d950f1bdb11d83a9d4c6c7e1937b7d7ef747dff884e4e6b6e70e72ce7ed330a1";
struct Guard {
    alignas(16) unsigned char fp[512];DWORD error=GetLastError();
    Guard(){_fxsave(fp);unsigned ignored=0;_controlfp_s(&ignored,_MCW_EM|_RC_NEAR,_MCW_EM|_MCW_RC);}
    ~Guard(){_fxrstor(fp);SetLastError(error);}
};
inline bool Hash(const void* bytes,ULONG count,std::string& text) {
    BCRYPT_ALG_HANDLE alg=nullptr;BCRYPT_HASH_HANDLE hash=nullptr;unsigned char value[32]{};
    bool okay=BCryptOpenAlgorithmProvider(&alg,BCRYPT_SHA256_ALGORITHM,nullptr,0)>=0
        &&BCryptCreateHash(alg,&hash,nullptr,0,nullptr,0,0)>=0;
    if(okay)okay=BCryptHashData(hash,const_cast<PUCHAR>(static_cast<const unsigned char*>(bytes)),count,0)>=0
        &&BCryptFinishHash(hash,value,32,0)>=0;
    if(hash)BCryptDestroyHash(hash);if(alg)BCryptCloseAlgorithmProvider(alg,0);
    if(okay){const char* hex="0123456789abcdef";text.clear();for(auto c:value){text+=hex[c>>4];text+=hex[c&15];}}
    return okay;
}
inline bool Parents(const std::wstring& path) {
    for(size_t i=3;i<path.size();++i)if(path[i]==L'\\') {
        const DWORD attr=GetFileAttributesW(path.substr(0,i).c_str());
        if(attr==INVALID_FILE_ATTRIBUTES||(attr&FILE_ATTRIBUTE_REPARSE_POINT))return false;
    }
    return true;
}
inline bool Pin(const wchar_t* path,const char* expected) {
    if(!Parents(path))return false;
    HANDLE file=CreateFileW(path,GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,
        FILE_FLAG_OPEN_REPARSE_POINT|FILE_FLAG_SEQUENTIAL_SCAN,nullptr);
    if(file==INVALID_HANDLE_VALUE)return false;
    BY_HANDLE_FILE_INFORMATION info{};LARGE_INTEGER size{};
    bool okay=GetFileInformationByHandle(file,&info)&&!(info.dwFileAttributes&FILE_ATTRIBUTE_REPARSE_POINT)
        &&GetFileSizeEx(file,&size)&&size.QuadPart>0&&size.QuadPart<=64*1024*1024;
    BCRYPT_ALG_HANDLE alg=nullptr;BCRYPT_HASH_HANDLE hash=nullptr;unsigned char digest[32]{},buffer[16384]{};DWORD count=0;
    okay=okay&&BCryptOpenAlgorithmProvider(&alg,BCRYPT_SHA256_ALGORITHM,nullptr,0)>=0
        &&BCryptCreateHash(alg,&hash,nullptr,0,nullptr,0,0)>=0;
    while(okay){if(!ReadFile(file,buffer,sizeof(buffer),&count,nullptr)){okay=false;break;}if(!count)break;okay=BCryptHashData(hash,buffer,count,0)>=0;}
    okay=okay&&BCryptFinishHash(hash,digest,32,0)>=0;
    if(hash)BCryptDestroyHash(hash);if(alg)BCryptCloseAlgorithmProvider(alg,0);CloseHandle(file);
    std::string text;const char* hex="0123456789abcdef";
    for(auto c:digest){text+=hex[c>>4];text+=hex[c&15];}
    return okay&&text==expected;
}
inline bool Module(HMODULE module,const char* pin) {
    wchar_t path[512]{};const DWORD count=module?GetModuleFileNameW(module,path,512):0;
    return count&&count<512&&Pin(path,pin);
}
inline bool Range(const void* pointer,size_t bytes) {
    const auto start=reinterpret_cast<ULONG_PTR>(pointer);
    if(start<0x10000||start>=0xffff0000||bytes>0xffff0000-start)return false;
    const auto end=start+bytes;auto at=start;
    while(at<end) {
        MEMORY_BASIC_INFORMATION m{};
        if(VirtualQuery(reinterpret_cast<const void*>(at),&m,sizeof(m))!=sizeof(m)||m.State!=MEM_COMMIT
            ||(m.Protect&(PAGE_GUARD|PAGE_NOACCESS)))return false;
        const auto next=reinterpret_cast<ULONG_PTR>(m.BaseAddress)+m.RegionSize;
        if(next<=at)return false;at=next<end?next:end;
    }
    return true;
}
inline bool Belongs(const void* pointer,HMODULE module) {
    MEMORY_BASIC_INFORMATION m{};
    return VirtualQuery(pointer,&m,sizeof(m))==sizeof(m)&&m.State==MEM_COMMIT&&m.Type==MEM_IMAGE&&m.AllocationBase==module;
}
}
