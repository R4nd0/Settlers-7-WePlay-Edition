// SPDX-License-Identifier: MIT
// Recipient programs + exact production pins; no device, identity override/GPU.
#include "water_shader_effects.h"
#include "water_shader_palette_contract.h"
#include <cstdio>
#include <string>
int wmain(int argc,wchar_t**argv){using namespace WaterShaderEffects;
    if(argc!=2)return 2;HANDLE file=CreateFileW(argv[1],GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL|FILE_FLAG_OPEN_REPARSE_POINT,nullptr);
    if(file==INVALID_HANDLE_VALUE)return 3;ConfigPalette c{};LARGE_INTEGER size{};DWORD got=0;BY_HANDLE_FILE_INFORMATION info{};
    const bool okay=GetFileInformationByHandle(file,&info)&&!(info.dwFileAttributes&(FILE_ATTRIBUTE_DIRECTORY|FILE_ATTRIBUTE_REPARSE_POINT))&&GetFileSizeEx(file,&size)&&size.QuadPart==sizeof(c)
        &&ReadFile(file,&c,sizeof(c),&got,nullptr)&&got==sizeof(c);CloseHandle(file);if(!okay)return 4;
    const std::wstring path=c.program_paths[0];const auto marker=path.find(L"\\LocalData\\MaterialShaders\\");if(marker==std::wstring::npos)return 5;
    if(!Initialize((path.substr(0,marker)+L"\\native.private.json").c_str(),c.render_width,c.render_height))return 6;
    c.enabled=0;if(!ConfigurePalette(&c))return 7;Stats s{};
    for(unsigned i=0;i<3;++i){c.selected_variant=i;if(!ConfigurePalette(&c)||!ReadStats(&s)||s.selected_variant!=i||s.shader_creations||s.retained_programs)return 8;
        const auto digit=[](char x){return x<='9'?x-'0':x-'a'+10;};for(unsigned j=0;j<32;++j){const auto* hex=WaterShaderPaletteContract::Digests[i];if(s.variant_sha256[i][j]!=(digit(hex[j*2])*16+digit(hex[j*2+1])))return 9;}}
    if(!Shutdown())return 10;std::puts("WaterPalette all3 actual recipient production hashes/OFF switches/close PASS; no override/GPU/game");return 0;
}
