// SPDX-License-Identifier: MIT
// Real recipient-source Host AND module admission; no device/GPU or overrides.
#include "material_shader_effects.h"
#include "material_shader_effects_contract_v3.h"
#include <cstdio>
int wmain(int argc,wchar_t** argv){
    using namespace MaterialShaderEffects;
    if(argc!=2)return 2;HANDLE file=CreateFileW(argv[1],GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL|FILE_FLAG_OPEN_REPARSE_POINT,nullptr);
    if(file==INVALID_HANDLE_VALUE)return 3;MaterialShaderConfigV3::Config config{};LARGE_INTEGER size{};BY_HANDLE_FILE_INFORMATION info{};DWORD count=0;
    const bool read=GetFileInformationByHandle(file,&info)&&!(info.dwFileAttributes&FILE_ATTRIBUTE_REPARSE_POINT)
        &&GetFileSizeEx(file,&size)&&size.QuadPart==sizeof(config)&&ReadFile(file,&config,sizeof(config),&count,nullptr)&&count==sizeof(config);
    CloseHandle(file);if(!read||!MaterialShaderConfigV3::FitsWorker(392))return 4;
    const std::wstring report=config.report_path;const auto separator=report.find_last_of(L"\\/");if(separator==std::wstring::npos)return 5;
    NativeGraphicsConfig::Version2 native{};native.base.size=sizeof(native);native.base.version=2;native.flags=NativeGraphicsConfig::Vegetation;
    native.base.render_width=config.render_width;native.base.render_height=config.render_height;native.base.output_width=1920;native.base.output_height=1080;
    if(wcscpy_s(native.base.report_path,(report.substr(0,separator)+L"\\native.private.json").c_str()))return 6;
    auto off=config;off.flags=0;MaterialShaderConfigV3::Config initial{};
    if(NativeEffectsContractV3::PrepareInitial(off,native,NativeEffectsContractV3::Layout::PrivateCase,initial,true)!=NativeEffectsContractV3::Error::None
        ||initial.flags!=Leaf||!NativeEffectsContractV3::SamePinnedConfig(initial,off))return 7;
    Config old{};old.size=7204;old.version=3;MaterialShaderConfigV2::Config modern{};modern.size=7204;modern.version=3;
    if(Initialize(&old)||InitializeV2(&modern)||!InitializeV3(&initial))return 8;
    Stats stats{};if(!ReadStats(&stats)||stats.config_version!=3||stats.initialized_flags!=Leaf||stats.shader_creations||stats.retained_programs)return 9;
    if(!SetEnabled(0)||!SetEnabled(Leaf))return 10;Shutdown();
    if(!ReadStats(&stats)||stats.initialized_flags||stats.enabled_flags||stats.cached_refs||stats.retained_programs||stats.shader_creations||stats.report_failures)return 11;
    std::printf("V3 actual recipient PS admitted by HOST and production module; toggle/close PASS, no identity overrides/GPU/game\n");return 0;
}
