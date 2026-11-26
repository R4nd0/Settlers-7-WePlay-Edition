// SPDX-License-Identifier: MIT
// Pure policy/header CPU tests; dummy artifacts never enter the effects runtime.
#include "material_shader_effects_contract_v2.h"
#include <cstdio>
#include <filesystem>
#include <limits>
#include <stdexcept>

namespace {
using namespace NativeEffectsContractV2;
unsigned checks=0;
void Check(bool value,const char* text){++checks;if(!value)throw std::runtime_error(text);}
template<size_t N>void Assign(wchar_t (&output)[N],const std::wstring& value){std::wmemset(output,0,N);Check(wcscpy_s(output,value.c_str())==0,"bounded fixture path");}
NativeGraphicsConfig::Version2 Native(const std::wstring& root,DWORD intent=NativeGraphicsConfig::Terrain|NativeGraphicsConfig::Vegetation){
    NativeGraphicsConfig::Version2 native{};native.base.size=sizeof(native);native.base.version=2;native.flags=intent;
    native.base.render_width=3840;native.base.render_height=2160;native.base.output_width=1920;native.base.output_height=1080;
    Assign(native.base.report_path,root+L"\\native.private.json");return native;
}
Config ConfigFor(const NativeGraphicsConfig::Version2& native,Layout layout,bool macro=false){
    Config value{};value.flags=Requested(native.flags);value.render_width=native.base.render_width;value.render_height=native.base.render_height;
    const std::wstring report=native.base.report_path;const auto slash=report.find_last_of(L'\\');const auto root=report.substr(0,slash);
    const auto base=root+(layout==Layout::PrivateCase?L"\\LocalData\\MaterialShaders\\":L"\\MaterialShaders\\");
    Assign(value.report_path,root+L"\\material-effects.private.json");
    wchar_t (*paths[])[512]={&value.leaf_vertex_path,&value.bump_pixel_path,&value.parallax_pixel_path,&value.bump_vertex_path,&value.parallax_vertex_path};
    const auto& programs=macro?MacroPrograms:Programs;
    for(size_t slot=0;slot<5;++slot)if(Requested(native.flags)&programs[slot].role)
        Assign(*paths[slot],base+programs[slot].directory+L"\\"+programs[slot].digest+programs[slot].suffix);
    if(macro)value.terrain_detail_scale=value.terrain_relief_scale=1;
    return value;
}
void File(const std::wstring& path){
    std::filesystem::create_directories(std::filesystem::path(path).parent_path());
    HANDLE file=CreateFileW(path.c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);Check(file!=INVALID_HANDLE_VALUE,"new dummy artifact");
    const unsigned char value=0;DWORD count=0;const bool valid=WriteFile(file,&value,1,&count,nullptr)&&count==1;CloseHandle(file);Check(valid,"dummy artifact written");
}
}
int wmain(int argc,wchar_t** argv){using namespace NativeEffectsContractV2;try{
    Check(argc==2||argc==3,"new private CPU fixture root and optional owned actual config");const std::wstring folder=argv[1];Check(CreateDirectoryW(folder.c_str(),nullptr)!=0,"new policy fixture root");
    auto native=Native(folder);auto value=ConfigFor(native,Layout::PrivateCase);
    SetLastError(0x37573001);Check(Validate(value,native,Layout::PrivateCase,false)==Error::None,"valid fixed private paths");Check(GetLastError()==0x37573001,"policy preserves caller LastError");
    for(DWORD flags:{0u,2u,4u,6u}){auto request=value;request.flags=flags;Check(Validate(request,native,Layout::PrivateCase,false)==Error::None,"explicit safe-boundary mask subset");
        Config initial{};Check(PrepareInitial(request,native,Layout::PrivateCase,initial,false)==Error::None&&initial.flags==6,"initial intent preloads both declared roles even for off request");
        Check(SamePinnedConfig(request,value),"all bytes except flags match");}
    const auto Reject=[&](auto change,Error expected){auto request=value;change(request);Check(Validate(request,native,Layout::PrivateCase,false)==expected,"bad V2 request refused");};
    Reject([](auto& v){v.size=4132;},Error::Header);Reject([](auto& v){v.version=1;},Error::Header);
    Reject([](auto& v){v.flags=1;},Error::Flags);Reject([](auto& v){v.flags=7;},Error::Flags);Reject([](auto& v){v.flags=8;},Error::Flags);
    Reject([](auto& v){v.render_width=1920;},Error::Dimensions);
    Reject([](auto& v){v.terrain_detail_scale=std::numeric_limits<float>::quiet_NaN();},Error::Parameters);
    Reject([](auto& v){v.terrain_detail_scale=std::numeric_limits<float>::denorm_min();},Error::Parameters);
    Reject([](auto& v){v.terrain_relief_scale=1.3f;},Error::Parameters);
    Reject([](auto& v){Assign(v.bump_vertex_path,L"C:\\Elsewhere\\bad.vs.bin");},Error::OutsideRoot);
    Reject([&](auto& v){Assign(v.bump_vertex_path,folder+L"\\arbitrary.vs.bin");},Error::FixedPath);
    Reject([&](auto& v){Assign(v.report_path,folder+L"\\alternate.private.json");},Error::FixedPath);
    Reject([](auto& v){Assign(v.leaf_vertex_path,L"C:\\Root\\CON.vs.bin");},Error::Path);
    auto request=value;request.flags=0;std::memset(request.bump_vertex_path,0,sizeof(request.bump_vertex_path));
    Check(Validate(request,native,Layout::PrivateCase,false)==Error::Path,"off request cannot drop an initial-intent path");
    auto changed_native=native;changed_native.flags=NativeGraphicsConfig::Water;
    Check(Validate(value,changed_native,Layout::PrivateCase,false)==Error::Flags,"water-only native intent cannot enable V2 terrain or leaf");
    changed_native=native;changed_native.base.output_width=1919;
    Check(Validate(value,changed_native,Layout::PrivateCase,false)==Error::Dimensions,"native aspect mismatch refused");
    changed_native=native;changed_native.base.version=1;
    Check(Validate(value,changed_native,Layout::PrivateCase,false)==Error::Header,"native V1 context refused");
    for(DWORD intent:{NativeGraphicsConfig::Terrain,NativeGraphicsConfig::Vegetation}){auto subset_native=Native(folder,intent);auto subset=ConfigFor(subset_native,Layout::PrivateCase);
        Check(Validate(subset,subset_native,Layout::PrivateCase,false)==Error::None,"single declared role supported");
        subset.flags=0;Config initial{};Check(PrepareInitial(subset,subset_native,Layout::PrivateCase,initial,false)==Error::None&&initial.flags==Requested(intent),"single-role initial-off normalization");}
    for(size_t index=0;index<sizeof(value);++index){auto changed=value;reinterpret_cast<unsigned char*>(&changed)[index]^=1;
        const bool flag=index>=offsetof(Config,flags)&&index<offsetof(Config,flags)+sizeof(DWORD);
        Check(SamePinnedConfig(value,changed)==flag,"SamePinnedConfig covers every transport byte except flags");}
    const wchar_t* artifacts[]={value.leaf_vertex_path,value.bump_pixel_path,value.parallax_pixel_path,value.bump_vertex_path,value.parallax_vertex_path};
    Check(Validate(value,native,Layout::PrivateCase,true)!=Error::None,"missing artifacts refused before activation");
    for(const auto* path:artifacts)File(path);
    Check(Validate(value,native,Layout::PrivateCase,true)==Error::None,"all five regular fixed artifacts preflight");
    std::filesystem::create_directory(value.report_path);
    Check(Validate(value,native,Layout::PrivateCase,true)==Error::Path,"report directory refused");
    // Use a fresh root rather than deleting an owned negative fixture.
    auto recipient_native=Native(folder+L"\\recipient\\LocalData");auto recipient=ConfigFor(recipient_native,Layout::RecipientLocalData);
    Check(Validate(recipient,recipient_native,Layout::RecipientLocalData,false)==Error::None,"production LocalData layout fixed");
    Check(Validate(value,native,Layout::RecipientLocalData,false)==Error::Path,"private layout never silently falls back to production");
    Check(Validate(recipient,recipient_native,Layout::PrivateCase,false)==Error::FixedPath,"production layout never silently adds diagnostic LocalData");
    changed_native=recipient_native;changed_native.flags|=NativeGraphicsConfig::Observe;Assign(changed_native.catalog_path,folder+L"\\catalog.bin");changed_native.catalog_sha256[0]=1;Assign(changed_native.observer_report_path,recipient.report_path);
    Check(Validate(recipient,changed_native,Layout::RecipientLocalData,false)==Error::ReportCollision,"observer/effects report collision refused");
    // Full production-shaped macro path, including all five preflighted files.
    auto macro_native=Native(folder+L"\\macro-recipient\\LocalData");
    auto macro=ConfigFor(macro_native,Layout::RecipientLocalData,true);
    Check(Validate(macro,macro_native,Layout::RecipientLocalData,false)==Error::None,"complete macro set on wire2 accepted");
    const wchar_t* macro_artifacts[]={macro.leaf_vertex_path,macro.bump_pixel_path,macro.parallax_pixel_path,macro.bump_vertex_path,macro.parallax_vertex_path};
    for(const auto* path:macro_artifacts)File(path);
    Check(Validate(macro,macro_native,Layout::RecipientLocalData,true)==Error::None,"full production macro paths preflight");
    auto historical=ConfigFor(macro_native,Layout::RecipientLocalData);
    for(unsigned stage=0;stage<2;++stage){auto mixed=macro;
        if(stage==0)Assign(mixed.bump_vertex_path,historical.bump_vertex_path);else Assign(mixed.parallax_vertex_path,historical.parallax_vertex_path);
        Check(Validate(mixed,macro_native,Layout::RecipientLocalData,false)==Error::FixedPath,"mixed historical and macro VS rejected");}
    for(unsigned field=0;field<2;++field){auto gained=macro;if(field==0)gained.terrain_detail_scale=1.2f;else gained.terrain_relief_scale=1.1f;
        Check(Validate(gained,macro_native,Layout::RecipientLocalData,false)==Error::Parameters,"macro requires unit factors");}
    auto renamed=macro;Assign(renamed.bump_vertex_path,std::wstring(historical.bump_vertex_path).substr(0,std::wstring(historical.bump_vertex_path).find_last_of(L'\\')+1)+MacroPrograms[3].digest+L".vs.bin");
    Check(Validate(renamed,macro_native,Layout::RecipientLocalData,false)==Error::FixedPath,"macro hash in old recipe folder rejected");
    Check(!SamePinnedConfig(macro,historical),"safe-boundary hot toggle cannot switch recipes implicitly");
    for(DWORD flags:{0u,2u,4u,6u}){auto off=macro;off.flags=flags;Config initial{};
        Check(Validate(off,macro_native,Layout::RecipientLocalData,true)==Error::None,"macro subset toggle retains complete pinned paths");
        Check(PrepareInitial(off,macro_native,Layout::RecipientLocalData,initial,true)==Error::None&&initial.flags==6,"macro off preloads complete declared intent");
        Check(SamePinnedConfig(off,macro),"macro toggles change flags only");}
    auto terrain_native=Native(folder+L"\\macro-only",NativeGraphicsConfig::Terrain);
    auto terrain_macro=ConfigFor(terrain_native,Layout::PrivateCase,true);
    Check(Validate(terrain_macro,terrain_native,Layout::PrivateCase,false)==Error::None,"terrain-only private macro layout");
    auto hemi_native=Native(folder+L"\\leaf-hemi-only",NativeGraphicsConfig::Vegetation);
    auto hemi=ConfigFor(hemi_native,Layout::PrivateCase);
    const auto hemi_base=folder+L"\\leaf-hemi-only\\LocalData\\MaterialShaders\\";
    Assign(hemi.leaf_vertex_path,hemi_base+LeafHemisphereProgram.directory+L"\\"+LeafHemisphereProgram.digest+LeafHemisphereProgram.suffix);
    Check(Validate(hemi,hemi_native,Layout::PrivateCase,false)==Error::None,"explicit hemisphere fixed private path accepted");File(hemi.leaf_vertex_path);
    Check(Validate(hemi,hemi_native,Layout::PrivateCase,true)==Error::None,"hemisphere single artifact full host preflight");
    for(DWORD flags:{0u,4u}){auto toggle=hemi;toggle.flags=flags;Config initial{};
        Check(PrepareInitial(toggle,hemi_native,Layout::PrivateCase,initial,true)==Error::None&&initial.flags==4,"hemisphere initially off derives original Leaf intent");
        Check(SamePinnedConfig(toggle,hemi),"hemisphere toggle changes flags only");}
    const auto historical_leaf=ConfigFor(hemi_native,Layout::PrivateCase);Check(!SamePinnedConfig(hemi,historical_leaf),"old leaf cannot be silently repinned through toggle");
    auto mixed_native=hemi_native;mixed_native.flags|=NativeGraphicsConfig::Terrain;auto mixed_leaf=ConfigFor(mixed_native,Layout::PrivateCase);Assign(mixed_leaf.leaf_vertex_path,hemi.leaf_vertex_path);
    Check(Validate(mixed_leaf,mixed_native,Layout::PrivateCase,false)==Error::FixedPath,"complete terrain/hemisphere mixed set refused");
    auto wrong_hemi=hemi;Assign(wrong_hemi.leaf_vertex_path,hemi_base+Programs[0].directory+L"\\"+LeafHemisphereProgram.digest+L".vs.bin");
    Check(Validate(wrong_hemi,hemi_native,Layout::PrivateCase,false)==Error::FixedPath,"hemisphere hash in old folder refused");
    auto hemi_recipient_native=Native(folder+L"\\leaf-recipient\\LocalData",NativeGraphicsConfig::Vegetation);auto hemi_recipient=ConfigFor(hemi_recipient_native,Layout::RecipientLocalData);
    Assign(hemi_recipient.leaf_vertex_path,folder+L"\\leaf-recipient\\LocalData\\MaterialShaders\\"+LeafHemisphereProgram.directory+L"\\"+LeafHemisphereProgram.digest+L".vs.bin");File(hemi_recipient.leaf_vertex_path);
    Check(Validate(hemi_recipient,hemi_recipient_native,Layout::RecipientLocalData,true)==Error::None,"hemisphere exact recipient LocalData preflight");
    if(argc==3){Config actual{};HANDLE input=CreateFileW(argv[2],GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL|FILE_FLAG_OPEN_REPARSE_POINT,nullptr);
        Check(input!=INVALID_HANDLE_VALUE,"open actual recipient-generated config");LARGE_INTEGER size{};DWORD bytes=0;BY_HANDLE_FILE_INFORMATION info{};
        const bool read=GetFileInformationByHandle(input,&info)&&!(info.dwFileAttributes&FILE_ATTRIBUTE_REPARSE_POINT)
            &&GetFileSizeEx(input,&size)&&size.QuadPart==sizeof(actual)&&ReadFile(input,&actual,sizeof(actual),&bytes,nullptr)&&bytes==sizeof(actual);CloseHandle(input);Check(read,"bounded actual6180 read");
        const std::wstring report=actual.report_path;const auto slash=report.find_last_of(L'\\');const auto slash2=report.find_last_of(L'/');
        const auto separator=slash==std::wstring::npos?slash2:slash2==std::wstring::npos?slash:(std::max)(slash,slash2);
        Check(separator!=std::wstring::npos,"actual report root");auto actual_native=Native(report.substr(0,separator),(actual.bump_pixel_path[0]?NativeGraphicsConfig::Terrain:0)|(actual.leaf_vertex_path[0]?NativeGraphicsConfig::Vegetation:0));
        actual_native.base.render_width=actual.render_width;actual_native.base.render_height=actual.render_height;
        Check(Validate(actual,actual_native,Layout::PrivateCase,true)==Error::None,"actual recipient-source macro config passes HOST contract preflight");}
    std::printf("NativeEffectsContractV2 CPU policy: %u checks passed; no effects linkage, GPU or game\n",checks);return 0;
}catch(const std::exception& error){std::fprintf(stderr,"V2 policy fixture failed: %s\n",error.what());return 1;}}
