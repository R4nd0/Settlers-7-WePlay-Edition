#pragma once
#include "crossplay_channel.h"
#include <string>
#include <cwchar>

// Launcher input is configuration, never native session/start authority.
// Keep this parser independent of the native initialization and hook code.
namespace CrossplayProduction {
struct LaunchInput {
    std::wstring activation,role,peer,port,session,key,identity,content,name,profile;
};
struct Launch {
    bool mixed=false,host=false;
    Crossplay::Config channel{};
    std::string name;
    std::wstring profile;
};
inline bool Nonzero(const unsigned char* bytes,size_t size){
    unsigned value=0;for(size_t i=0;i<size;++i)value|=bytes[i];return value!=0;
}
template<size_t N> inline bool Hex(const std::wstring& text,std::array<unsigned char,N>& output){
    output={};if(text.size()!=2*N)return false;
    for(size_t i=0;i<N;++i){
        unsigned value=0;
        for(size_t j=0;j<2;++j){
            const wchar_t c=text[2*i+j];
            if(!((c>=L'0' && c<=L'9') || (c>=L'a' && c<=L'f'))){output={};return false;}
            value=value*16+static_cast<unsigned>(c<=L'9'?c-L'0':c-L'a'+10);
        }
        output[i]=static_cast<unsigned char>(value);
    }
    return Nonzero(output.data(),N);
}
inline bool Port(const std::wstring& text,uint16_t& output){
    output=0;if(text.empty() || text.size()>5 || text[0]==L'0')return false;
    uint32_t value=0;
    for(wchar_t c:text){if(c<L'0' || c>L'9')return false;value=value*10+static_cast<uint32_t>(c-L'0');}
    if(!value || value>65535)return false;output=static_cast<uint16_t>(value);return true;
}
inline bool AbsoluteProfile(const std::wstring& text){
    if(text.size()<4 || text.size()>=260 || text[1]!=L':' || text[2]!=L'\\'
        || !((text[0]>=L'A' && text[0]<=L'Z') || (text[0]>=L'a' && text[0]<=L'z')))return false;
    size_t begin=3;
    for(size_t i=0;i<=text.size();++i){
        if(i<text.size() && (text[i]<32 || text[i]==L'/' || (i!=1 && text[i]==L':')
            || text[i]==L'*' || text[i]==L'?' || text[i]==L'"' || text[i]==L'<' || text[i]==L'>' || text[i]==L'|'))return false;
        if(i<3 || (i<text.size() && text[i]!=L'\\'))continue;
        if(i==begin || (i-begin==1 && text[begin]==L'.') || (i-begin==2 && text[begin]==L'.' && text[begin+1]==L'.')
            || text[i-1]==L'.' || text[i-1]==L' ')return false;
        begin=i+1;
    }
    return true;
}
inline bool Identity(Crossplay::Edition edition,const Crossplay::Id& value){
    if(!Nonzero(value.data(),value.size()))return false;
    if(edition!=Crossplay::Edition::Gold)return edition==Crossplay::Edition::UbisoftHistory || edition==Crossplay::Edition::SteamHistory;
    uint32_t principal=0;for(unsigned i=0;i<4;++i)principal|=uint32_t(value[i])<<(8*i);
    if(!principal || principal>INT32_MAX)return false;
    for(size_t i=4;i<value.size();++i)if(value[i])return false;
    return true;
}
inline bool Parse(const LaunchInput& input,Crossplay::Edition edition,const Crossplay::Digest& executable,
                  const std::wstring& actual_profile,Launch& output){
    output={};if(input.activation.empty())return true;
    if(input.activation!=L"1" || (input.role!=L"host" && input.role!=L"guest")
        || !Nonzero(executable.data(),executable.size()))return false;
    Launch next;next.mixed=true;next.host=input.role==L"host";
    if(input.peer!=L"1" && input.peer!=L"2" && input.peer!=L"3")return false;
    const auto peer=static_cast<Crossplay::Edition>(input.peer[0]-L'0');
    if(next.host?(edition!=Crossplay::Edition::Gold || peer==Crossplay::Edition::Gold)
        :(peer!=Crossplay::Edition::Gold || (edition!=Crossplay::Edition::UbisoftHistory && edition!=Crossplay::Edition::SteamHistory)))return false;
    auto& config=next.channel;config.edition=edition;config.expected_peer=peer;config.slot=next.host?1:2;config.executable=executable;
    if(!Port(input.port,config.port) || !Hex(input.session,config.session) || !Hex(input.key,config.key)
        || !Hex(input.identity,config.native_identity) || !Identity(edition,config.native_identity) || !Hex(input.content,config.content)
        || input.name.empty() || input.name.size()>64 || !AbsoluteProfile(input.profile)
        || !AbsoluteProfile(actual_profile) || _wcsicmp(input.profile.c_str(),actual_profile.c_str()))return false;
    for(wchar_t c:input.name){if(c<32 || c>126)return false;next.name.push_back(static_cast<char>(c));}
    next.profile=actual_profile;output=std::move(next);return true;
}
// Reads only this process's explicit launcher handoff. Empty-but-present
// activation is invalid; an absent activation retains the existing mode.
bool ReadLaunch(Crossplay::Edition edition,const Crossplay::Digest& executable,
                const std::wstring& actual_profile,Launch& output);
}
