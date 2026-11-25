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
Config ConfigFor(const NativeGraphicsConfig::Version2& native,Layout layout){
    Config value{};value.flags=Requested(native.flags);value.render_width=native.base.render_width;value.render_height=native.base.render_height;
    const std::wstring report=native.base.report_path;const auto slash=report.find_last_of(L'\\');const auto root=report.substr(0,slash);
    const auto base=root+(layout==Layout::PrivateCase?L"\\LocalData\\MaterialShaders\\":L"\\MaterialShaders\\");
    Assign(value.report_path,root+L"\\material-effects.private.json");
    wchar_t (*paths[])[512]={&value.leaf_vertex_path,&value.bump_pixel_path,&value.parallax_pixel_path,&value.bump_vertex_path,&value.parallax_vertex_path};
    for(size_t slot=0;slot<5;++slot)if(Requested(native.flags)&Programs[slot].role)
        Assign(*paths[slot],base+Programs[slot].directory+L"\\"+Programs[slot].digest+Programs[slot].suffix);
    return value;
}
void File(const std::wstring& path){
    std::filesystem::create_directories(std::filesystem::path(path).parent_path());
    HANDLE file=CreateFileW(path.c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);Check(file!=INVALID_HANDLE_VALUE,"new dummy artifact");
    const unsigned char value=0;DWORD count=0;const bool valid=WriteFile(file,&value,1,&count,nullptr)&&count==1;CloseHandle(file);Check(valid,"dummy artifact written");
}
}
int wmain(int argc,wchar_t** argv){using namespace NativeEffectsContractV2;try{
    Check(argc==2,"new private CPU fixture root");const std::wstring folder=argv[1];Check(CreateDirectoryW(folder.c_str(),nullptr)!=0,"new policy fixture root");
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
    std::printf("NativeEffectsContractV2 CPU policy: %u checks passed; no effects linkage, GPU or game\n",checks);return 0;
}catch(const std::exception& error){std::fprintf(stderr,"V2 policy fixture failed: %s\n",error.what());return 1;}}
