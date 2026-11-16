#define WIN32_LEAN_AND_MEAN
#include <windows.h>
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
}
bool ReadLaunch(Crossplay::Edition edition,const Crossplay::Digest& executable,
                const std::wstring& actual_profile,Launch& output){
    struct ErrorScope {DWORD value=GetLastError();~ErrorScope(){SetLastError(value);}} error;
    output={};LaunchInput input;bool present=false;
    if(!Read(L"WEPLAY_CROSSPLAY_V1",1,input.activation,present))return false;
    if(!present)return true;
    struct Field {const wchar_t* key;size_t maximum;std::wstring* target;};
    const Field fields[]={
        {L"WEPLAY_CROSSPLAY_V1_ROLE",5,&input.role},{L"WEPLAY_CROSSPLAY_V1_PEER",1,&input.peer},
        {L"WEPLAY_CROSSPLAY_V1_PORT",5,&input.port},{L"WEPLAY_CROSSPLAY_V1_SESSION",32,&input.session},
        {L"WEPLAY_CROSSPLAY_V1_KEY",64,&input.key},{L"WEPLAY_CROSSPLAY_V1_IDENTITY",32,&input.identity},
        {L"WEPLAY_CROSSPLAY_V1_CONTENT",64,&input.content},{L"WEPLAY_CROSSPLAY_V1_NAME",64,&input.name},
        {L"WEPLAY_CROSSPLAY_V1_PROFILE",259,&input.profile}};
    for(const auto& field:fields)if(!Read(field.key,field.maximum,*field.target,present) || !present)return false;
    return Parse(input,edition,executable,actual_profile,output);
}
}
