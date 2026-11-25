// SPDX-License-Identifier: MIT
// CPU-only production-pin load. No fixture identity override, D3D device or game.
#include "material_shader_effects.h"
#include "material_shader_config_v2.h"
#include <cstdio>

int wmain(int argc,wchar_t** argv){
    using namespace MaterialShaderEffects;
    if(argc!=2){std::fprintf(stderr,"Use an owned private ConfigV2 file.\n");return 2;}
    HANDLE file=CreateFileW(argv[1],GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL|FILE_FLAG_OPEN_REPARSE_POINT,nullptr);
    if(file==INVALID_HANDLE_VALUE)return 3;
    MaterialShaderConfigV2::Config config{};LARGE_INTEGER size{};DWORD bytes=0;BY_HANDLE_FILE_INFORMATION info{};
    const bool valid=GetFileInformationByHandle(file,&info)&&!(info.dwFileAttributes&FILE_ATTRIBUTE_REPARSE_POINT)
        &&GetFileSizeEx(file,&size)&&(size.QuadPart==sizeof(config)||size.QuadPart==sizeof(Config))
        &&ReadFile(file,&config,static_cast<DWORD>(size.QuadPart),&bytes,nullptr)&&bytes==size.QuadPart;
    CloseHandle(file);if(!valid)return 4;
    bool initialized=false;
    if(bytes==sizeof(Config)&&config.size==sizeof(Config)&&config.version==1){
        Config legacy{};std::memcpy(&legacy,&config,sizeof(legacy));
        if(InitializeV2(&config))return 5;initialized=Initialize(&legacy);
    }else if(bytes==sizeof(config)&&config.size==sizeof(config)&&config.version==2){
        if(Initialize(reinterpret_cast<const Config*>(&config)))return 5;initialized=InitializeV2(&config);
    }else return 5;
    if(!initialized)return 5;
    Stats before{};if(!ReadStats(&before)||before.config_version!=config.version||before.shader_creations||before.retained_programs||!before.initialized_flags)return 6;
    if(!SetEnabled(0)||!SetEnabled(config.flags))return 7;
    Shutdown();Stats after{};
    if(!ReadStats(&after)||after.initialized_flags||after.enabled_flags||after.cached_refs||after.retained_programs||after.shader_creations||after.report_failures)return 8;
    std::printf("CPU production V%lu pins loaded/toggled/closed; no identity overrides, GPU/device/game or visual qualification\n",config.version);return 0;
}
