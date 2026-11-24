#pragma once
#include "supersampling_hook.h"
#include <cstddef>
#include <cstring>
#include <cwchar>

namespace NativeGraphicsConfig {
constexpr DWORD Observe=1,Water=2,Terrain=4,Vegetation=8,StableShadows=16;
constexpr DWORD KnownFlags=Observe|Water|Terrain|Vegetation|StableShadows;

struct Version2 {
    SupersamplingConfig base;
    DWORD flags;
    wchar_t catalog_path[512];
    unsigned char catalog_sha256[32];
    wchar_t observer_report_path[512];
};
static_assert(sizeof(SupersamplingConfig)==1048,"The version-1 launcher ABI changed.");
static_assert(sizeof(Version2)==3132,"The version-2 launcher ABI changed.");
static_assert(offsetof(Version2,flags)==1048,"Version-2 must retain the version-1 prefix.");

enum class Error {None,Size,Version,Dimensions,Capabilities,Path,ObserverIdentity,UnusedFields};

inline bool Zero(const void* data,size_t size) {
    const auto* bytes=static_cast<const unsigned char*>(data);
    for(size_t i=0;i<size;++i)if(bytes[i])return false;
    return true;
}

inline bool AbsolutePath(const wchar_t (&path)[512],const wchar_t* suffix) {
    if(!path[0] || path[511])return false;
    const size_t length=std::wcslen(path),ending=std::wcslen(suffix);
    if(length<4 || path[1]!=L':' || (path[2]!=L'\\' && path[2]!=L'/')
        || !((path[0]>=L'A' && path[0]<=L'Z') || (path[0]>=L'a' && path[0]<=L'z'))
        || length<ending || _wcsicmp(path+length-ending,suffix))return false;
    for(size_t i=3;i<length;++i) {
        if(path[i]<32 || path[i]==L':' || path[i]==L'*' || path[i]==L'?' || path[i]==L'"' || path[i]==L'|' || path[i]==L'<' || path[i]==L'>')return false;
        const bool begin=i==3 || path[i-1]==L'\\' || path[i-1]==L'/';
        if(begin && path[i]==L'.' && (path[i+1]==L'\\' || path[i+1]==L'/' || !path[i+1]
            || (path[i+1]==L'.' && (path[i+2]==L'\\' || path[i+2]==L'/' || !path[i+2]))))return false;
    }
    return true;
}

inline Error Validate(const Version2& value,DWORD compiled_capabilities) {
    const auto& base=value.base;
    if(base.version!=1 && base.version!=2)return Error::Version;
    if((base.version==1 && base.size!=sizeof(SupersamplingConfig))
        || (base.version==2 && base.size!=sizeof(Version2)))return Error::Size;
    if((value.flags&~KnownFlags) || (value.flags&~compiled_capabilities))return Error::Capabilities;
    if(base.output_width<320 || base.output_height<180 || base.render_width>3840 || base.render_height>2160
        || base.render_width<base.output_width || base.render_height<base.output_height
        || static_cast<unsigned long long>(base.render_width)*base.output_height
            !=static_cast<unsigned long long>(base.render_height)*base.output_width
        || (base.render_width==base.output_width && base.version==1))return Error::Dimensions;
    if(!AbsolutePath(base.report_path,L".json"))return Error::Path;
    if(base.version==1 && (value.flags || !Zero(value.catalog_path,sizeof(value.catalog_path))
        || !Zero(value.catalog_sha256,sizeof(value.catalog_sha256)) || !Zero(value.observer_report_path,sizeof(value.observer_report_path))))return Error::UnusedFields;
    if(value.flags&Observe) {
        if(!AbsolutePath(value.catalog_path,L".bin") || !AbsolutePath(value.observer_report_path,L".json")
            || !_wcsicmp(base.report_path,value.observer_report_path))return Error::Path;
        if(Zero(value.catalog_sha256,sizeof(value.catalog_sha256)))return Error::ObserverIdentity;
    } else if(!Zero(value.catalog_path,sizeof(value.catalog_path)) || !Zero(value.catalog_sha256,sizeof(value.catalog_sha256))
        || !Zero(value.observer_report_path,sizeof(value.observer_report_path)))return Error::UnusedFields;
    return Error::None;
}

inline Error Decode(const void* data,size_t bytes,DWORD compiled_capabilities,Version2& output) {
    if(!data || bytes<sizeof(DWORD)*2)return Error::Size;
    DWORD header[2]{};std::memcpy(header,data,sizeof(header));
    if(header[1]!=1 && header[1]!=2)return Error::Version;
    const size_t expected=header[1]==1?sizeof(SupersamplingConfig):sizeof(Version2);
    if(header[0]!=expected || bytes!=expected)return Error::Size;
    output={};std::memcpy(&output,data,bytes);
    return Validate(output,compiled_capabilities);
}
}
