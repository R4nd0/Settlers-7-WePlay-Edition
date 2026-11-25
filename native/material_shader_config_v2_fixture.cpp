// SPDX-License-Identifier: MIT
// CPU-only layout, role/path validation and old-allocation boundary fixture.
#include "material_shader_config_v2.h"
#include <cstdio>
#include <limits>
#include <stdexcept>
#include <string>

namespace {
unsigned checks=0;
void Check(bool value,const char* text){++checks;if(!value)throw std::runtime_error(text);}
template<size_t N> void Assign(wchar_t (&path)[N],const wchar_t* value){std::wmemset(path,0,N);wcscpy_s(path,value);}
MaterialShaderConfigV2::Config Valid(){
    MaterialShaderConfigV2::Config value{};value.flags=7;value.render_width=3840;value.render_height=2160;
    Assign(value.leaf_vertex_path,L"C:\\Recipient\\LocalData\\MaterialShaders\\leaf-v2\\leaf.vs.bin");
    Assign(value.bump_pixel_path,L"C:\\Recipient\\LocalData\\MaterialShaders\\bump-v1\\bump.ps.bin");
    Assign(value.parallax_pixel_path,L"C:\\Recipient\\LocalData\\MaterialShaders\\parallax-v1\\parallax.ps.bin");
    Assign(value.report_path,L"C:\\Recipient\\LocalData\\material-effects.private.json");
    Assign(value.bump_vertex_path,L"C:\\Recipient\\LocalData\\MaterialShaders\\bump-v2\\bump.vs.bin");
    Assign(value.parallax_vertex_path,L"C:\\Recipient\\LocalData\\MaterialShaders\\parallax-v2\\parallax.vs.bin");
    return value;
}
}
int wmain(int argc,wchar_t** argv){using namespace MaterialShaderConfigV2;try{
    Check(argc==1||argc==2,"optional owned private ConfigV2 file only");
    auto value=Valid();Check(Validate(value),"valid full configuration");Config output{};
    Check(Decode(&value,sizeof(value),&output)&&!std::memcmp(&value,&output,sizeof(value)),"exact V2 round trip");
    Check(FitsWorker(2012)&&FitsWorker(512),"measured worker prefix fits remaining budget");
    Check(!FitsWorker(2013)&&!FitsWorker(0)&&!FitsWorker(static_cast<size_t>(-1)),"full worker overhead bound is separate from Config size");
    for(DWORD flags=1;flags<=7;++flags){auto subset=Valid();subset.flags=flags;
        if(!(flags&4))std::memset(subset.leaf_vertex_path,0,sizeof(subset.leaf_vertex_path));
        if(!(flags&2)){std::memset(subset.bump_vertex_path,0,sizeof(subset.bump_vertex_path));std::memset(subset.parallax_vertex_path,0,sizeof(subset.parallax_vertex_path));
            std::memset(subset.bump_pixel_path,0,sizeof(subset.bump_pixel_path));std::memset(subset.parallax_pixel_path,0,sizeof(subset.parallax_pixel_path));}
        Check(Validate(subset),"explicit role subset");}
    const auto Refuse=[](auto mutate){auto candidate=Valid();mutate(candidate);Check(!Validate(candidate),"malformed V2 refused");};
    Refuse([](auto& c){c.size=4132;});Refuse([](auto& c){c.version=1;});Refuse([](auto& c){c.flags=0;});Refuse([](auto& c){c.flags=8;});
    Refuse([](auto& c){c.render_width=8193;});Refuse([](auto& c){c.render_width=8192;c.render_height=8192;});
    Refuse([](auto& c){c.render_width=319;});Refuse([](auto& c){c.render_height=199;});
    Refuse([](auto& c){c.river_density_scale=std::numeric_limits<float>::quiet_NaN();});
    Refuse([](auto& c){c.terrain_detail_scale=1.41f;});Refuse([](auto& c){c.river_reflection_scale=.99f;});
    Refuse([](auto& c){c.leaf_vertex_path[0]=0;});Refuse([](auto& c){c.parallax_vertex_path[0]=0;});
    Refuse([](auto& c){c.flags=1;});
    Refuse([](auto& c){Assign(c.bump_vertex_path,c.parallax_vertex_path);});
    Refuse([](auto& c){Assign(c.leaf_vertex_path,L"relative/leaf.vs.bin");});
    Refuse([](auto& c){Assign(c.leaf_vertex_path,L"C:relative/leaf.vs.bin");});
    Refuse([](auto& c){Assign(c.leaf_vertex_path,L"C:\\Root\\..\\leaf.vs.bin");});
    Refuse([](auto& c){Assign(c.leaf_vertex_path,L"C:\\Root\\.\\leaf.vs.bin");});
    Refuse([](auto& c){Assign(c.leaf_vertex_path,L"C:\\Root\\leaf:alternate.vs.bin");});
    Refuse([](auto& c){Assign(c.leaf_vertex_path,L"C:\\Root\\CON.vs.bin");});
    Refuse([](auto& c){Assign(c.leaf_vertex_path,L"C:\\Root\\COM1.vs.bin");});
    Refuse([](auto& c){Assign(c.leaf_vertex_path,L"C:\\NUL\\leaf.vs.bin");});
    Refuse([](auto& c){Assign(c.leaf_vertex_path,L"C:\\Root\\LPT\x00b2.vs.bin");});
    Refuse([](auto& c){std::wstring path=L"C:\\Root\\";path.append(256,L'a');path+=L".vs.bin";Assign(c.leaf_vertex_path,path.c_str());});
    Refuse([](auto& c){Assign(c.leaf_vertex_path,L"\\\\?\\C:\\leaf.vs.bin");});
    Refuse([](auto& c){Assign(c.leaf_vertex_path,L"C:\\Root\\leaf.ps.bin");});
    Refuse([](auto& c){Assign(c.report_path,L"C:\\Root\\public.json");});
    Refuse([](auto& c){for(auto& x:c.leaf_vertex_path)x=L'x';});
    Refuse([](auto& c){c.leaf_vertex_path[511]=L'x';});
    auto unc=Valid();Assign(unc.leaf_vertex_path,L"\\\\server\\share\\leaf.vs.bin");Check(Validate(unc),"ordinary UNC path shape");
    Check(!Decode(nullptr,Bytes,&output)&&!Decode(&value,Bytes,nullptr),"null arguments refused");
    Check(!Decode(&value,4132,&output)&&!Decode(&value,Bytes+1,&output),"wrong allocation size refused");
    SYSTEM_INFO system{};GetSystemInfo(&system);const size_t page=system.dwPageSize;
    auto* pages=static_cast<unsigned char*>(VirtualAlloc(nullptr,page*2,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE));Check(pages!=nullptr,"guarded allocation");
    DWORD old=0;Check(VirtualProtect(pages+page,page,PAGE_NOACCESS,&old)!=0,"guard page");
    // V1 is larger than a typical single page. Place only its final bytes before
    // the guard; size rejection must not dereference this pointer at all.
    const void* old_pointer=pages+page-1;
    Check(!Decode(old_pointer,4132,&output),"legacy buffer rejected before any dereference");
    Check(VirtualFree(pages,0,MEM_RELEASE)!=0,"guarded allocation released");
    if(argc==2){
        HANDLE file=CreateFileW(argv[1],GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
        Check(file!=INVALID_HANDLE_VALUE,"open private Python transport fixture");
        Config incoming{};DWORD read=0;LARGE_INTEGER size{};
        const bool valid=GetFileSizeEx(file,&size)&&size.QuadPart==Bytes&&ReadFile(file,&incoming,Bytes,&read,nullptr)&&read==Bytes;
        CloseHandle(file);Check(valid&&Decode(&incoming,sizeof(incoming),&output),"Python-generated V2 transport accepted without runtime activation");
    }
    std::printf("material ConfigV2 CPU contract: %u checks passed; no GPU/game/effects linkage\n",checks);return 0;
}catch(const std::exception& e){std::fprintf(stderr,"ConfigV2 fixture failed: %s\n",e.what());return 1;}}
