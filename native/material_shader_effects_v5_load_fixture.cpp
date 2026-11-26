// SPDX-License-Identifier: MIT
// Exact recipient programs and host/module pins; CPU only, no overrides/device.
#include "material_shader_effects.h"
#include "material_shader_effects_contract_v5.h"
#include <cstdio>
#include <filesystem>
int wmain(int argc,wchar_t**argv){using namespace MaterialShaderEffects;
    if(argc!=2)return 2;HANDLE file=CreateFileW(argv[1],GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL|FILE_FLAG_OPEN_REPARSE_POINT,nullptr);
    if(file==INVALID_HANDLE_VALUE)return 3;MaterialShaderConfigV5::Config c{};LARGE_INTEGER size{};BY_HANDLE_FILE_INFORMATION info{};DWORD got=0;
    const bool okay=GetFileInformationByHandle(file,&info)&&!(info.dwFileAttributes&(FILE_ATTRIBUTE_REPARSE_POINT|FILE_ATTRIBUTE_DIRECTORY))&&GetFileSizeEx(file,&size)&&size.QuadPart==sizeof(c)
        &&ReadFile(file,&c,sizeof(c),&got,nullptr)&&got==sizeof(c);CloseHandle(file);if(!okay)return 4;
    const auto report=(std::filesystem::path(argv[1]).parent_path()/L"native.private.json").wstring();
    NativeGraphicsConfig::Version2 n{};n.base.size=sizeof(n);n.base.version=2;n.flags=NativeGraphicsConfig::Terrain|NativeGraphicsConfig::Vegetation;
    n.base.render_width=c.render_width;n.base.render_height=c.render_height;n.base.output_width=1920;n.base.output_height=1080;wcscpy_s(n.base.report_path,report.c_str());
    if(NativeEffectsContractV5::Validate(c,n,MaterialShaderConfigV5::Layout::PrivateCase)!=NativeEffectsContractV5::Error::None||!InitializeV5(&c,report.c_str()))return 5;
    Stats stats{};if(!ReadStats(&stats)||stats.config_version!=5||stats.initialized_flags!=6||stats.shader_creations||stats.retained_programs)return 6;
    const auto digit=[](wchar_t x){return x<=L'9'?x-L'0':x-L'a'+10;};
    for(unsigned i=0;i<7;++i){const auto*hex=NativeEffectsContractV5::Programs[i].digest;for(unsigned j=0;j<32;++j)if(stats.combined_program_sha256[i][j]!=(digit(hex[j*2])*16+digit(hex[j*2+1])))return 7;}
    for(DWORD flags:{0u,2u,4u,6u})for(DWORD index=0;index<3;++index){c.flags=flags;c.selected_variant=index;if(!ConfigureV5(&c)||!ReadStats(&stats)||stats.enabled_flags!=flags||stats.selected_variant!=index||stats.shader_creations)return 8;}
    Shutdown();if(!ReadStats(&stats)||stats.initialized_flags||stats.enabled_flags||stats.report_failures)return 9;
    std::puts("CombinedV5 HOST+production all7 actual recipient hashes/12 tuples/close PASS; no override/GPU/game");return 0;
}
