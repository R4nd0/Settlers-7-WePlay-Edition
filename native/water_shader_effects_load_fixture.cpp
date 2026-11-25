#include "water_shader_effects.h"
#include <cstdio>

// Optional CPU-only cross-language load check. The caller supplies an exact
// privately staged1076-byte config and trusted private report; no device exists.
int wmain(int argc,wchar_t** argv){using namespace WaterShaderEffects;
    if(argc!=3)return 2;
    HANDLE file=CreateFileW(argv[1],GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,
                            FILE_ATTRIBUTE_NORMAL|FILE_FLAG_OPEN_REPARSE_POINT,nullptr);
    if(file==INVALID_HANDLE_VALUE)return 3;
    Config config{};DWORD bytes=0;LARGE_INTEGER size{};
    const bool valid=GetFileSizeEx(file,&size)&&size.QuadPart==sizeof(Config)
        &&ReadFile(file,&config,sizeof(config),&bytes,nullptr)&&bytes==sizeof(config);
    CloseHandle(file);
    if(!valid||config.size!=sizeof(Config)||config.version!=1||config.enabled!=1)return 4;
    if(!Initialize(argv[2],config.render_width,config.render_height)||!Configure(&config))return 5;
    Stats stats{};if(!ReadStats(&stats)||!stats.configured||!stats.enabled||stats.shader_creations||stats.retained_programs)return 6;
    Config disabled{};disabled.render_width=config.render_width;disabled.render_height=config.render_height;
    if(!Configure(&disabled)||!Reset(nullptr)||!Shutdown())return 7;
    std::printf("{\"format\":\"s7-water-production-config-cpu-fixture-v1\",\"passed\":true,\"config_bytes\":%zu,\"gpu\":false,\"game\":false}\n",sizeof(Config));
    return 0;
}
