#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <bcrypt.h>
#include "crossplay_production_launch.h"
namespace CrossplayProduction {
namespace {
bool Read(const wchar_t* key,size_t maximum,std::wstring& value,bool& present){
    value.clear();SetLastError(ERROR_SUCCESS);
    const auto needed=GetEnvironmentVariableW(key,nullptr,0);
    present=needed || GetLastError()!=ERROR_ENVVAR_NOT_FOUND;
    if(!needed)return !present;
    if(needed>maximum+1)return false;
    std::wstring buffer(needed,L'\0');
    const auto size=GetEnvironmentVariableW(key,&buffer[0],needed);
    if(!size || size+1!=needed)return false;
    buffer.resize(size);value=std::move(buffer);return true;
}
#ifdef S7_CROSSPLAY_ROOM_LOBBY_LAB
bool ReadRoom(const std::wstring& activation,Crossplay::Edition edition,const Crossplay::Digest& executable,
              const std::wstring& profile,Launch& output) {
    RoomLaunchInput input;
    input.activation=activation;
    auto& common=input.common;
    struct Field {const wchar_t* key;size_t maximum;std::wstring* target;};
    const Field fields[]={
        {L"SLOT",1,&input.slot},{L"HOST_SLOT",1,&input.host_slot},{L"GENERATION",10,&input.generation},
        {L"MAP_SLOTS",Crossplay::RoomSnapshot::MaximumMembers,&input.map_slots},
        {L"MAP",CrossplaySessionConfig::MaximumPath,&input.map},{L"CAPACITY",1,&input.capacity},
        {L"PORT",5,&common.port},{L"SESSION",32,&common.session},{L"KEY",64,&common.key},
        {L"IDENTITY",32,&common.identity},{L"CONTENT",64,&common.content},
        {L"NAME",64,&common.name},{L"PROFILE",259,&common.profile}};
    for(const auto& field:fields){
        const auto env=std::wstring(L"WEPLAY_CROSSPLAY_ROOM_V2_")+field.key;
        bool present=false;
        if(!Read(env.c_str(),field.maximum,*field.target,present) || !present)return false;
    }
    for(const auto& field:{Field{L"GOLD_AUTH_PORT",5,&common.gold_auth_port},Field{L"GOLD_CONFIG_PORT",5,&common.gold_config_port}}){
        const auto env=std::wstring(L"WEPLAY_CROSSPLAY_ROOM_V2_")+field.key;
        bool present=false;
        if(!Read(env.c_str(),5,*field.target,present) || present!=(edition==Crossplay::Edition::Gold))return false;
    }
    return ParseRoom(input,edition,executable,profile,output);
}
#endif
}
bool ReadLaunch(Crossplay::Edition edition,const Crossplay::Digest& executable,
                const std::wstring& actual_profile,Launch& output){
    struct ErrorScope {DWORD value=GetLastError();~ErrorScope(){SetLastError(value);}} error;
    output={};LaunchInput input;bool present=false,room_present=false;
    std::wstring room_activation;
    if(!Read(L"WEPLAY_CROSSPLAY_ROOM_V2",1,room_activation,room_present))return false;
    if(!Read(L"WEPLAY_CROSSPLAY_V1",1,input.activation,present))return false;
    if(room_present){
#ifdef S7_CROSSPLAY_ROOM_LOBBY_LAB
        return !present && ReadRoom(room_activation,edition,executable,actual_profile,output);
#else
        return false;
#endif
    }
    if(!present)return true;
    struct Field {const wchar_t* key;size_t maximum;std::wstring* target;};
    const Field fields[]={
        {L"WEPLAY_CROSSPLAY_V1_ROLE",5,&input.role},{L"WEPLAY_CROSSPLAY_V1_PEER",1,&input.peer},
        {L"WEPLAY_CROSSPLAY_V1_PORT",5,&input.port},{L"WEPLAY_CROSSPLAY_V1_SESSION",32,&input.session},
        {L"WEPLAY_CROSSPLAY_V1_KEY",64,&input.key},{L"WEPLAY_CROSSPLAY_V1_IDENTITY",32,&input.identity},
        {L"WEPLAY_CROSSPLAY_V1_CONTENT",64,&input.content},{L"WEPLAY_CROSSPLAY_V1_NAME",64,&input.name},
        {L"WEPLAY_CROSSPLAY_V1_PROFILE",259,&input.profile}};
    for(const auto& field:fields)if(!Read(field.key,field.maximum,*field.target,present) || !present)return false;
    for(const auto& field:{Field{L"WEPLAY_CROSSPLAY_V1_GOLD_AUTH_PORT",5,&input.gold_auth_port},Field{L"WEPLAY_CROSSPLAY_V1_GOLD_CONFIG_PORT",5,&input.gold_config_port}}){
        if(!Read(field.key,field.maximum,*field.target,present) || (present!=(edition==Crossplay::Edition::Gold)))return false;
    }
    return Parse(input,edition,executable,actual_profile,output);
}
bool GoldLocalSecret(const Launch& launch,std::string& output){
    struct ErrorScope {DWORD value=GetLastError();~ErrorScope(){SetLastError(value);}} error;
    output.clear();
    if(!launch.mixed || (!launch.host && !launch.IsRoom()) || launch.channel.edition!=Crossplay::Edition::Gold
        || !Nonzero(launch.channel.key.data(),launch.channel.key.size()) || !Nonzero(launch.channel.session.data(),launch.channel.session.size()))return false;
    BCRYPT_ALG_HANDLE algorithm=nullptr;BCRYPT_HASH_HANDLE hash=nullptr;unsigned char digest[32]{};
    constexpr unsigned char label[]="S7X-GOLD-LOCAL-AUTH-1";
    bool okay=BCryptOpenAlgorithmProvider(&algorithm,BCRYPT_SHA256_ALGORITHM,nullptr,BCRYPT_ALG_HANDLE_HMAC_FLAG)>=0
        && BCryptCreateHash(algorithm,&hash,nullptr,0,const_cast<PUCHAR>(launch.channel.key.data()),32,0)>=0;
    if(okay)okay=BCryptHashData(hash,const_cast<PUCHAR>(label),sizeof(label)-1,0)>=0
        && BCryptHashData(hash,const_cast<PUCHAR>(launch.channel.session.data()),16,0)>=0
        && BCryptFinishHash(hash,digest,32,0)>=0;
    if(hash)BCryptDestroyHash(hash);if(algorithm)BCryptCloseAlgorithmProvider(algorithm,0);
    if(okay){constexpr char hex[]="0123456789abcdef";for(auto value:digest){output.push_back(hex[value>>4]);output.push_back(hex[value&15]);}}
    SecureZeroMemory(digest,sizeof(digest));return okay;
}
}
