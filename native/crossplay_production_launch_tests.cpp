#define WIN32_LEAN_AND_MEAN
#define S7_PRODUCTION_UI_TESTING
#include <windows.h>
#include "crossplay_production_launch.h"
#include "crossplay_production_context.h"
#include "crossplay_production_runtime.h"
#include "crossplay_production_ui.h"
#include <cstdio>
#include <cstdlib>
namespace {
unsigned checks=0;
void Check(bool value){++checks;if(!value){std::fprintf(stderr,"Launch contract failed at check %u\n",checks);std::exit(1);}}
using Crossplay::Edition;
using namespace CrossplayProduction;
const std::wstring Profile=L"Z:\\Game\\LocalData\\ProfileRoot\\Profiles";
LaunchInput Input(){return {L"1",L"host",L"2",L"32100",std::wstring(32,L'1'),std::wstring(64,L'2'),
    std::wstring(L"7b")+std::wstring(30,L'0'),std::wstring(64,L'4'),L"WePlay Host",Profile,L"32101",L"32102"};}
DWORD WINAPI ForeignContext(void*){return CurrentLaunch()?1:0;}
uint32_t lua[8]{},argument[2]{};alignas(16) unsigned char lua_string[128]{};
std::string ui_result;int ui_boolean=-1;unsigned ui_pushes=0;
void PushString(void* state,const char* text,size_t size){Check(state==lua);ui_result.assign(text,size);++ui_pushes;SetLastError(0x99887766);}
void PushBoolean(void* state,int value){Check(state==lua);ui_boolean=value;++ui_pushes;SetLastError(0x99887766);}
void UiInput(const std::string& command){
    memset(lua,0,sizeof(lua));memset(lua_string,0,sizeof(lua_string));ui_result.clear();ui_boolean=-1;ui_pushes=0;
    Check(command.size()<sizeof(lua_string)-16);
    lua[4]=reinterpret_cast<uintptr_t>(argument);lua[5]=lua[4]+8;
    argument[0]=reinterpret_cast<uintptr_t>(lua_string);argument[1]=0xfffffffb;
    const auto size=static_cast<uint32_t>(command.size());memcpy(lua_string+12,&size,4);memcpy(lua_string+16,command.data(),command.size());
}
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
        input.gold_auth_port.clear();input.gold_config_port.clear();
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
        if(member==&LaunchInput::identity){input.role=L"guest";input.peer=L"1";input.gold_auth_port.clear();input.gold_config_port.clear();Check(accept(input,Edition::SteamHistory));}
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
        {L"WEPLAY_CROSSPLAY_V1_NAME",&environment.name},{L"WEPLAY_CROSSPLAY_V1_PROFILE",&environment.profile},
        {L"WEPLAY_CROSSPLAY_V1_GOLD_AUTH_PORT",&environment.gold_auth_port},{L"WEPLAY_CROSSPLAY_V1_GOLD_CONFIG_PORT",&environment.gold_config_port}};
    for(const auto& field:fields)Check(SetEnvironmentVariableW(field.first,field.second->c_str())!=FALSE);
    Check(ReadLaunch(Edition::Gold,executable,Profile,output)&&output.mixed&&output.host);
    Check(PrepareLaunch(Edition::Gold,executable,Profile));
    Check(CurrentLaunch()&&CurrentLaunch()->mixed&&CurrentLaunch()->gold_auth_port==32101);
    Check(!PrepareRuntime()&&RuntimePhase()==Phase::Dormant);PumpRuntime();Check(!CloseRuntime());
    Check(!PrepareLaunch(Edition::Gold,executable,Profile));
    HANDLE foreign=CreateThread(nullptr,0,ForeignContext,nullptr,0,nullptr);Check(foreign!=nullptr);
    Check(WaitForSingleObject(foreign,5000)==WAIT_OBJECT_0);DWORD foreign_result=1;
    Check(GetExitCodeThread(foreign,&foreign_result)&&foreign_result==0);Check(CloseHandle(foreign)!=FALSE);
    Check(!ReadLaunch(Edition::Gold,executable,L"Z:\\ForeignProfile",output)&&!output.mixed);
    Check(SetEnvironmentVariableW(L"WEPLAY_CROSSPLAY_V1_KEY",nullptr)!=FALSE);
    Check(!ReadLaunch(Edition::Gold,executable,Profile,output)&&!output.mixed);
    for(const auto& field:fields)Check(SetEnvironmentVariableW(field.first,nullptr)!=FALSE);
    Check(CurrentLaunch()&&CurrentLaunch()->mixed&&CurrentLaunch()->channel.port==32100);
    input=Input();input.gold_auth_port.clear();Check(!accept(input));
    input=Input();input.gold_config_port=L"65536";Check(!accept(input));
    input=Input();input.role=L"guest";input.peer=L"1";Check(!accept(input,Edition::UbisoftHistory));
    Check(accept(Input()));std::string secret;
    for(unsigned i=0;i<32;++i)output.channel.key[i]=static_cast<unsigned char>(i);
    for(unsigned i=0;i<16;++i)output.channel.session[i]=static_cast<unsigned char>(i);
    SetLastError(1908);Check(GoldLocalSecret(output,secret)&&GetLastError()==1908
        && secret=="9b88c19d84020f1d655a60c3a23287aba853d838cd30ca552ec12a5c3accd12e");
    output.channel.session[0]^=1;std::string changed;Check(GoldLocalSecret(output,changed)&&changed!=secret);
    output.host=false;Check(!GoldLocalSecret(output,changed)&&changed.empty());
    std::string ui;Check(UiCommand("mode",ui)&&ui=="host");Check(UiCommand("phase",ui)&&ui=="pending");
    Check(!UiCommand("guest-ready-available",ui)&&ui.empty());
    Check(!GuestReadyActionAvailable());
    Check(!UiCommand("key",ui)&&ui.empty());
    Check(!UiCommand("bootstrap-flags",ui)&&ui.empty());
    Check(!UiCommand("bootstrap-template",ui)&&ui.empty());
    UiConsole::installed=true;UiConsole::owner=GetCurrentThreadId();UiConsole::profile=UiConsole::Pin(Edition::Gold);
    UiConsole::test_push_string=&PushString;UiConsole::test_push_boolean=&PushBoolean;
    UiInput("weplay-crossplay-v1:mode");SetLastError(1910);
    Check(UiConsole::Handle(lua)==1&&ui_result=="host"&&ui_pushes==1&&GetLastError()==1910);
    UiInput("weplay-crossplay-v1:phase");Check(UiConsole::Handle(lua)==1&&ui_result=="pending");
    UiInput("weplay-crossplay-v1:key");Check(UiConsole::Handle(lua)==1&&ui_boolean==0&&ui_result.empty());
    UiInput("ordinary console output");Check(UiConsole::Handle(lua)==0&&!ui_pushes);
    UiInput("weplay-crossplay-v1:mode");argument[1]=0;Check(UiConsole::Handle(lua)==0&&!ui_pushes);
    UiInput("weplay-crossplay-v1:mode");lua[5]+=8;Check(UiConsole::Handle(lua)==0&&!ui_pushes);
    UiInput("weplay-crossplay-v1:mode");++UiConsole::owner;Check(UiConsole::Handle(lua)==0&&!ui_pushes);--UiConsole::owner;
    UiInput("weplay-crossplay-v1:mode");UiConsole::installed=false;Check(UiConsole::Handle(lua)==0&&!ui_pushes);
    Check(UiConsole::Handle(nullptr)==0);
    std::printf("Production launch contract: %u checks passed.\n",checks);
}
