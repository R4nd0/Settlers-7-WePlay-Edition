// SPDX-License-Identifier: MIT
// WePlay Community. Read-only clock -> ReShade uniforms. No game calls/writes.
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <bcrypt.h>
#include <vector>
#include <mutex>
#include <atomic>
#include "atmosphere_clock.h"
#include "atmosphere_fp.h"
#include "third_party/reshade/reshade.hpp"
#pragma comment(lib,"bcrypt.lib")
extern "C" __declspec(dllexport) const char* NAME="WePlay atmosphere clock";
extern "C" __declspec(dllexport) const char* DESCRIPTION="Morning haze and daytime heat follow the actual sun; read-only, no game state changes";
namespace {
const atmosphere::Profile* profile=nullptr;
uint32_t image_base=0;
struct Memory {
    bool read(uint32_t address,void* output,size_t size){SIZE_T got=0;return ReadProcessMemory(GetCurrentProcess(),reinterpret_cast<const void*>(static_cast<uintptr_t>(address)),output,size,&got)&&got==size;}
};
bool FileHash(unsigned char (&result)[32]){
    wchar_t path[MAX_PATH]{};DWORD length=GetModuleFileNameW(nullptr,path,MAX_PATH);
    if(!length||length>=MAX_PATH)return false;
    HANDLE file=CreateFileW(path,GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
    if(file==INVALID_HANDLE_VALUE)return false;
    BCRYPT_ALG_HANDLE algorithm=nullptr;BCRYPT_HASH_HANDLE hash=nullptr;DWORD object_size=0,got=0;bool okay=false;
    if(BCryptOpenAlgorithmProvider(&algorithm,BCRYPT_SHA256_ALGORITHM,nullptr,0)>=0
       &&BCryptGetProperty(algorithm,BCRYPT_OBJECT_LENGTH,reinterpret_cast<PUCHAR>(&object_size),sizeof(object_size),&got,0)>=0&&object_size<=65536){
        std::vector<unsigned char> object(object_size),buffer(65536);
        if(BCryptCreateHash(algorithm,&hash,object.data(),object_size,nullptr,0,0)>=0){
            okay=true;
            for(;;){DWORD count=0;if(!ReadFile(file,buffer.data(),static_cast<DWORD>(buffer.size()),&count,nullptr)){okay=false;break;}if(!count)break;
                if(BCryptHashData(hash,buffer.data(),count,0)<0){okay=false;break;}}
            okay=okay&&BCryptFinishHash(hash,result,sizeof(result),0)>=0;
            BCryptDestroyHash(hash);hash=nullptr;
        }
    }
    if(algorithm)BCryptCloseAlgorithmProvider(algorithm,0);CloseHandle(file);return okay;
}
bool Same(const unsigned char* value,const char* hex){
    for(unsigned i=0;i<32;++i){const auto digit=[](char c){return c<='9'?c-'0':c-'a'+10;};if(value[i]!=(digit(hex[i*2])*16+digit(hex[i*2+1])))return false;}return true;
}
void Initialize(){
    unsigned char digest[32]{};if(!FileHash(digest))return;
#ifdef WEPLAY_ATMOSPHERE_GOLD
    if(Same(digest,"76ae5c8a9f74d997a662d347803398cf39ea034e5344725c0ccb0a1ea94bf5ab"))profile=&atmosphere::Gold;
#else
    if(Same(digest,"4ef51632a8a193050ebe8ef3f3fe38a9b240b4aa57ff62a4f381bc0f539df64f"))profile=&atmosphere::Ubisoft;
    else if(Same(digest,"8349ae6523dfb5c54feaaa61370ec65708d2cde6d1063c1c9cfbe030b2aab410"))profile=&atmosphere::Steam;
#ifdef WEPLAY_ATMOSPHERE_LAB
    else if(Same(digest,"c853e8a4d4386a6beaf674dac86882faf4920a0f0288148d365940d0230819b9"))profile=&atmosphere::Ubisoft;
#endif
#endif
    if(!profile)return;
    const auto base=reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
    if(base>UINT32_MAX-profile->image_size){profile=nullptr;return;}
    Memory memory;IMAGE_DOS_HEADER dos{};IMAGE_NT_HEADERS32 pe{};
    if(!memory.read(static_cast<uint32_t>(base),&dos,sizeof(dos))||dos.e_magic!=IMAGE_DOS_SIGNATURE||dos.e_lfanew<=0||dos.e_lfanew>4096
       ||!memory.read(static_cast<uint32_t>(base)+dos.e_lfanew,&pe,sizeof(pe))||pe.Signature!=IMAGE_NT_SIGNATURE
       ||pe.FileHeader.Machine!=IMAGE_FILE_MACHINE_I386||pe.OptionalHeader.SizeOfImage!=profile->image_size){profile=nullptr;return;}
    image_base=static_cast<uint32_t>(base);
}
void Begin(reshade::api::effect_runtime* runtime,reshade::api::command_list*,reshade::api::resource_view,reshade::api::resource_view){
    atmosphere::FpScope fp;if(!fp.valid)return;
    static std::once_flag initialization;
    std::call_once(initialization,[]{try{Initialize();}catch(...){profile=nullptr;}});
    atmosphere::Clock clock;Memory memory;bool ready=false;
    if(profile)ready=atmosphere::Read(memory,image_base,*profile,clock,
#ifdef WEPLAY_ATMOSPHERE_LAB
        true
#else
        false
#endif
        )==atmosphere::Status::ready;
    auto uniform=runtime->find_uniform_variable("WePlay-Midsummer.fx","AtmospherePhaseReady");if(uniform.handle)runtime->set_uniform_value_bool(uniform,&ready,1);
    uniform=runtime->find_uniform_variable("WePlay-Midsummer.fx","AtmosphereSunAzimuth");if(uniform.handle)runtime->set_uniform_value_float(uniform,&clock.azimuth,1);
    uniform=runtime->find_uniform_variable("WePlay-Midsummer.fx","AtmosphereGameTime");if(uniform.handle)runtime->set_uniform_value_float(uniform,&clock.milliseconds,1);
#ifdef WEPLAY_ATMOSPHERE_LAB
    static std::atomic<unsigned> count{0};if(++count%120==0){char line[160];sprintf_s(line,"WEPLAY-ATMOSPHERE ready=%u azimuth=%.3f clock=%.3f",ready?1u:0u,clock.azimuth,clock.milliseconds);reshade::log::message(reshade::log::level::info,line);}
#endif
}
}
BOOL APIENTRY DllMain(HMODULE module,DWORD reason,LPVOID){
    if(reason==DLL_PROCESS_ATTACH){if(!reshade::register_addon(module))return FALSE;reshade::register_event<reshade::addon_event::reshade_begin_effects>(Begin);}
    else if(reason==DLL_PROCESS_DETACH){reshade::unregister_event<reshade::addon_event::reshade_begin_effects>(Begin);reshade::unregister_addon(module);}
    return TRUE;
}
