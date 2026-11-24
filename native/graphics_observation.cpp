// SPDX-License-Identifier: MIT
// Engine getters and already-mapped scalar observations; no renderer mutation.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <bcrypt.h>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <vector>
#include "graphics_observation.h"
#include "atmosphere_clock.h"
#include "atmosphere_fp.h"
#if !defined(_M_IX86)
#error Graphics observation requires the original x86 engine ABI
#endif

namespace GraphicsObservation {
namespace {
using MainContext=void* (__cdecl*)();
using Camera=void* (__thiscall*)(void*);
using Matrix=void (__thiscall*)(void*,void*);
using Floats=void (__thiscall*)(void*,float*,float*);
using Size=void (__thiscall*)(void*,int*,int*);
struct Getters {MainContext main=nullptr;Camera camera=nullptr;Matrix world=nullptr;
    Floats fov=nullptr,clip=nullptr;Size size=nullptr;};
struct Profile {const char* hash;uint32_t edition,lod_store;const atmosphere::Profile* clock;};
constexpr Profile Profiles[]={
    {"4ef51632a8a193050ebe8ef3f3fe38a9b240b4aa57ff62a4f381bc0f539df64f",1,0x111799,&atmosphere::Ubisoft},
    // The instruction-derived slot is used only when the decoded live setter
    // matches its full two-MULSS / MOVSS-store shape. Packed file bytes alone
    // do not qualify this Steam observation.
    {"8349ae6523dfb5c54feaaa61370ec65708d2cde6d1063c1c9cfbe030b2aab410",2,0x110d39,&atmosphere::Steam},
    {"76ae5c8a9f74d997a662d347803398cf39ea034e5344725c0ccb0a1ea94bf5ab",3,0x1593d6,&atmosphere::Gold},
};
constexpr char VisionHash[]="d950f1bdb11d83a9d4c6c7e1937b7d7ef747dff884e4e6b6e70e72ce7ed330a1";
constexpr char BaseHash[]="adaedb323c7e79abd758ac7237d9ed5eb6cdb43a95ef6329c4b88bbcfb2c7c0d";
Getters getters;
const Profile* profile=nullptr;
uint32_t image_base=0;
bool initialized=false;
std::once_flag initialization;
std::vector<HANDLE> pinned_files;
struct LastErrorScope {
    DWORD value=GetLastError();
    ~LastErrorScope(){SetLastError(value);}
};

struct Memory {
    bool read(uint32_t address,void* output,size_t size) {
        SIZE_T got=0;
        return address>=0x10000 && size<=65536 && address<=UINT32_MAX-size
            && ReadProcessMemory(GetCurrentProcess(),reinterpret_cast<const void*>(address),output,size,&got) && got==size;
    }
};

bool HashModule(HMODULE module,char (&hex)[65],HANDLE& retained) {
    retained=INVALID_HANDLE_VALUE;
    if(!module)return false;
    wchar_t path[32768]{};
    const DWORD length=GetModuleFileNameW(module,path,32768);
    if(!length || length>=32768)return false;
    HANDLE file=CreateFileW(path,GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
    if(file==INVALID_HANDLE_VALUE)return false;
    BCRYPT_ALG_HANDLE algorithm=nullptr;BCRYPT_HASH_HANDLE hash=nullptr;
    bool okay=BCryptOpenAlgorithmProvider(&algorithm,BCRYPT_SHA256_ALGORITHM,nullptr,0)>=0;
    if(okay)okay=BCryptCreateHash(algorithm,&hash,nullptr,0,nullptr,0,0)>=0;
    std::array<unsigned char,65536> bytes{};std::array<unsigned char,32> digest{};
    DWORD count=0;uint64_t total=0;
    while(okay) {
        if(!ReadFile(file,bytes.data(),static_cast<DWORD>(bytes.size()),&count,nullptr)){okay=false;break;}
        if(!count)break;
        total+=count;
        if(total>64u*1024u*1024u || BCryptHashData(hash,bytes.data(),count,0)<0){okay=false;break;}
    }
    if(okay)okay=BCryptFinishHash(hash,digest.data(),static_cast<ULONG>(digest.size()),0)>=0;
    if(hash)BCryptDestroyHash(hash);
    if(algorithm)BCryptCloseAlgorithmProvider(algorithm,0);
    if(!okay){CloseHandle(file);return false;}
    constexpr char digits[]="0123456789abcdef";
    for(size_t i=0;i<digest.size();++i){hex[2*i]=digits[digest[i]>>4];hex[2*i+1]=digits[digest[i]&15];}
    hex[64]=0;retained=file;return true;
}

bool Pin(HMODULE module,const char* expected) {
    char actual[65]{};HANDLE file=INVALID_HANDLE_VALUE;
    if(!HashModule(module,actual,file))return false;
    if(std::strcmp(actual,expected)){CloseHandle(file);return false;}
    HMODULE retained=nullptr;
    if(!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_PIN,
        reinterpret_cast<const wchar_t*>(module),&retained) || retained!=module){CloseHandle(file);return false;}
    pinned_files.push_back(file);return true;
}

template<class T> bool Export(HMODULE module,const char* name,uint32_t rva,T& output,
    uint32_t length,uint64_t fingerprint,uint32_t relocation=UINT32_MAX) {
    const auto value=GetProcAddress(module,name);
    if(!value || reinterpret_cast<uintptr_t>(value)!=reinterpret_cast<uintptr_t>(module)+rva)return false;
    MEMORY_BASIC_INFORMATION info{};
    if(!VirtualQuery(reinterpret_cast<void*>(value),&info,sizeof(info)) || info.State!=MEM_COMMIT
        || info.AllocationBase!=module || info.Protect&(PAGE_GUARD|PAGE_NOACCESS)
        || !(info.Protect&(PAGE_EXECUTE|PAGE_EXECUTE_READ|PAGE_EXECUTE_READWRITE|PAGE_EXECUTE_WRITECOPY)))return false;
    std::array<unsigned char,32> code{};Memory memory;
    if(!length || length>code.size() || !memory.read(static_cast<uint32_t>(reinterpret_cast<uintptr_t>(value)),code.data(),length))return false;
    if(relocation!=UINT32_MAX) {
        if(relocation>length || length-relocation<4)return false;
        uint32_t address=0;std::memcpy(&address,code.data()+relocation,4);
        address=address-static_cast<uint32_t>(reinterpret_cast<uintptr_t>(module))+0x10000000u;
        std::memcpy(code.data()+relocation,&address,4);
    }
    uint64_t actual=0xcbf29ce484222325ull;
    for(uint32_t i=0;i<length;++i)actual=(actual^code[i])*0x100000001b3ull;
    if(actual!=fingerprint)return false;
    output=reinterpret_cast<T>(value);return true;
}

void Init() {
    atmosphere::FpScope fp;if(!fp.valid)return;
    HMODULE executable=GetModuleHandleW(nullptr),vision=GetModuleHandleW(L"Vision90.dll"),base=GetModuleHandleW(L"vBase90.dll");
    char actual[65]{};HANDLE file=INVALID_HANDLE_VALUE;
    if(!HashModule(executable,actual,file))return;
    for(const auto& candidate:Profiles)if(!std::strcmp(actual,candidate.hash)){profile=&candidate;break;}
    if(!profile){CloseHandle(file);return;}
    pinned_files.push_back(file);
    if(!Pin(vision,VisionHash) || !Pin(base,BaseHash))return;
    const auto address=reinterpret_cast<uintptr_t>(executable);
    if(address>UINT32_MAX-profile->clock->image_size)return;
    image_base=static_cast<uint32_t>(address);
    Memory memory;IMAGE_DOS_HEADER dos{};IMAGE_NT_HEADERS32 pe{};
    if(!memory.read(image_base,&dos,sizeof(dos)) || dos.e_magic!=IMAGE_DOS_SIGNATURE
        || dos.e_lfanew<64 || dos.e_lfanew>4096
        || !memory.read(image_base+dos.e_lfanew,&pe,sizeof(pe)) || pe.Signature!=IMAGE_NT_SIGNATURE
        || pe.FileHeader.Machine!=IMAGE_FILE_MACHINE_I386 || pe.OptionalHeader.SizeOfImage!=profile->clock->image_size)return;
    // Prefix hashes are ABI/change guards, not security identities. Whole-file
    // SHA256 pins above remain mandatory; absolute operands normalize ASLR.
    initialized=Export(vision,"?GetMainRenderContext@VisRenderContext_cl@@SAPAV1@XZ",0x40700,getters.main,6,0x5ef1054a75073397ull,1)
        && Export(vision,"?GetCamera@VisRenderContext_cl@@QBEPAVVisContextCamera_cl@@XZ",0x40720,getters.camera,4,0x1d394ef6ff61ced8ull)
        && Export(vision,"?GetWorldMatrix@VisContextCamera_cl@@QBEXAAVVisMatrix4x4_cl@@@Z",0x166a0,getters.world,32,0x2a057b15421c8f57ull)
        && Export(vision,"?GetFinalFOV@VisRenderContext_cl@@QBEXAAM0@Z",0x3faa0,getters.fov,32,0x0285bc4e6ec5d633ull)
        && Export(vision,"?GetClipPlanes@VisRenderContext_cl@@QBEXAAM0@Z",0x3fae0,getters.clip,27,0xc1737e5edef08ad8ull)
        && Export(vision,"?GetSize@VisRenderContext_cl@@QBEXAAH0@Z",0x40730,getters.size,32,0x24b7a917a3776777ull,2);
}

bool ReadLod(float& output) {
    if(!profile || !image_base)return false;
    Memory memory;
    std::array<unsigned char,24> code{};
    if(!memory.read(image_base+profile->lod_store-16,code.data(),code.size()))return false;
    constexpr unsigned char mul[]={0xf3,0x0f,0x59,0x05},store[]={0xf3,0x0f,0x11,0x05};
    if(std::memcmp(code.data(),mul,4) || std::memcmp(code.data()+8,mul,4) || std::memcmp(code.data()+16,store,4))return false;
    uint32_t destination=0;std::memcpy(&destination,code.data()+20,4);
    float value=0;
    if(destination<image_base || destination>image_base+profile->clock->image_size-4
        || !memory.read(destination,&value,sizeof(value)) || !std::isfinite(value) || value<=0 || value>100)return false;
    output=value;return true;
}

bool ReadGetters(Snapshot& result) {
    void* context=getters.main();
    if(reinterpret_cast<uintptr_t>(context)<0x10000){result.status=MissingContext;return false;}
    void* camera=getters.camera(context);
    if(reinterpret_cast<uintptr_t>(camera)<0x10000){result.status=MissingContext;return false;}
    struct Buffer {uint32_t before;float values[16];uint32_t after;};
    Buffer world{0x7ac51d29,{},0x1de34b87};
    // The original GetInverseEyeMatrix caller constructs an identity matrix
    // before invoking GetWorldMatrix. That getter fills 15 components and
    // leaves the homogeneous final element intact. Preserve the same canonical
    // initialization; this element is a convention, not a measured extra field.
    for(unsigned i=0;i<16;i+=5)world.values[i]=1.f;
    getters.world(camera,world.values);
    getters.fov(context,&result.fov_native[0],&result.fov_native[1]);
    getters.clip(context,&result.clip_native[0],&result.clip_native[1]);
    int width=0,height=0;getters.size(context,&width,&height);
    if(world.before!=0x7ac51d29 || world.after!=0x1de34b87 || getters.main()!=context || getters.camera(context)!=camera
        || width<1 || width>16384 || height<1 || height>16384){result.status=InvalidOutput;return false;}
    for(float value:world.values)if(!std::isfinite(value) || std::abs(value)>1e9f){result.status=InvalidOutput;return false;}
    const auto& m=world.values;
    const double determinant=static_cast<double>(m[0])*(m[5]*m[10]-m[6]*m[9])
        -static_cast<double>(m[1])*(m[4]*m[10]-m[6]*m[8])+static_cast<double>(m[2])*(m[4]*m[9]-m[5]*m[8]);
    if(!std::isfinite(determinant) || std::abs(determinant)<1e-6 || std::abs(determinant)>1e6){result.status=InvalidOutput;return false;}
    for(float fov:result.fov_native)if(!std::isfinite(fov) || fov<=0 || fov>=180){result.status=InvalidOutput;return false;}
    if(!std::isfinite(result.clip_native[0]) || !std::isfinite(result.clip_native[1])
        || result.clip_native[0]<=0 || result.clip_native[1]<=result.clip_native[0]){result.status=InvalidOutput;return false;}
    std::memcpy(result.camera_world,world.values,sizeof(result.camera_world));
    result.context_width=static_cast<uint32_t>(width);result.context_height=static_cast<uint32_t>(height);
    result.available=CameraReady|ContextSizeReady;result.status=Ready;return true;
}

bool SafeGetters(Snapshot& result) {
    __try{return ReadGetters(result);}
    __except(EXCEPTION_EXECUTE_HANDLER){result.status=GetterFault;return false;}
}
}

bool Initialize(){LastErrorScope error;std::call_once(initialization,Init);return initialized;}

bool Read(Snapshot& output) {
    LastErrorScope error;
    output={};output.size=sizeof(output);output.version=1;output.status=NotInitialized;
    output.sun_status=static_cast<uint32_t>(atmosphere::Status::unavailable);
    if(!initialized)return false;
    atmosphere::FpScope fp;
    if(!fp.valid){output.status=FloatingPointUnavailable;return false;}
    output.edition=profile?profile->edition:0;
    Snapshot camera=output;
    if(!SafeGetters(camera)){output.status=camera.status;return false;}
    output=camera;
    if(ReadLod(output.lod_scale))output.available|=LodReady;
    if(profile && image_base) {
        Memory memory;atmosphere::Clock clock;
        const auto status=atmosphere::Read(memory,image_base,*profile->clock,clock);
        output.sun_status=static_cast<uint32_t>(status);
        if(status==atmosphere::Status::ready){output.available|=SunAzimuthReady;output.sun_azimuth=clock.azimuth;output.sun_clock_ms=clock.milliseconds;}
    }
    return true;
}

#ifdef GRAPHICS_OBSERVATION_FIXTURE
void InitializeFixture(const FixtureGetters& value) {
    getters={value.main,value.camera,value.world,value.fov,value.clip,value.size};
    profile=nullptr;image_base=0;initialized=true;
}
#endif
}
