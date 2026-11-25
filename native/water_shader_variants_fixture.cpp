#include "water_shader_effects.h"
#include <cstdio>
#include <cstring>
#include <string>

using namespace WaterShaderEffects;
unsigned checks=0;
bool Check(bool value,const char* name){++checks;if(!value)std::fprintf(stderr,"FAIL: %s\n",name);return value;}
bool Read(const wchar_t* path,Config& config){HANDLE file=CreateFileW(path,GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL|FILE_FLAG_OPEN_REPARSE_POINT,nullptr);
    if(file==INVALID_HANDLE_VALUE)return false;LARGE_INTEGER size{};DWORD got=0;const bool ok=GetFileSizeEx(file,&size)&&size.QuadPart==1076&&ReadFile(file,&config,sizeof(config),&got,nullptr)&&got==1076;CloseHandle(file);return ok;}
Stats State(){Stats s{};ReadStats(&s);return s;}
int wmain(int argc,wchar_t** argv){if(argc!=4)return 2;Config a{},b{};
    if(!Check(Read(argv[1],a)&&Read(argv[2],b),"exact1076 A/B payloads"))return 3;
    if(!Check(Initialize(argv[3],a.render_width,a.render_height),"one admitted trust root"))return 4;
    if(!Check(Configure(&a)&&State().variant==1&&State().enabled,"exact A selected"))return 5;
    if(!Check(Configure(&b)&&State().variant==2&&State().enabled,"exact B selected in same process"))return 6;
    Config wrong=b;std::memcpy(wrong.pixel_sha256,a.pixel_sha256,32);
    if(!Check(!Configure(&wrong)&&State().variant==2&&State().enabled,"A hash with B recipe/path rejected without changing active B"))return 7;
    wrong=a;std::memcpy(wrong.pixel_sha256,b.pixel_sha256,32);
    if(!Check(!Configure(&wrong)&&State().variant==2,"B hash with A path rejected"))return 8;
    wrong=b;wrong.pixel_sha256[0]^=1;
    if(!Check(!Configure(&wrong)&&State().variant==2,"unknown enabled hash rejected"))return 9;
    wrong.enabled=0;
    if(!Check(!Configure(&wrong)&&State().variant==2&&State().enabled,"unknown populated disable hash rejected"))return 10;
    wrong=b;wrong.enabled=0;
    if(!Check(Configure(&wrong)&&State().variant==2&&!State().enabled,"explicit known B off retains variant identity"))return 11;
    if(!Check(Configure(&a)&&State().variant==1&&State().enabled,"explicit B-to-A switch accepted, A immutable"))return 12;
    Config off{};off.render_width=a.render_width;off.render_height=a.render_height;
    if(!Check(Configure(&off)&&!State().enabled&&State().variant==1,"zero off bootstrap remains supported"))return 13;
    if(!Check(Reset(nullptr)&&Shutdown(),"safe no-device cleanup"))return 14;
    std::printf("{\"format\":\"s7-waterAB-production-pin-cpu-v1\",\"passed\":true,\"checks\":%u,\"transport\":1076,\"A_B_A_same_process\":true,\"unknown_mismatched_pins_rejected\":true,\"gpu\":false,\"game\":false}\n",checks);return 0;
}
