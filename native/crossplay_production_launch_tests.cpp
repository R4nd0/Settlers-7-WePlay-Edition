#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "crossplay_production_launch.h"
#include <cstdio>
#include <cstdlib>
namespace {
unsigned checks=0;
void Check(bool value){++checks;if(!value){std::fprintf(stderr,"Launch contract failed at check %u\n",checks);std::exit(1);}}
using Crossplay::Edition;
using namespace CrossplayProduction;
const std::wstring Profile=L"Z:\\Game\\LocalData\\ProfileRoot\\Profiles";
LaunchInput Input(){return {L"1",L"host",L"2",L"32100",std::wstring(32,L'1'),std::wstring(64,L'2'),
    std::wstring(L"7b")+std::wstring(30,L'0'),std::wstring(64,L'4'),L"WePlay Host",Profile};}
}
int main(){
    Crossplay::Digest executable{};executable[0]=7;Launch output;
    auto accept=[&](const LaunchInput& in,Edition local=Edition::Gold){return Parse(in,local,executable,Profile,output);};
    Check(accept({}) && !output.mixed);
    auto input=Input();Check(accept(input) && output.mixed && output.host && output.channel.slot==1
        && output.channel.port==32100 && output.channel.executable==executable && output.name=="WePlay Host");
    input.peer=L"3";Check(accept(input) && output.channel.expected_peer==Edition::SteamHistory);
    for(auto local:{Edition::UbisoftHistory,Edition::SteamHistory}){
        input=Input();input.role=L"guest";input.peer=L"1";
        Check(accept(input,local) && !output.host && output.channel.slot==2);
        input.peer=L"2";Check(!accept(input,local) && !output.mixed);
        input.role=L"host";input.peer=L"1";Check(!accept(input,local));
    }
    input=Input();input.role=L"guest";input.peer=L"1";Check(!accept(input));
    input=Input();input.peer=L"1";Check(!accept(input));
    for(const auto& invalid:{L"0",L"2",L"01",L"1 ",L"true"}){input=Input();input.activation=invalid;Check(!accept(input));}
    for(const auto& invalid:{L"",L"Host",L"guest ",L"observer"}){input=Input();input.role=invalid;Check(!accept(input));}
    for(const auto& invalid:{L"",L"0",L"4",L"02",L"-1"}){input=Input();input.peer=invalid;Check(!accept(input));}
    for(const auto& invalid:{L"",L"0",L"01",L"65536",L"123456",L"-1",L"+1",L" 1",L"1x"}){input=Input();input.port=invalid;Check(!accept(input));}
    for(const auto& valid:{L"1",L"65535"}){input=Input();input.port=valid;Check(accept(input));}
    for(auto member:{&LaunchInput::session,&LaunchInput::key,&LaunchInput::identity,&LaunchInput::content}){
        input=Input();const auto size=(input.*member).size();
        for(wchar_t invalid:{L'0',L'A',L'g',L' ',L'\0'}){
            input=Input();input.*member=std::wstring(size,invalid);Check(!accept(input));
        }
        input=Input();(input.*member).pop_back();Check(!accept(input));
        input=Input();(input.*member).push_back(L'1');Check(!accept(input));
        input=Input();input.*member=std::wstring(size,L'f');
        if(member==&LaunchInput::identity){input.role=L"guest";input.peer=L"1";Check(accept(input,Edition::SteamHistory));}
        else Check(accept(input));
    }
    for(const auto& invalid:{L"",L"relative",L"Z:relative",L"Z:\\Game\\..\\Profiles",L"Z:\\Game\\.\\Profiles",
        L"Z:\\Game\\\\Profiles",L"Z:\\Game\\Profiles\\",L"Z:/Game/Profiles",L"Z:\\Game\\Profiles.",L"Z:\\Game\\Profiles ",
        L"\\\\server\\Game",L"Z:\\Game\\Profiles:stream",L"Z:\\Other\\Profiles"}){
        input=Input();input.profile=invalid;Check(!accept(input));
    }
    input=Input();input.profile=L"z:\\game\\localdata\\profileroot\\profiles";Check(accept(input));
    input=Input();input.name=std::wstring(64,L'x');Check(accept(input));input.name.push_back(L'x');Check(!accept(input));
    for(const auto& invalid:{L"",L"line\nname",L"tab\tname",L"\u00e4"}){input=Input();input.name=invalid;Check(!accept(input));}
    input=Input();input.name=std::wstring(L"a\0b",3);Check(!accept(input));
    input=Input();input.profile=std::wstring(L"Z:\\a\0b",6);Check(!accept(input));
    Crossplay::Digest zero{};Check(!Parse(Input(),Edition::Gold,zero,Profile,output) && !output.mixed);
    for(const auto& invalid:{L"00000080000000000000000000000000",L"01000000010000000000000000000000",L"00000000010000000000000000000000"}){
        input=Input();input.identity=invalid;Check(!accept(input));
    }
    // Exercise actual Windows environment semantics, including empty/present,
    // invalid activation and error preservation, without changing game state.
    Check(SetEnvironmentVariableW(L"WEPLAY_CROSSPLAY_V1",nullptr)!=FALSE);
    SetLastError(1701);Check(ReadLaunch(Edition::Gold,executable,Profile,output) && !output.mixed && GetLastError()==1701);
    Check(SetEnvironmentVariableW(L"WEPLAY_CROSSPLAY_V1",L"2")!=FALSE);
    Check(!ReadLaunch(Edition::Gold,executable,Profile,output) && !output.mixed);
    Check(SetEnvironmentVariableW(L"WEPLAY_CROSSPLAY_V1",nullptr)!=FALSE);
    const auto environment=Input();
    const std::pair<const wchar_t*,const std::wstring*> fields[]={
        {L"WEPLAY_CROSSPLAY_V1",&environment.activation},{L"WEPLAY_CROSSPLAY_V1_ROLE",&environment.role},
        {L"WEPLAY_CROSSPLAY_V1_PEER",&environment.peer},{L"WEPLAY_CROSSPLAY_V1_PORT",&environment.port},
        {L"WEPLAY_CROSSPLAY_V1_SESSION",&environment.session},{L"WEPLAY_CROSSPLAY_V1_KEY",&environment.key},
        {L"WEPLAY_CROSSPLAY_V1_IDENTITY",&environment.identity},{L"WEPLAY_CROSSPLAY_V1_CONTENT",&environment.content},
        {L"WEPLAY_CROSSPLAY_V1_NAME",&environment.name},{L"WEPLAY_CROSSPLAY_V1_PROFILE",&environment.profile}};
    for(const auto& field:fields)Check(SetEnvironmentVariableW(field.first,field.second->c_str())!=FALSE);
    Check(ReadLaunch(Edition::Gold,executable,Profile,output)&&output.mixed&&output.host);
    Check(!ReadLaunch(Edition::Gold,executable,L"Z:\\ForeignProfile",output)&&!output.mixed);
    Check(SetEnvironmentVariableW(L"WEPLAY_CROSSPLAY_V1_KEY",nullptr)!=FALSE);
    Check(!ReadLaunch(Edition::Gold,executable,Profile,output)&&!output.mixed);
    for(const auto& field:fields)Check(SetEnvironmentVariableW(field.first,nullptr)!=FALSE);
    std::printf("Production launch contract: %u checks passed.\n",checks);
}
