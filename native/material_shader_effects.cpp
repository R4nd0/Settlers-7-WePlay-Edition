#include "material_shader_effects.h"
#include "material_shader_config_v2.h"
#include "material_shader_config_v3.h"
#include "material_shader_config_v4.h"
#include "terrain_macro_guard.h"
#include <bcrypt.h>
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <float.h>
#include <intrin.h>
#include <string>
#include <vector>

namespace MaterialShaderEffects {
namespace {
constexpr size_t MaxCode=256*1024,MaxCache=256;
struct Guard {
    alignas(16) unsigned char fp[512];DWORD error=GetLastError();DWORD alignment_padding[3]{};
    Guard(){_fxsave(fp);unsigned int ignored=0;_controlfp_s(&ignored,_MCW_EM|_RC_NEAR,_MCW_EM|_MCW_RC);}
    ~Guard(){_fxrstor(fp);SetLastError(error);}
};
struct Lock {SRWLOCK* mutex;explicit Lock(SRWLOCK& value):mutex(&value){AcquireSRWLockExclusive(mutex);}~Lock(){ReleaseSRWLockExclusive(mutex);}};
struct File {
    HANDLE value=INVALID_HANDLE_VALUE;
    ~File(){if(value!=INVALID_HANDLE_VALUE)CloseHandle(value);}
};
template<class T>struct Ref {
    T* value=nullptr;
    Ref()=default;Ref(const Ref&)=delete;Ref& operator=(const Ref&)=delete;
    ~Ref(){if(value)value->Release();}
    T* release()noexcept{auto* result=value;value=nullptr;return result;}
};
struct Identity {DWORD bytes;const char* text;std::array<unsigned char,32> hash{};};
struct Role {Identity vertex,pixel;const char* replacement_text;std::array<unsigned char,32> replacement{};};
std::array<Role,4> roles{{
    {{2592,"f190d66ee1ae43637341a76e5b644cc63bf3c2a5b5c94ea9286764e13f1c16c0",{}},{3740,"dd65ba0b9d79c7c4d9ad9904404388230556b194eedb8cbdfbd4fa99f44eee5b",{}},nullptr,{}},
    {{2004,"6daa00fadd5f62f1fff27c3221154af9b83ed36225496ab981e9b624bf73a0cb",{}},{1636,"811ea82dae81585cd9f795efd2c10918cd78cbf53d89fe62be0475b6bacf5f18",{}},"c2ae22a6739e071774edd071870e6dc8fd058b5bf6eec6490bc1a4603fea2fa3",{}},
    {{2004,"41abe0979ae889b7f4e8cc51eea3fea5e7355649e1c2aca5fafb1e0e89a20daa",{}},{3396,"e776a4441563c4a3fe073811d694246c7245262244b77c614fddfe4f43144191",{}},"d0d082bbb9b8d2de1388b790165bcebc5fda571d88895db3e8b36004bf2e52fa",{}},
    {{3832,"15701e004a296b3137122207b4557c71700c20aa23eda1b01482c45223ece0df",{}},{1328,"2e633d1cd30a2db140fc338938864d909141f6cb9fd702e40ab6b89b141d90be",{}},"2797260e11f28f67cecacd2e79f0df1064bb3ed07ffdc81ab129db2963a1cbbe",{}}
}};
struct Cached {IUnknown* object=nullptr;DWORD stage=0,mask=0;};
struct ProgramPin {DWORD bytes=0,stage=0;const char* text=nullptr;std::array<unsigned char,32> hash{};};
std::array<std::array<ProgramPin,5>,6> programs{{
    {{{3872,1,"2797260e11f28f67cecacd2e79f0df1064bb3ed07ffdc81ab129db2963a1cbbe",{}},
      {1836,2,"c2ae22a6739e071774edd071870e6dc8fd058b5bf6eec6490bc1a4603fea2fa3",{}},
      {3596,2,"d0d082bbb9b8d2de1388b790165bcebc5fda571d88895db3e8b36004bf2e52fa",{}},{},{}}},
    {{{3828,1,"119352b9def2ba290c8a5bc99b18b77cf4730461e97d0fbf677fba104e1e472a",{}},
      {1836,2,"c2ae22a6739e071774edd071870e6dc8fd058b5bf6eec6490bc1a4603fea2fa3",{}},
      {3596,2,"d0d082bbb9b8d2de1388b790165bcebc5fda571d88895db3e8b36004bf2e52fa",{}},
      {2004,1,"46a8dd89646f84cb761f832f47739f3d0c4fe5494688b3104b56c43fac8d3f07",{}},
      {2004,1,"d985366573816225b04473b762fd6f899241849185274347f513c1f4cf7de6c6",{}}}},
    {{{3828,1,"119352b9def2ba290c8a5bc99b18b77cf4730461e97d0fbf677fba104e1e472a",{}},
      {1836,2,"c2ae22a6739e071774edd071870e6dc8fd058b5bf6eec6490bc1a4603fea2fa3",{}},
      {3596,2,"d0d082bbb9b8d2de1388b790165bcebc5fda571d88895db3e8b36004bf2e52fa",{}},
      {2020,1,"da8154477a4fd3c44ace75d9c42d6d3714dc196c9c0f30fc332f06493539a21b",{}},
      {2020,1,"b421d294516b89d0e0de90af56982b76ad8d196808c2d548ed251a3312313c7d",{}}}},
    {{{3852,1,"ffb6d89661e9ae8a9d75ba3d322a318996d07d1dbb81281fefdb4b8df2f277f7",{}},{},{},{},{}}},
    {{{1372,2,"284d5c84ee6700039ee818fe7f063b2b90536beac45e261a5b3e0cf8a6d1746a",{}},{},{},{},{}}},
    {{{1372,2,"1b825358f46c39b04b1d540738f4be5e862bfe6627b8545ab092c471d259477c",{}},
      {1372,2,"284d5c84ee6700039ee818fe7f063b2b90536beac45e261a5b3e0cf8a6d1746a",{}},
      {1372,2,"b869fe375b76509f60a48d34bde83565d28c0416aca85f27c8a445045cb6a51f",{}},{},{}}}
}};
struct State {
    Config config{};Stats stats{};IDirect3DDevice9* device=nullptr;DWORD thread=0,program_variant=0;
    BCRYPT_ALG_HANDLE algorithm=nullptr;
    std::array<Cached,MaxCache> cache{};size_t count=0;
    std::array<unsigned char,MaxCode> scratch{};
    std::array<std::vector<unsigned char>,5> code{};
    std::array<std::array<unsigned char,32>,5> loaded_hashes{};
    std::array<HANDLE,5> files{INVALID_HANDLE_VALUE,INVALID_HANDLE_VALUE,INVALID_HANDLE_VALUE,INVALID_HANDLE_VALUE,INVALID_HANDLE_VALUE};
    IDirect3DVertexShader9* leaf=nullptr;
    IDirect3DPixelShader9* leaf_pixel=nullptr;
    std::array<IDirect3DPixelShader9*,3> leaf_palette{};
    IDirect3DPixelShader9* bump=nullptr;IDirect3DPixelShader9* parallax=nullptr;
    IDirect3DVertexShader9* bump_vertex=nullptr;IDirect3DVertexShader9* parallax_vertex=nullptr;
    DrawTransaction* transaction=nullptr;
    IDirect3DVertexShader9* active_vertex=nullptr;IDirect3DPixelShader9* active_pixel=nullptr;
    bool unrestored=false;
    std::array<DWORD,4> sample_counts{};
};
State current;SRWLOCK mutex=SRWLOCK_INIT;
void EventReport() noexcept;
#ifdef S7_MATERIAL_EFFECTS_FIXTURE
std::array<bool,4> fixture_override{};
std::array<std::array<bool,5>,6> fixture_program_override{};
#endif
bool Finite(float value){DWORD bits=0;std::memcpy(&bits,&value,4);return (bits&0x7f800000u)!=0x7f800000u;}
bool Between(float value,float low,float high){return Finite(value)&&value>=low&&value<=high;}
bool Vector(const float* value){return value && Finite(value[0])&&Finite(value[1])&&Finite(value[2])&&Finite(value[3]);}
bool Absolute(const wchar_t* path){
    const size_t length=path?wcsnlen_s(path,512):0;
    if(length<3||length>=512)return false;
    const bool drive=((path[0]>=L'A'&&path[0]<=L'Z')||(path[0]>=L'a'&&path[0]<=L'z'))&&path[1]==L':'&&(path[2]==L'\\'||path[2]==L'/');
    const bool unc=length>4&&path[0]==L'\\'&&path[1]==L'\\'&&path[2]!=L'.'&&path[2]!=L'?';
    if(!drive&&!unc)return false;
    std::wstring normalized(path);std::replace(normalized.begin(),normalized.end(),L'/',L'\\');
    wchar_t full[512]{};const DWORD count=GetFullPathNameW(normalized.c_str(),512,full,nullptr);
    return count&&count<512&&!_wcsicmp(normalized.c_str(),full);
}
bool PrivateReport(const wchar_t* path){const size_t length=wcsnlen_s(path,512);constexpr wchar_t ending[]=L".private.json";constexpr size_t suffix=sizeof(ending)/sizeof(wchar_t)-1;return length>=suffix&&length<508&&Absolute(path)&&!_wcsicmp(path+length-suffix,ending);}
void Exception()noexcept{Lock lock(mutex);++current.stats.exceptions;current.stats.faulted=1;EventReport();}
void Hex(const char* text,std::array<unsigned char,32>& result){for(size_t i=0;i<32;++i){auto digit=[](char c){return c<='9'?c-'0':c-'a'+10;};result[i]=static_cast<unsigned char>((digit(text[i*2])<<4)|digit(text[i*2+1]));}}
void PrepareProgramPins(unsigned version){
    for(size_t slot=0;slot<programs[version-1].size();++slot){auto& pin=programs[version-1][slot];
#ifdef S7_MATERIAL_EFFECTS_FIXTURE
        if(fixture_program_override[version-1][slot])continue;
#endif
        if(pin.text)Hex(pin.text,pin.hash);}
}
std::wstring NormalPath(const wchar_t* value){std::wstring path(value);std::replace(path.begin(),path.end(),L'/',L'\\');return path;}
bool PlainAncestors(const std::wstring& path,bool report){
    size_t root=3;
    if(path.size()>2&&path[0]==L'\\'&&path[1]==L'\\'){
        const auto server=path.find(L'\\',2);if(server==std::wstring::npos)return false;
        const auto share=path.find(L'\\',server+1);if(share==std::wstring::npos)return false;root=share+1;
    }
    std::wstring part=path;bool leaf=true;
    for(;;){
        const DWORD attributes=GetFileAttributesW(part.c_str());
        if(attributes==INVALID_FILE_ATTRIBUTES){if(!(leaf&&report&&GetLastError()==ERROR_FILE_NOT_FOUND))return false;}
        else if((attributes&FILE_ATTRIBUTE_REPARSE_POINT)||(leaf?(attributes&FILE_ATTRIBUTE_DIRECTORY)!=0:(attributes&FILE_ATTRIBUTE_DIRECTORY)==0))return false;
        if(part.size()<=root)break;const auto separator=part.find_last_of(L'\\');
        if(separator==std::wstring::npos)return false;part.resize(separator<root?root:separator);leaf=false;
    }
    return true;
}
bool V2Paths(const MaterialShaderConfigV2::Config& config,unsigned pin_version=2){
    const auto report=NormalPath(config.report_path);if(!Absolute(config.report_path)||!PlainAncestors(report,true))return false;
    const auto separator=report.find_last_of(L'\\');if(separator==std::wstring::npos)return false;
    const auto root=report.substr(0,separator+1);
    const wchar_t* paths[]={config.leaf_vertex_path,config.bump_pixel_path,config.parallax_pixel_path,config.bump_vertex_path,config.parallax_vertex_path};
    constexpr wchar_t digits[]=L"0123456789abcdef";
    for(size_t slot=0;slot<5;++slot){if(!paths[slot][0])continue;const auto path=NormalPath(paths[slot]);
        if(!Absolute(paths[slot])||path.size()<=root.size()||_wcsnicmp(path.c_str(),root.c_str(),root.size())||!PlainAncestors(path,false))return false;
        std::wstring expected;for(unsigned char value:programs[pin_version-1][slot].hash){expected+=digits[value>>4];expected+=digits[value&15];}
        expected+=programs[pin_version-1][slot].stage==1?L".vs.bin":L".ps.bin";
        const auto slash=path.find_last_of(L'\\');if(slash==std::wstring::npos||_wcsicmp(path.c_str()+slash+1,expected.c_str()))return false;
        if(pin_version==3&&slot>=3){
            const auto parent=path.find_last_of(L'\\',slash-1);
            const wchar_t* recipe=slot==3?L"terrain-macro-relief-BumpLayerShader-v3":L"terrain-macro-relief-ParallaxLayerShader-v3";
            if(parent==std::wstring::npos||_wcsicmp(path.substr(parent+1,slash-parent-1).c_str(),recipe))return false;
        }
        if(pin_version==4){
            const auto parent=path.find_last_of(L'\\',slash-1);
            if(slot!=0||parent==std::wstring::npos||_wcsicmp(path.substr(parent+1,slash-parent-1).c_str(),L"leaf-hemisphere-contrast-v3"))return false;
        }
    }
    return true;
}
bool V3Paths(const MaterialShaderConfigV3::Config& config){
    const auto report=NormalPath(config.report_path);const auto path=NormalPath(config.leaf_pixel_path);
    if(!Absolute(config.report_path)||!PlainAncestors(report,true)||!Absolute(config.leaf_pixel_path)||!PlainAncestors(path,false))return false;
    const auto separator=report.find_last_of(L'\\');if(separator==std::wstring::npos)return false;
    const auto root=report.substr(0,separator+1);
    if(path.size()<=root.size()||_wcsnicmp(path.c_str(),root.c_str(),root.size()))return false;
    // Canonical recipe filename is immutable even in authored GPU fixtures;
    // fixture-only content identity overrides remain independently hash-pinned.
    std::wstring expected;for(size_t i=0;i<64;++i)expected+=static_cast<wchar_t>(programs[4][0].text[i]);expected+=L".ps.bin";
    const auto slash=path.find_last_of(L'\\');if(slash==std::wstring::npos||_wcsicmp(path.c_str()+slash+1,expected.c_str()))return false;
    const auto parent=path.find_last_of(L'\\',slash-1);
    return parent!=std::wstring::npos&&!_wcsicmp(path.substr(parent+1,slash-parent-1).c_str(),L"leaf-wrapped-diffuse-v4");
}
bool V4Paths(const MaterialShaderConfigV4::Config& config){
    const auto report=NormalPath(config.report_path);if(!Absolute(config.report_path)||!PlainAncestors(report,true))return false;
    const auto separator=report.find_last_of(L'\\');if(separator==std::wstring::npos)return false;const auto root=report.substr(0,separator+1);
    constexpr const wchar_t* names[]={L"leaf-palette-A-v5",L"leaf-palette-B-v6",L"leaf-palette-C-v7"};
    for(unsigned i=0;i<3;++i){const auto path=NormalPath(config.program_paths[i]);
        if(!Absolute(config.program_paths[i])||!PlainAncestors(path,false)||path.size()<=root.size()||_wcsnicmp(path.c_str(),root.c_str(),root.size()))return false;
        std::wstring expected;for(size_t j=0;j<64;++j)expected+=static_cast<wchar_t>(programs[5][i].text[j]);expected+=L".ps.bin";
        const auto slash=path.find_last_of(L'\\');if(slash==std::wstring::npos||_wcsicmp(path.c_str()+slash+1,expected.c_str()))return false;
        const auto parent=path.find_last_of(L'\\',slash-1);if(parent==std::wstring::npos||_wcsicmp(path.substr(parent+1,slash-parent-1).c_str(),names[i]))return false;
    }return true;
}
bool Hash(const void* data,size_t bytes,std::array<unsigned char,32>& output){
    BCRYPT_HASH_HANDLE hash=nullptr;bool valid=current.algorithm&&bytes<=MAXDWORD&&BCryptCreateHash(current.algorithm,&hash,nullptr,0,nullptr,0,0)>=0;
    if(valid)valid=BCryptHashData(hash,const_cast<PUCHAR>(static_cast<const unsigned char*>(data)),static_cast<ULONG>(bytes),0)>=0;
    if(valid)valid=BCryptFinishHash(hash,output.data(),32,0)>=0;if(hash)BCryptDestroyHash(hash);return valid;
}
void ClearFrame(){
    for(size_t i=0;i<current.count;++i){IUnknown* object=current.cache[i].object;current.cache[i]=Cached{};if(object)object->Release();}
    current.count=0;current.stats.cached_refs=0;
}
void CountPrograms(){
    current.stats.retained_programs=(current.leaf?1u:0u)+(current.leaf_pixel?1u:0u)+(current.bump?1u:0u)+(current.parallax?1u:0u)
        +(current.bump_vertex?1u:0u)+(current.parallax_vertex?1u:0u);
    for(auto* shader:current.leaf_palette)if(shader)++current.stats.retained_programs;
    current.stats.peak_retained_programs=std::max(current.stats.peak_retained_programs,current.stats.retained_programs);
}
void ReleaseActiveOriginals(){
    auto* vertex=current.active_vertex;auto* pixel=current.active_pixel;
    current.active_vertex=nullptr;current.active_pixel=nullptr;current.stats.active_original_refs=0;
    if(vertex)vertex->Release();if(pixel)pixel->Release();
}
void Clear(){
    ClearFrame();
    auto* leaf=current.leaf;auto* leaf_pixel=current.leaf_pixel;auto* bump=current.bump;auto* parallax=current.parallax;
    auto* bump_vertex=current.bump_vertex;auto* parallax_vertex=current.parallax_vertex;
    current.leaf=nullptr;current.leaf_pixel=nullptr;current.bump=nullptr;current.parallax=nullptr;current.bump_vertex=nullptr;current.parallax_vertex=nullptr;CountPrograms();
    if(leaf)leaf->Release();if(leaf_pixel)leaf_pixel->Release();if(bump)bump->Release();if(parallax)parallax->Release();
    for(auto*& shader:current.leaf_palette){auto* held=shader;shader=nullptr;if(held)held->Release();}CountPrograms();
    if(bump_vertex)bump_vertex->Release();if(parallax_vertex)parallax_vertex->Release();
    ReleaseActiveOriginals();
}
void CloseFiles(){for(auto& file:current.files)if(file!=INVALID_HANDLE_VALUE){CloseHandle(file);file=INVALID_HANDLE_VALUE;}for(auto& code:current.code)code.clear();current.loaded_hashes={};}
bool Load(unsigned index,const wchar_t* path,unsigned version){
    if(version<1||version>6||index>=5)return false;const auto& pin=programs[version-1][index];if(!pin.stage)return false;
    if(!Absolute(path))return false;
    File file{CreateFileW(path,GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL|FILE_FLAG_OPEN_REPARSE_POINT,nullptr)};
    if(file.value==INVALID_HANDLE_VALUE)return false;
    LARGE_INTEGER size{};BY_HANDLE_FILE_INFORMATION info{};bool valid=GetFileInformationByHandle(file.value,&info)&&!(info.dwFileAttributes&FILE_ATTRIBUTE_REPARSE_POINT)
        &&GetFileSizeEx(file.value,&size)&&size.QuadPart>=12&&size.QuadPart<=static_cast<LONGLONG>(MaxCode)&&!(size.QuadPart%4);
    DWORD bytes=0;
    if(valid){current.code[index].resize(static_cast<size_t>(size.QuadPart));valid=ReadFile(file.value,current.code[index].data(),static_cast<DWORD>(size.QuadPart),&bytes,nullptr)&&static_cast<LONGLONG>(bytes)==size.QuadPart;}
    std::array<unsigned char,32> hash{};if(valid)valid=(!pin.bytes||bytes==pin.bytes)&&Hash(current.code[index].data(),bytes,hash)&&hash==pin.hash;
    DWORD model=0;if(valid)std::memcpy(&model,current.code[index].data(),4);
    if(valid)valid=model==(pin.stage==1?0xfffe0300u:0xffff0300u);
    if(valid){current.loaded_hashes[index]=hash;current.files[index]=file.value;file.value=INVALID_HANDLE_VALUE;}else current.code[index].clear();return valid;
}
bool Context(IDirect3DDevice9* device){
    Ref<IDirect3DSurface9> target;D3DSURFACE_DESC desc{};D3DVIEWPORT9 viewport{};DWORD alpha=0,srgb=0;
    bool valid=SUCCEEDED(device->GetRenderTarget(0,&target.value))&&target.value&&SUCCEEDED(target.value->GetDesc(&desc));
    valid=valid&&desc.Width==current.config.render_width&&desc.Height==current.config.render_height&&desc.Format==D3DFMT_A8R8G8B8
        &&SUCCEEDED(device->GetViewport(&viewport))&&!viewport.X&&!viewport.Y&&viewport.Width==desc.Width&&viewport.Height==desc.Height
        &&SUCCEEDED(device->GetRenderState(D3DRS_ALPHATESTENABLE,&alpha))&&!alpha
        &&SUCCEEDED(device->GetRenderState(D3DRS_SRGBWRITEENABLE,&srgb))&&!srgb;
    return valid;
}
template<class Shader> DWORD Classify(Shader* shader,DWORD stage){
    if(!shader)return 0;
    for(size_t i=0;i<current.count;++i)if(current.cache[i].object==static_cast<IUnknown*>(shader)&&current.cache[i].stage==stage)return current.cache[i].mask;
    UINT bytes=0;std::array<unsigned char,32> hash{};DWORD mask=0;
    bool valid=SUCCEEDED(shader->GetFunction(nullptr,&bytes))&&bytes&&bytes<=MaxCode&&!(bytes%4);const UINT expected=bytes;
    if(!valid){++current.stats.query_failures;return 0;}
    bool known_size=false;for(const auto& role:roles)if((stage==1?role.vertex:role.pixel).bytes==bytes)known_size=true;
    if(!known_size)return 0; // Unknown sizes never enter GetFunction(body)/SHA hot path.
    if(valid)valid=SUCCEEDED(shader->GetFunction(current.scratch.data(),&bytes))&&bytes==expected&&Hash(current.scratch.data(),bytes,hash);
    if(!valid){++current.stats.query_failures;return 0;}
    for(size_t i=0;i<roles.size();++i){const auto& key=stage==1?roles[i].vertex:roles[i].pixel;if(key.bytes==bytes&&key.hash==hash)mask|=1u<<i;}
    return mask; // A full positive cache never prevents checking a new identity.
}
template<class Shader>void Remember(Shader* shader,DWORD stage,DWORD mask){
    if(!shader||!mask)return;
    for(size_t i=0;i<current.count;++i)if(current.cache[i].object==static_cast<IUnknown*>(shader)&&current.cache[i].stage==stage)return;
    if(current.count==MaxCache){++current.stats.cache_overflows;return;}
    shader->AddRef();current.cache[current.count++]={shader,stage,mask};current.stats.cached_refs=static_cast<DWORD>(current.count);
}
bool ReadConstant(IDirect3DDevice9* device,ConstantBackup& value){return SUCCEEDED(value.stage==1?device->GetVertexShaderConstantF(value.index,value.original.data(),1):device->GetPixelShaderConstantF(value.index,value.original.data(),1))&&Vector(value.original.data());}
HRESULT SetConstant(IDirect3DDevice9* device,const ConstantBackup& value,bool original){const auto& data=original?value.original:value.applied;return value.stage==1?device->SetVertexShaderConstantF(value.index,data.data(),1):device->SetPixelShaderConstantF(value.index,data.data(),1);}
bool Restore(DrawTransaction& draw){
    bool valid=true;
    for(DWORD i=0;i<draw.touched_constants;++i)if(FAILED(SetConstant(draw.device,draw.constants[i],true)))valid=false;
    if(draw.touched_vertex&&FAILED(draw.device->SetVertexShader(draw.original_vertex)))valid=false;
    if(draw.touched_pixel&&FAILED(draw.device->SetPixelShader(draw.original_pixel)))valid=false;
    for(DWORD i=0;i<draw.touched_constants;++i){std::array<float,4> got{};const auto& c=draw.constants[i];
        HRESULT hr=c.stage==1?draw.device->GetVertexShaderConstantF(c.index,got.data(),1):draw.device->GetPixelShaderConstantF(c.index,got.data(),1);
        if(FAILED(hr)||std::memcmp(got.data(),c.original.data(),sizeof(got)))valid=false;}
    if(draw.touched_vertex){Ref<IDirect3DVertexShader9> got;HRESULT hr=draw.device->GetVertexShader(&got.value);if(FAILED(hr)||got.value!=draw.original_vertex)valid=false;}
    if(draw.touched_pixel){Ref<IDirect3DPixelShader9> got;HRESULT hr=draw.device->GetPixelShader(&got.value);if(FAILED(hr)||got.value!=draw.original_pixel)valid=false;}
    current.unrestored=!valid;if(valid){if(current.transaction==&draw)current.transaction=nullptr;ReleaseActiveOriginals();}
    if(!valid){++current.stats.restore_failures;current.stats.faulted=1;EventReport();}return valid;
}
struct Mutation {
    DrawTransaction& draw;DrawTransaction* output;bool committed=false;
    ~Mutation()noexcept{
        if(committed || (!draw.touched_constants&&!draw.touched_vertex&&!draw.touched_pixel))return;
        try{draw.unsafe_state=!Restore(draw);}catch(...){draw.unsafe_state=true;current.unrestored=true;++current.stats.restore_failures;++current.stats.exceptions;current.stats.faulted=1;}
        draw.active=false;*output=draw;if(draw.unsafe_state)current.transaction=output;else if(current.transaction==output)current.transaction=nullptr;
        EventReport();
    }
};
struct PendingOriginals {
    bool committed=false;
    ~PendingOriginals(){if(!committed&&!current.unrestored)ReleaseActiveOriginals();}
};
void Report(){
    if(!current.config.report_path[0])return;const auto& s=current.stats;const DWORD terrain_variant=current.program_variant>=4?0u:current.program_variant;
    const DWORD leaf_variant=(s.initialized_flags&Leaf)?(current.program_variant==6?5u+s.selected_variant:current.program_variant==5?4u:current.program_variant==4?3u:current.program_variant==1?1u:2u):0u;char text[2048]{};
    std::snprintf(text,sizeof(text),"{\"format\":\"s7-native-material-effects-v1\",\"enabled_flags\":%lu,\"faulted\":%lu,\"draws\":%llu,\"matched\":%llu,\"restored_draws\":%llu,\"restore_failures\":%llu,\"context_rejections\":%llu,\"input_rejections\":%llu,\"query_failures\":%llu,\"shader_creations\":%llu,\"shader_failures\":%llu,\"set_failures\":%llu,\"failed_draws\":%llu,\"cached_refs\":%lu,\"retained_programs\":%lu,\"peak_retained_programs\":%lu,\"frames\":%llu,\"resets\":%llu,\"cache_overflows\":%llu,\"applied\":[%llu,%llu,%llu,%llu],\"samples\":[",
        s.enabled_flags,s.faulted,s.draws,s.matched,s.restored_draws,s.restore_failures,s.context_rejections,s.input_rejections,s.query_failures,s.shader_creations,s.shader_failures,s.set_failures,s.failed_draws,s.cached_refs,s.retained_programs,s.peak_retained_programs,s.frames,s.resets,s.cache_overflows,s.applied[0],s.applied[1],s.applied[2],s.applied[3]);
    std::string json=text;
    for(DWORD i=0;i<s.sample_count;++i){const auto& sample=s.samples[i];if(i)json+=',';std::snprintf(text,sizeof(text),"{\"role\":%lu,\"epoch\":%llu,\"restored\":%lu,\"shader_changed\":%lu,\"vertex_changed\":%lu,\"pixel_changed\":%lu,\"constants\":[",sample.role,sample.epoch,sample.restored,sample.shader_changed,sample.vertex_changed,sample.pixel_changed);json+=text;
        for(DWORD j=0;j<sample.constant_count;++j){const auto& c=sample.constants[j];if(j)json+=',';
            std::snprintf(text,sizeof(text),"{\"stage\":%lu,\"index\":%lu,\"original\":[%.9g,%.9g,%.9g,%.9g],\"applied\":[%.9g,%.9g,%.9g,%.9g]}",c.stage,c.index,c.original[0],c.original[1],c.original[2],c.original[3],c.applied[0],c.applied[1],c.applied[2],c.applied[3]);json+=text;}
        json+="]}";}
    std::snprintf(text,sizeof(text),"],\"initialized_flags\":%lu,\"active_transactions\":%lu,\"active_original_refs\":%lu,\"periodic_reporting\":%lu,\"enable_changes\":%llu,\"last_change_frame\":%llu,\"config_version\":%lu,\"terrain_recipe_variant\":%lu,\"leaf_recipe_variant\":%lu,\"selected_variant\":%lu,\"program_creations\":[%llu,%llu,%llu,%llu,%llu]}\n",s.initialized_flags,s.active_transactions,s.active_original_refs,s.periodic_reporting,s.enable_changes,s.last_change_frame,s.config_version,terrain_variant,leaf_variant,s.selected_variant,s.program_creations[0],s.program_creations[1],s.program_creations[2],s.program_creations[3],s.program_creations[4]);json+=text;
    if(!json.empty()&&json.back()=='\n')json.pop_back();if(!json.empty()&&json.back()=='}')json.pop_back();
    const auto triple=[&](const char* name,const std::array<std::uint64_t,3>& v){std::snprintf(text,sizeof(text),",\"%s\":[%llu,%llu,%llu]",name,v[0],v[1],v[2]);json+=text;};
    triple("variant_applications",s.variant_applications);triple("variant_restorations",s.variant_restorations);
    triple("variant_successful_draws",s.variant_successful_draws);triple("variant_failed_draws",s.variant_failed_draws);triple("variant_creations",s.variant_creations);json+="}\n";
    const std::wstring temporary=std::wstring(current.config.report_path)+L".tmp";HANDLE file=CreateFileW(temporary.c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);
    if(file==INVALID_HANDLE_VALUE){++current.stats.report_failures;return;}DWORD count=0;bool valid=WriteFile(file,json.data(),static_cast<DWORD>(json.size()),&count,nullptr)&&count==json.size();CloseHandle(file);
    if(valid)valid=MoveFileExW(temporary.c_str(),current.config.report_path,MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)!=0;if(!valid)++current.stats.report_failures;
}
void EventReport() noexcept{try{Report();}catch(...){++current.stats.report_failures;}}
bool MacroInputs(IDirect3DDevice9* device,bool parallax){
    std::array<float,4> region{},detail{};
    if(FAILED(device->GetVertexShaderConstantF(66,region.data(),1))
        ||FAILED(device->GetVertexShaderConstantF(67,detail.data(),1))
        ||!TerrainMacroGuard::Constants(region.data(),detail.data()))return false;
    for(unsigned sampler=4;sampler<=(parallax?6u:5u);++sampler){
        Ref<IDirect3DBaseTexture9> texture;
        if(FAILED(device->GetTexture(sampler,&texture.value))||!texture.value||texture.value->GetType()!=D3DRTYPE_TEXTURE)return false;
        auto* plain=static_cast<IDirect3DTexture9*>(texture.value);D3DSURFACE_DESC desc{};
        if(FAILED(plain->GetLevelDesc(0,&desc))||!TerrainMacroGuard::Texture(sampler,D3DRTYPE_TEXTURE,plain->GetLevelCount(),desc))return false;
    }
    return true;
}
struct InitFiles {
    bool committed=false;
    ~InitFiles(){if(!committed){CloseFiles();if(current.algorithm){BCryptCloseAlgorithmProvider(current.algorithm,0);current.algorithm=nullptr;}}}
};
bool InitializeCommon(const Config& config,unsigned version,const wchar_t* bump_vertex=nullptr,const wchar_t* parallax_vertex=nullptr,unsigned pin_version=0,const wchar_t* leaf_pixel=nullptr,const wchar_t (*palette)[512]=nullptr,DWORD selected=MAXDWORD){
    if(!pin_version)pin_version=version;
    if(current.stats.initialized_flags||current.stats.active_transactions||current.unrestored||current.transaction||config.size!=sizeof(Config)||config.version!=1
        ||!config.flags||(config.flags&~AllowedFlags)||config.render_width<320||config.render_height<200
        ||config.render_width>8192||config.render_height>8192||static_cast<std::uint64_t>(config.render_width)*config.render_height>32u*1024u*1024u
        ||!PrivateReport(config.report_path)||!Between(config.river_density_scale,.85f,1)||!Between(config.river_reflection_scale,1,1.3f)
        ||!Between(config.terrain_detail_scale,1,1.4f)||!Between(config.terrain_relief_scale,1,1.2f))return false;
    for(size_t index=0;index<roles.size();++index){auto& role=roles[index];
#ifdef S7_MATERIAL_EFFECTS_FIXTURE
        if(fixture_override[index])continue;
#endif
        Hex(role.vertex.text,role.vertex.hash);Hex(role.pixel.text,role.pixel.hash);if(role.replacement_text)Hex(role.replacement_text,role.replacement);}
    PrepareProgramPins(pin_version);
    InitFiles pending;
    if(!current.algorithm&&BCryptOpenAlgorithmProvider(&current.algorithm,BCRYPT_SHA256_ALGORITHM,nullptr,0)<0)return false;
    bool valid=true;
    if(config.flags&Leaf){if(version==4){valid=palette&&selected<3;for(unsigned i=0;valid&&i<3;++i)valid=Load(i,palette[i],pin_version);}
        else valid=Load(0,leaf_pixel?leaf_pixel:config.leaf_vertex_path,pin_version);}
    if(valid&&(config.flags&Terrain))valid=Load(1,config.bump_pixel_path,pin_version)&&Load(2,config.parallax_pixel_path,pin_version);
    if(valid&&version==2&&(config.flags&Terrain))valid=bump_vertex&&parallax_vertex&&Load(3,bump_vertex,pin_version)&&Load(4,parallax_vertex,pin_version);
    if(!valid)return false;
    current.config=config;current.stats=Stats{};current.stats.config_version=version;current.stats.enabled_flags=config.flags;current.stats.initialized_flags=config.flags;
    if(version==4){current.stats.selected_variant=selected;for(unsigned i=0;i<3;++i)current.stats.variant_sha256[i]=current.loaded_hashes[i];}
    current.device=nullptr;current.thread=0;current.program_variant=pin_version;current.unrestored=false;current.transaction=nullptr;current.sample_counts.fill(0);pending.committed=true;
    EventReport();return true;
}
bool LifecycleReady(){
    if(!current.stats.active_transactions&&!current.unrestored&&!current.transaction)return true;
    ++current.stats.context_rejections;current.stats.faulted=1;EventReport();return false;
}
bool VertexProgram(IDirect3DDevice9* device,unsigned slot,IDirect3DVertexShader9*& shader){
    if(shader)return true;const HRESULT hr=device->CreateVertexShader(reinterpret_cast<const DWORD*>(current.code[slot].data()),&shader);
    if(FAILED(hr)||!shader){if(shader){auto* failed=shader;shader=nullptr;failed->Release();}CountPrograms();++current.stats.shader_failures;current.stats.faulted=1;EventReport();return false;}
    ++current.stats.shader_creations;++current.stats.program_creations[slot];CountPrograms();return true;
}
bool PixelProgram(IDirect3DDevice9* device,unsigned slot,IDirect3DPixelShader9*& shader){
    if(shader)return true;const HRESULT hr=device->CreatePixelShader(reinterpret_cast<const DWORD*>(current.code[slot].data()),&shader);
    if(FAILED(hr)||!shader){if(shader){auto* failed=shader;shader=nullptr;failed->Release();}CountPrograms();++current.stats.shader_failures;current.stats.faulted=1;EventReport();return false;}
    ++current.stats.shader_creations;++current.stats.program_creations[slot];if(current.stats.config_version==4&&slot<3)++current.stats.variant_creations[slot];CountPrograms();return true;
}
}

bool RiverPolicy(const float fog[4],const float reflection[4],float fog_out[4],float reflection_out[4],float fog_scale,float reflection_scale) noexcept{
    Guard guard;if(!Vector(fog)||!Vector(reflection)||!fog_out||!reflection_out||!Between(fog_scale,.85f,1)||!Between(reflection_scale,1,1.30f))return false;
    for(unsigned i=0;i<4;++i)if(!Between(fog[i],0,1)||!Between(reflection[i],0,1))return false;
    for(unsigned i=0;i<4;++i){fog_out[i]=std::clamp(fog[i]*fog_scale,0.0f,1.0f);reflection_out[i]=std::clamp(reflection[i]*reflection_scale,0.0f,1.0f);}return true;
}
bool TerrainPolicy(const float original[4],float output[4],bool parallax,float detail_scale,float relief_scale) noexcept{
    Guard guard;if(!Vector(original)||!output||!Between(original[1],0,1)||!Between(detail_scale,1,1.40f)||!Between(relief_scale,1,1.20f)
        ||(parallax&&!Between(original[2],0,.1f)))return false;
    std::memcpy(output,original,4*sizeof(float));output[1]=std::clamp(original[1]*detail_scale,0.0f,1.0f);
    if(parallax)output[2]=std::clamp(original[2]*relief_scale,0.0f,.1f);return true;
}
bool Initialize(const Config* config) noexcept{
    Guard guard;try{Lock lock(mutex);if(!config||config->size!=sizeof(Config)||config->version!=1)return false;
        const Config copy=*config;return InitializeCommon(copy,1);
    }catch(...){Exception();return false;}
}
bool InitializeV2(const MaterialShaderConfigV2::Config* config) noexcept{
    Guard guard;try{Lock lock(mutex);if(current.stats.initialized_flags||current.stats.active_transactions||current.unrestored||current.transaction||!config||config->size!=MaterialShaderConfigV2::Bytes||config->version!=2)return false;
        const MaterialShaderConfigV2::Config copy=*config;if(!MaterialShaderConfigV2::Validate(copy))return false;
        PrepareProgramPins(2);unsigned variant=2;
        if(!V2Paths(copy)){
            PrepareProgramPins(3);
            if((copy.flags&Terrain)&&!(copy.flags&River)&&copy.terrain_detail_scale==1.0f&&copy.terrain_relief_scale==1.0f&&V2Paths(copy,3))variant=3;
            else{PrepareProgramPins(4);if(copy.flags!=Leaf||!V2Paths(copy,4))return false;variant=4;}
        }
        Config common{};std::memcpy(&common,&copy,sizeof(common));common.size=sizeof(common);common.version=1;
        return InitializeCommon(common,2,copy.bump_vertex_path,copy.parallax_vertex_path,variant);
    }catch(...){Exception();return false;}
}
bool InitializeV3(const MaterialShaderConfigV3::Config* config) noexcept{
    Guard guard;try{Lock lock(mutex);
        if(current.stats.initialized_flags||current.stats.active_transactions||current.unrestored||current.transaction
            ||!config||config->size!=MaterialShaderConfigV3::Bytes||config->version!=3)return false;
        const MaterialShaderConfigV3::Config copy=*config;if(!MaterialShaderConfigV3::Validate(copy))return false;
        PrepareProgramPins(5);if(!V3Paths(copy))return false;
        Config common{};std::memcpy(&common,&copy,sizeof(common));common.size=sizeof(common);common.version=1;
        return InitializeCommon(common,3,nullptr,nullptr,5,copy.leaf_pixel_path);
    }catch(...){Exception();return false;}
}
bool InitializeV4(const MaterialShaderConfigV4::Config* config) noexcept{
    Guard guard;try{Lock lock(mutex);
        if(current.stats.initialized_flags||current.stats.active_transactions||current.unrestored||current.transaction||!config
            ||config->size!=MaterialShaderConfigV4::Bytes||config->version!=4)return false;
        const MaterialShaderConfigV4::Config copy=*config;if(!MaterialShaderConfigV4::Validate(copy))return false;
        PrepareProgramPins(6);if(!V4Paths(copy))return false;
        Config common{};common.flags=Leaf;common.render_width=copy.render_width;common.render_height=copy.render_height;wcscpy_s(common.report_path,copy.report_path);
        return InitializeCommon(common,4,nullptr,nullptr,6,nullptr,copy.program_paths,copy.selected_variant);
    }catch(...){Exception();return false;}
}
bool SetVariant(DWORD index) noexcept{
    Guard guard;try{Lock lock(mutex);
        if(current.stats.config_version!=4||!current.stats.initialized_flags||index>2||current.stats.faulted||current.stats.active_transactions||current.unrestored||current.transaction
            ||(current.thread&&current.thread!=GetCurrentThreadId()))return false;
        if(index==current.stats.selected_variant)return true;
        ClearFrame();current.stats.selected_variant=index;++current.stats.enable_changes;current.stats.last_change_frame=current.stats.frames;
        current.stats.sample_count=0;current.stats.samples.fill(Sample{});current.sample_counts.fill(0);EventReport();return true;
    }catch(...){Exception();return false;}
}
bool SetEnabled(DWORD flags) noexcept{
    Guard guard;try{Lock lock(mutex);
        if(!current.stats.initialized_flags || (flags&~current.stats.initialized_flags)
            || current.stats.active_transactions || current.unrestored || current.transaction
            || (flags&&current.stats.faulted)
            || (current.thread&&current.thread!=GetCurrentThreadId()))return false;
        if(flags==current.stats.enabled_flags)return true;
        if(current.stats.config_version==4)ClearFrame();else Clear();current.stats.enabled_flags=flags;++current.stats.enable_changes;current.stats.last_change_frame=current.stats.frames;
        current.stats.sample_count=0;current.stats.samples.fill(Sample{});current.sample_counts.fill(0);
        try{Report();}catch(...){++current.stats.report_failures;}return true;
    }catch(...){Exception();return false;}
}
void SetDiagnostics(bool periodic) noexcept{
    Guard guard;try{Lock lock(mutex);if(!current.stats.initialized_flags)return;
        const DWORD enabled=periodic?1u:0u;if(enabled==current.stats.periodic_reporting)return;
        current.stats.periodic_reporting=enabled;EventReport();
    }catch(...){Exception();}
}
void BeginDraw(IDirect3DDevice9* device,DrawTransaction* output) noexcept{
    Guard guard;try{Lock lock(mutex);if(!output)return;
        if(current.transaction==output){++current.stats.context_rejections;current.stats.faulted=1;current.unrestored=true;output->unsafe_state=true;EventReport();return;}
        *output=DrawTransaction{};
        if(current.transaction||current.stats.active_transactions||current.unrestored){++current.stats.context_rejections;current.stats.faulted=1;output->unsafe_state=true;EventReport();return;}
        if(!device||!current.stats.enabled_flags||current.stats.faulted)return;
        if((current.device&&current.device!=device)||(current.thread&&current.thread!=GetCurrentThreadId())){++current.stats.context_rejections;return;}
        ++current.stats.draws;
        Ref<IDirect3DVertexShader9> vertex;Ref<IDirect3DPixelShader9> pixel;
        if(FAILED(device->GetVertexShader(&vertex.value))||FAILED(device->GetPixelShader(&pixel.value))){++current.stats.query_failures;return;}
        const auto original_vertex=vertex.value;const auto original_pixel=pixel.value;
        const DWORD vertex_mask=Classify(vertex.value,1),pixel_mask=Classify(pixel.value,2),mask=vertex_mask&pixel_mask;if(!mask)return;
        unsigned role=0;while(role<roles.size()&&!(mask&(1u<<role)))++role;
        if(role==roles.size()||!(current.stats.enabled_flags&(role==0?River:role==3?Leaf:Terrain)))return;
        ++current.stats.matched;if(!Context(device)){++current.stats.context_rejections;return;}
        DWORD blend=0;if(FAILED(device->GetRenderState(D3DRS_ALPHABLENDENABLE,&blend))||blend!=(role==1||role==2?1u:0u)){++current.stats.context_rejections;return;}
        DrawTransaction draw{};draw.device=device;draw.original_vertex=original_vertex;draw.original_pixel=original_pixel;draw.role=role+1;
        if(role==3&&current.stats.config_version==4){if(current.stats.selected_variant>2){++current.stats.input_rejections;return;}draw.selected_variant=current.stats.selected_variant;}
        if(role==0){draw.constant_count=2;draw.constants[0].stage=1;draw.constants[0].index=68;draw.constants[1].stage=1;draw.constants[1].index=69;
            if(!ReadConstant(device,draw.constants[0])||!ReadConstant(device,draw.constants[1])
                ||!RiverPolicy(draw.constants[0].original.data(),draw.constants[1].original.data(),draw.constants[0].applied.data(),draw.constants[1].applied.data(),current.config.river_density_scale,current.config.river_reflection_scale)){++current.stats.input_rejections;return;}}
        if(role==1||role==2){draw.constant_count=1;draw.constants[0].stage=2;draw.constants[0].index=33;
            if(!ReadConstant(device,draw.constants[0])||!TerrainPolicy(draw.constants[0].original.data(),draw.constants[0].applied.data(),role==2,current.config.terrain_detail_scale,current.config.terrain_relief_scale)){++current.stats.input_rejections;return;}}
        if(current.program_variant==3&&(role==1||role==2)&&!MacroInputs(device,role==2)){++current.stats.input_rejections;return;}
        // No unrelated/invalid draw can claim the device or render thread.
        current.device=device;current.thread=GetCurrentThreadId();
        Remember(vertex.value,1,vertex_mask);Remember(pixel.value,2,pixel_mask);
        current.active_vertex=vertex.release();current.active_pixel=pixel.release();current.stats.active_original_refs=2;
        PendingOriginals originals;Mutation mutation{draw,output};bool valid=true;
        if(role==3){
            if(current.stats.config_version==4){for(unsigned i=0;i<3;++i)if(!PixelProgram(device,i,current.leaf_palette[i]))return;
                draw.touched_pixel=1;valid=SUCCEEDED(device->SetPixelShader(current.leaf_palette[draw.selected_variant]));}
            else if(current.stats.config_version==3){if(!PixelProgram(device,0,current.leaf_pixel))return;
                draw.touched_pixel=1;valid=SUCCEEDED(device->SetPixelShader(current.leaf_pixel));}
            else{if(!VertexProgram(device,0,current.leaf))return;
                draw.touched_vertex=1;valid=SUCCEEDED(device->SetVertexShader(current.leaf));}}
        if(role==1||role==2){auto& pixel_shader=role==1?current.bump:current.parallax;const unsigned pixel_slot=role==1?1:2;
            auto& vertex_shader=role==1?current.bump_vertex:current.parallax_vertex;const unsigned vertex_slot=role==1?3:4;
            const bool paired=current.stats.config_version==2;
            // Prepare both objects before mutating either shader stage.
            if(paired&&!VertexProgram(device,vertex_slot,vertex_shader))return;
            if(!PixelProgram(device,pixel_slot,pixel_shader))return;
            if(paired){draw.touched_vertex=1;valid=SUCCEEDED(device->SetVertexShader(vertex_shader));}
            if(valid){draw.touched_pixel=1;valid=SUCCEEDED(device->SetPixelShader(pixel_shader));}}
        for(DWORD i=0;valid&&i<draw.constant_count;++i){draw.touched_constants=i+1;valid=SUCCEEDED(SetConstant(device,draw.constants[i],false));}
        if(!valid){++current.stats.set_failures;return;}
        draw.active=true;*output=draw;current.transaction=output;++current.stats.applied[role];if(draw.selected_variant<3)++current.stats.variant_applications[draw.selected_variant];++current.stats.active_transactions;mutation.committed=true;originals.committed=true;
    }catch(...){Exception();}
}
void EndDraw(DrawTransaction* draw,HRESULT result) noexcept{
    Guard guard;try{Lock lock(mutex);if(!draw)return;
        if(current.transaction&&current.transaction!=draw){++current.stats.context_rejections;current.stats.faulted=1;draw->unsafe_state=true;EventReport();return;}
        if(current.transaction!=draw||(!draw->active&&!draw->unsafe_state))return;const bool applied=draw->active;bool restored=Restore(*draw);draw->unsafe_state=!restored;
        if(applied&&restored)++current.stats.restored_draws;if(applied&&FAILED(result))++current.stats.failed_draws;
        if(applied&&draw->selected_variant<3){const auto i=draw->selected_variant;if(restored)++current.stats.variant_restorations[i];
            if(SUCCEEDED(result))++current.stats.variant_successful_draws[i];else ++current.stats.variant_failed_draws[i];}
        if(applied&&current.stats.active_transactions)--current.stats.active_transactions;
        if(applied&&current.stats.sample_count<current.stats.samples.size()&&current.sample_counts[draw->role-1]<8){auto& sample=current.stats.samples[current.stats.sample_count++];sample.role=draw->role;sample.epoch=current.stats.enable_changes;sample.constant_count=draw->constant_count;sample.constants=draw->constants;sample.restored=restored?1u:0u;sample.shader_changed=draw->touched_vertex||draw->touched_pixel;sample.vertex_changed=draw->touched_vertex;sample.pixel_changed=draw->touched_pixel;++current.sample_counts[draw->role-1];}
        draw->active=false;
        if(FAILED(result))EventReport();
    }catch(...){if(draw){Lock lock(mutex);if(draw->active&&current.stats.active_transactions)--current.stats.active_transactions;draw->unsafe_state=true;draw->active=false;current.unrestored=true;}Exception();}
}
bool RetryRestore(DrawTransaction* draw) noexcept{Guard guard;try{Lock lock(mutex);if(!draw||current.transaction!=draw||!draw->device)return false;draw->unsafe_state=!Restore(*draw);if(draw->active&&current.stats.active_transactions)--current.stats.active_transactions;draw->active=false;EventReport();return !draw->unsafe_state;}catch(...){if(draw){Lock lock(mutex);if(draw->active&&current.stats.active_transactions)--current.stats.active_transactions;draw->unsafe_state=true;draw->active=false;current.unrestored=true;}Exception();return false;}}
void EndFrame(IDirect3DDevice9* device) noexcept{Guard guard;try{Lock lock(mutex);if(current.device&&current.device!=device)return;if(!LifecycleReady())return;ClearFrame();++current.stats.frames;if(current.stats.periodic_reporting&&current.stats.frames%120==0)Report();}catch(...){Exception();}}
void Reset(IDirect3DDevice9* device) noexcept{Guard guard;try{Lock lock(mutex);if(current.device&&current.device!=device)return;if(!LifecycleReady())return;Clear();current.device=nullptr;++current.stats.resets;}catch(...){Exception();}}
void Flush() noexcept{Guard guard;try{Lock lock(mutex);Report();}catch(...){Exception();}}
void Shutdown() noexcept{Guard guard;try{Lock lock(mutex);if(!LifecycleReady())return;Clear();CloseFiles();if(current.algorithm){BCryptCloseAlgorithmProvider(current.algorithm,0);current.algorithm=nullptr;}current.stats.enabled_flags=0;current.stats.initialized_flags=0;current.stats.faulted=0;current.config.flags=0;current.device=nullptr;current.thread=0;Report();}catch(...){Exception();}}
bool ReadStats(Stats* output) noexcept{Guard guard;try{Lock lock(mutex);if(!output||output->size!=sizeof(Stats)||output->version!=2)return false;*output=current.stats;return true;}catch(...){return false;}}
#ifdef S7_MATERIAL_EFFECTS_FIXTURE
bool FixtureCompatibility(unsigned role,const unsigned char vertex[32],const unsigned char pixel[32],DWORD vertex_bytes,DWORD pixel_bytes,const unsigned char replacement[32]) noexcept{
    Guard guard;try{Lock lock(mutex);if(current.stats.initialized_flags||role>=roles.size()||!vertex||!pixel)return false;std::memcpy(roles[role].vertex.hash.data(),vertex,32);std::memcpy(roles[role].pixel.hash.data(),pixel,32);roles[role].vertex.bytes=vertex_bytes;roles[role].pixel.bytes=pixel_bytes;if(replacement){std::memcpy(roles[role].replacement.data(),replacement,32);if(role){const unsigned slot=role==3?0:role;std::memcpy(programs[0][slot].hash.data(),replacement,32);programs[0][slot].bytes=0;fixture_program_override[0][slot]=true;}}fixture_override[role]=true;return true;}catch(...){return false;}}
bool FixtureProgramCompatibility(unsigned version,unsigned slot,const unsigned char replacement[32],DWORD bytes) noexcept{
    Guard guard;try{Lock lock(mutex);if(current.stats.initialized_flags||version<1||version>6||slot>=5||!programs[version-1][slot].stage||!replacement||bytes<12||bytes>MaxCode||bytes%4)return false;
        std::memcpy(programs[version-1][slot].hash.data(),replacement,32);programs[version-1][slot].bytes=bytes;fixture_program_override[version-1][slot]=true;return true;
    }catch(...){return false;}}
#endif
}
