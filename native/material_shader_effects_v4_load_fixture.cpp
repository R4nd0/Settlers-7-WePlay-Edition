// SPDX-License-Identifier: MIT
// Actual recipient programs admitted by unmodified production pins; CPU only.
#include "material_shader_effects.h"
#include "material_shader_effects_contract_v4.h"
#include <cstdio>
int wmain(int argc,wchar_t**argv){using namespace MaterialShaderEffects;
    if(argc!=2)return 2;HANDLE f=CreateFileW(argv[1],GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL|FILE_FLAG_OPEN_REPARSE_POINT,nullptr);
    if(f==INVALID_HANDLE_VALUE)return 3;MaterialShaderConfigV4::Config config{};LARGE_INTEGER length{};BY_HANDLE_FILE_INFORMATION info{};DWORD got=0;
    const bool okay=GetFileInformationByHandle(f,&info)&&!(info.dwFileAttributes&FILE_ATTRIBUTE_REPARSE_POINT)&&GetFileSizeEx(f,&length)&&length.QuadPart==sizeof(config)
        &&ReadFile(f,&config,sizeof(config),&got,nullptr)&&got==sizeof(config);CloseHandle(f);if(!okay)return 4;
    const std::wstring report=config.report_path;const auto slash=report.find_last_of(L"\\/");if(slash==std::wstring::npos)return 5;
    NativeGraphicsConfig::Version2 native{};native.base.size=sizeof(native);native.base.version=2;native.flags=NativeGraphicsConfig::Vegetation;
    native.base.render_width=config.render_width;native.base.render_height=config.render_height;native.base.output_width=1920;native.base.output_height=1080;
    wcscpy_s(native.base.report_path,(report.substr(0,slash)+L"/native.private.json").c_str());
    auto off=config;off.flags=0;MaterialShaderConfigV4::Config initial{};
    if(NativeEffectsContractV4::PrepareInitial(off,native,NativeEffectsContractV4::Layout::PrivateCase,initial,true)!=NativeEffectsContractV4::Error::None||!InitializeV4(&initial))return 6;
    Stats stats{};if(!ReadStats(&stats)||stats.config_version!=4||stats.shader_creations||stats.retained_programs)return 7;
    for(unsigned i=0;i<3;++i){if(!SetEnabled(0)||!SetVariant(i)||!ReadStats(&stats)||stats.selected_variant!=i)return 8;
        for(unsigned j=0;j<32;++j){const wchar_t* hex=NativeEffectsContractV4::Digests[i];const auto digit=[](wchar_t c){return c<=L'9'?c-L'0':c-L'a'+10;};
            if(stats.variant_sha256[i][j]!=(digit(hex[j*2])*16+digit(hex[j*2+1])))return 9;}}
    Shutdown();if(!ReadStats(&stats)||stats.enabled_flags||stats.initialized_flags||stats.report_failures)return 10;
    std::printf("V4 HOST + production module: all3 recipient hashes, OFF selections and close PASS; no overrides/GPU/game\n");return 0;
}
