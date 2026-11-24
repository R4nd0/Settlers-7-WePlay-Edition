#include "material_shader_effects.h"
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
struct Identity {DWORD bytes;const char* text;std::array<unsigned char,32> hash{};};
struct Role {Identity vertex,pixel;const char* replacement_text;std::array<unsigned char,32> replacement{};};
std::array<Role,4> roles{{
    {{2592,"f190d66ee1ae43637341a76e5b644cc63bf3c2a5b5c94ea9286764e13f1c16c0",{}},{3740,"dd65ba0b9d79c7c4d9ad9904404388230556b194eedb8cbdfbd4fa99f44eee5b",{}},nullptr,{}},
    {{2004,"6daa00fadd5f62f1fff27c3221154af9b83ed36225496ab981e9b624bf73a0cb",{}},{1636,"811ea82dae81585cd9f795efd2c10918cd78cbf53d89fe62be0475b6bacf5f18",{}},"c2ae22a6739e071774edd071870e6dc8fd058b5bf6eec6490bc1a4603fea2fa3",{}},
    {{2004,"41abe0979ae889b7f4e8cc51eea3fea5e7355649e1c2aca5fafb1e0e89a20daa",{}},{3396,"e776a4441563c4a3fe073811d694246c7245262244b77c614fddfe4f43144191",{}},"d0d082bbb9b8d2de1388b790165bcebc5fda571d88895db3e8b36004bf2e52fa",{}},
    {{3832,"15701e004a296b3137122207b4557c71700c20aa23eda1b01482c45223ece0df",{}},{1328,"2e633d1cd30a2db140fc338938864d909141f6cb9fd702e40ab6b89b141d90be",{}},"2797260e11f28f67cecacd2e79f0df1064bb3ed07ffdc81ab129db2963a1cbbe",{}}
}};
struct Cached {IUnknown* object=nullptr;DWORD stage=0,mask=0;};
struct State {
    Config config{};Stats stats{};IDirect3DDevice9* device=nullptr;DWORD thread=0;
    BCRYPT_ALG_HANDLE algorithm=nullptr;
    std::array<Cached,MaxCache> cache{};size_t count=0;
    std::array<unsigned char,MaxCode> scratch{};
    std::array<std::vector<unsigned char>,3> code{};
    std::array<HANDLE,3> files{INVALID_HANDLE_VALUE,INVALID_HANDLE_VALUE,INVALID_HANDLE_VALUE};
    IDirect3DVertexShader9* leaf=nullptr;
    IDirect3DPixelShader9* bump=nullptr;IDirect3DPixelShader9* parallax=nullptr;
    bool unrestored=false;
    std::array<DWORD,4> sample_counts{};
};
State current;SRWLOCK mutex=SRWLOCK_INIT;
void EventReport() noexcept;
#ifdef S7_MATERIAL_EFFECTS_FIXTURE
std::array<bool,4> fixture_override{};
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
    current.stats.retained_programs=(current.leaf?1u:0u)+(current.bump?1u:0u)+(current.parallax?1u:0u);
    current.stats.peak_retained_programs=std::max(current.stats.peak_retained_programs,current.stats.retained_programs);
}
void Clear(){
    ClearFrame();
    auto* leaf=current.leaf;auto* bump=current.bump;auto* parallax=current.parallax;
    current.leaf=nullptr;current.bump=nullptr;current.parallax=nullptr;CountPrograms();
    if(leaf)leaf->Release();if(bump)bump->Release();if(parallax)parallax->Release();
}
void CloseFiles(){for(auto& file:current.files)if(file!=INVALID_HANDLE_VALUE){CloseHandle(file);file=INVALID_HANDLE_VALUE;}}
bool Load(unsigned index,const wchar_t* path,unsigned role){
    if(!Absolute(path))return false;
    File file{CreateFileW(path,GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL|FILE_FLAG_OPEN_REPARSE_POINT,nullptr)};
    if(file.value==INVALID_HANDLE_VALUE)return false;
    LARGE_INTEGER size{};BY_HANDLE_FILE_INFORMATION info{};bool valid=GetFileInformationByHandle(file.value,&info)&&!(info.dwFileAttributes&FILE_ATTRIBUTE_REPARSE_POINT)
        &&GetFileSizeEx(file.value,&size)&&size.QuadPart>=12&&size.QuadPart<=static_cast<LONGLONG>(MaxCode)&&!(size.QuadPart%4);
    DWORD bytes=0;
    if(valid){current.code[index].resize(static_cast<size_t>(size.QuadPart));valid=ReadFile(file.value,current.code[index].data(),static_cast<DWORD>(size.QuadPart),&bytes,nullptr)&&static_cast<LONGLONG>(bytes)==size.QuadPart;}
    std::array<unsigned char,32> hash{};if(valid)valid=Hash(current.code[index].data(),bytes,hash)&&hash==roles[role].replacement;
    DWORD version=0;if(valid)std::memcpy(&version,current.code[index].data(),4);
    if(valid)valid=version==(index==0?0xfffe0300u:0xffff0300u);
    if(valid){current.files[index]=file.value;file.value=INVALID_HANDLE_VALUE;}else current.code[index].clear();return valid;
}
bool Context(IDirect3DDevice9* device){
    IDirect3DSurface9* target=nullptr;D3DSURFACE_DESC desc{};D3DVIEWPORT9 viewport{};DWORD alpha=0,srgb=0;
    bool valid=SUCCEEDED(device->GetRenderTarget(0,&target))&&target&&SUCCEEDED(target->GetDesc(&desc));if(target)target->Release();
    valid=valid&&desc.Width==current.config.render_width&&desc.Height==current.config.render_height&&desc.Format==D3DFMT_A8R8G8B8
        &&SUCCEEDED(device->GetViewport(&viewport))&&!viewport.X&&!viewport.Y&&viewport.Width==desc.Width&&viewport.Height==desc.Height
        &&SUCCEEDED(device->GetRenderState(D3DRS_ALPHATESTENABLE,&alpha))&&!alpha
        &&SUCCEEDED(device->GetRenderState(D3DRS_SRGBWRITEENABLE,&srgb))&&!srgb;
    return valid;
}
template<class Shader> DWORD Classify(Shader* shader,DWORD stage){
    if(!shader)return 0;
    for(size_t i=0;i<current.count;++i)if(current.cache[i].object==static_cast<IUnknown*>(shader)&&current.cache[i].stage==stage){shader->Release();return current.cache[i].mask;}
    if(current.count==MaxCache){++current.stats.cache_overflows;shader->Release();return 0;}
    UINT bytes=0;std::array<unsigned char,32> hash{};DWORD mask=0;
    bool valid=SUCCEEDED(shader->GetFunction(nullptr,&bytes))&&bytes&&bytes<=MaxCode&&!(bytes%4);const UINT expected=bytes;
    if(valid)valid=SUCCEEDED(shader->GetFunction(current.scratch.data(),&bytes))&&bytes==expected&&Hash(current.scratch.data(),bytes,hash);
    if(!valid){++current.stats.query_failures;shader->Release();return 0;}
    for(size_t i=0;i<roles.size();++i){const auto& key=stage==1?roles[i].vertex:roles[i].pixel;if(key.bytes==bytes&&key.hash==hash)mask|=1u<<i;}
    current.cache[current.count++]={shader,stage,mask};current.stats.cached_refs=static_cast<DWORD>(current.count);return mask;
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
    if(draw.touched_vertex){IDirect3DVertexShader9* got=nullptr;HRESULT hr=draw.device->GetVertexShader(&got);if(FAILED(hr)||got!=draw.original_vertex)valid=false;if(got)got->Release();}
    if(draw.touched_pixel){IDirect3DPixelShader9* got=nullptr;HRESULT hr=draw.device->GetPixelShader(&got);if(FAILED(hr)||got!=draw.original_pixel)valid=false;if(got)got->Release();}
    current.unrestored=!valid;
    if(!valid){++current.stats.restore_failures;current.stats.faulted=1;EventReport();}return valid;
}
struct Mutation {
    DrawTransaction& draw;DrawTransaction* output;bool committed=false;
    ~Mutation()noexcept{
        if(committed || (!draw.touched_constants&&!draw.touched_vertex&&!draw.touched_pixel))return;
        try{draw.unsafe_state=!Restore(draw);}catch(...){draw.unsafe_state=true;current.unrestored=true;++current.stats.restore_failures;++current.stats.exceptions;current.stats.faulted=1;}
        draw.active=false;*output=draw;
        EventReport();
    }
};
void Report(){
    if(!current.config.report_path[0])return;const auto& s=current.stats;char text[2048]{};
    std::snprintf(text,sizeof(text),"{\"format\":\"s7-native-material-effects-v1\",\"enabled_flags\":%lu,\"faulted\":%lu,\"draws\":%llu,\"matched\":%llu,\"restored_draws\":%llu,\"restore_failures\":%llu,\"context_rejections\":%llu,\"input_rejections\":%llu,\"query_failures\":%llu,\"shader_creations\":%llu,\"shader_failures\":%llu,\"set_failures\":%llu,\"failed_draws\":%llu,\"cached_refs\":%lu,\"retained_programs\":%lu,\"peak_retained_programs\":%lu,\"frames\":%llu,\"resets\":%llu,\"cache_overflows\":%llu,\"applied\":[%llu,%llu,%llu,%llu],\"samples\":[",
        s.enabled_flags,s.faulted,s.draws,s.matched,s.restored_draws,s.restore_failures,s.context_rejections,s.input_rejections,s.query_failures,s.shader_creations,s.shader_failures,s.set_failures,s.failed_draws,s.cached_refs,s.retained_programs,s.peak_retained_programs,s.frames,s.resets,s.cache_overflows,s.applied[0],s.applied[1],s.applied[2],s.applied[3]);
    std::string json=text;
    for(DWORD i=0;i<s.sample_count;++i){const auto& sample=s.samples[i];if(i)json+=',';std::snprintf(text,sizeof(text),"{\"role\":%lu,\"epoch\":%llu,\"restored\":%lu,\"shader_changed\":%lu,\"constants\":[",sample.role,sample.epoch,sample.restored,sample.shader_changed);json+=text;
        for(DWORD j=0;j<sample.constant_count;++j){const auto& c=sample.constants[j];if(j)json+=',';
            std::snprintf(text,sizeof(text),"{\"stage\":%lu,\"index\":%lu,\"original\":[%.9g,%.9g,%.9g,%.9g],\"applied\":[%.9g,%.9g,%.9g,%.9g]}",c.stage,c.index,c.original[0],c.original[1],c.original[2],c.original[3],c.applied[0],c.applied[1],c.applied[2],c.applied[3]);json+=text;}
        json+="]}";}
    std::snprintf(text,sizeof(text),"],\"initialized_flags\":%lu,\"active_transactions\":%lu,\"periodic_reporting\":%lu,\"enable_changes\":%llu,\"last_change_frame\":%llu}\n",s.initialized_flags,s.active_transactions,s.periodic_reporting,s.enable_changes,s.last_change_frame);json+=text;
    const std::wstring temporary=std::wstring(current.config.report_path)+L".tmp";HANDLE file=CreateFileW(temporary.c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);
    if(file==INVALID_HANDLE_VALUE){++current.stats.report_failures;return;}DWORD count=0;bool valid=WriteFile(file,json.data(),static_cast<DWORD>(json.size()),&count,nullptr)&&count==json.size();CloseHandle(file);
    if(valid)valid=MoveFileExW(temporary.c_str(),current.config.report_path,MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)!=0;if(!valid)++current.stats.report_failures;
}
void EventReport() noexcept{try{Report();}catch(...){++current.stats.report_failures;}}
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
    Guard guard;try{Lock lock(mutex);if(current.stats.initialized_flags||!config||config->size!=sizeof(Config)||config->version!=1
        ||!config->flags||(config->flags&~AllowedFlags)||config->render_width<320||config->render_height<200
        ||config->render_width>8192||config->render_height>8192||static_cast<std::uint64_t>(config->render_width)*config->render_height>32u*1024u*1024u
        ||!PrivateReport(config->report_path)||!Between(config->river_density_scale,.85f,1)||!Between(config->river_reflection_scale,1,1.3f)
        ||!Between(config->terrain_detail_scale,1,1.4f)||!Between(config->terrain_relief_scale,1,1.2f))return false;
        for(size_t index=0;index<roles.size();++index){auto& role=roles[index];
#ifdef S7_MATERIAL_EFFECTS_FIXTURE
            if(fixture_override[index])continue;
#endif
            Hex(role.vertex.text,role.vertex.hash);Hex(role.pixel.text,role.pixel.hash);if(role.replacement_text)Hex(role.replacement_text,role.replacement);}
        if(!current.algorithm&&BCryptOpenAlgorithmProvider(&current.algorithm,BCRYPT_SHA256_ALGORITHM,nullptr,0)<0)return false;
        bool valid=true;
        if(config->flags&Leaf)valid=Load(0,config->leaf_vertex_path,3);
        if(valid&&(config->flags&Terrain))valid=Load(1,config->bump_pixel_path,1)&&Load(2,config->parallax_pixel_path,2);
        if(!valid){CloseFiles();return false;}
        current.config=*config;current.stats=Stats{};current.stats.enabled_flags=config->flags;current.stats.initialized_flags=config->flags;current.device=nullptr;current.thread=0;current.unrestored=false;current.sample_counts.fill(0);
        try{Report();}catch(...){++current.stats.report_failures;}return true;
    }catch(...){Exception();return false;}
}
bool SetEnabled(DWORD flags) noexcept{
    Guard guard;try{Lock lock(mutex);
        if(!current.stats.initialized_flags || (flags&~current.stats.initialized_flags)
            || current.stats.active_transactions || current.unrestored
            || (flags&&current.stats.faulted)
            || (current.thread&&current.thread!=GetCurrentThreadId()))return false;
        if(flags==current.stats.enabled_flags)return true;
        Clear();current.stats.enabled_flags=flags;++current.stats.enable_changes;current.stats.last_change_frame=current.stats.frames;
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
    Guard guard;try{Lock lock(mutex);if(!output)return;*output=DrawTransaction{};
        if(!device||!current.stats.enabled_flags||current.stats.faulted)return;
        if(current.stats.active_transactions||current.unrestored){++current.stats.context_rejections;return;}
        if((current.device&&current.device!=device)||(current.thread&&current.thread!=GetCurrentThreadId())){++current.stats.context_rejections;return;}
        current.device=device;current.thread=GetCurrentThreadId();++current.stats.draws;
        IDirect3DVertexShader9* vertex=nullptr;IDirect3DPixelShader9* pixel=nullptr;
        if(FAILED(device->GetVertexShader(&vertex))||FAILED(device->GetPixelShader(&pixel))){if(vertex)vertex->Release();if(pixel)pixel->Release();++current.stats.query_failures;return;}
        const auto original_vertex=vertex;const auto original_pixel=pixel;
        const DWORD mask=Classify(vertex,1)&Classify(pixel,2);if(!mask)return;
        unsigned role=0;while(role<roles.size()&&!(mask&(1u<<role)))++role;
        if(role==roles.size()||!(current.stats.enabled_flags&(role==0?River:role==3?Leaf:Terrain)))return;
        ++current.stats.matched;if(!Context(device)){++current.stats.context_rejections;return;}
        DWORD blend=0;if(FAILED(device->GetRenderState(D3DRS_ALPHABLENDENABLE,&blend))||blend!=(role==1||role==2?1u:0u)){++current.stats.context_rejections;return;}
        DrawTransaction draw{};draw.device=device;draw.original_vertex=original_vertex;draw.original_pixel=original_pixel;draw.role=role+1;
        if(role==0){draw.constant_count=2;draw.constants[0].stage=1;draw.constants[0].index=68;draw.constants[1].stage=1;draw.constants[1].index=69;
            if(!ReadConstant(device,draw.constants[0])||!ReadConstant(device,draw.constants[1])
                ||!RiverPolicy(draw.constants[0].original.data(),draw.constants[1].original.data(),draw.constants[0].applied.data(),draw.constants[1].applied.data(),current.config.river_density_scale,current.config.river_reflection_scale)){++current.stats.input_rejections;return;}}
        if(role==1||role==2){draw.constant_count=1;draw.constants[0].stage=2;draw.constants[0].index=33;
            if(!ReadConstant(device,draw.constants[0])||!TerrainPolicy(draw.constants[0].original.data(),draw.constants[0].applied.data(),role==2,current.config.terrain_detail_scale,current.config.terrain_relief_scale)){++current.stats.input_rejections;return;}}
        Mutation mutation{draw,output};bool valid=true;
        if(role==3){if(!current.leaf){HRESULT hr=device->CreateVertexShader(reinterpret_cast<const DWORD*>(current.code[0].data()),&current.leaf);if(FAILED(hr)||!current.leaf){if(current.leaf){current.leaf->Release();current.leaf=nullptr;}++current.stats.shader_failures;current.stats.faulted=1;EventReport();return;}++current.stats.shader_creations;CountPrograms();}
            draw.touched_vertex=1;valid=SUCCEEDED(device->SetVertexShader(current.leaf));}
        if(role==1||role==2){auto& shader=role==1?current.bump:current.parallax;const unsigned code=role==1?1:2;
            if(!shader){HRESULT hr=device->CreatePixelShader(reinterpret_cast<const DWORD*>(current.code[code].data()),&shader);if(FAILED(hr)||!shader){if(shader){shader->Release();shader=nullptr;}++current.stats.shader_failures;current.stats.faulted=1;EventReport();return;}++current.stats.shader_creations;CountPrograms();}
            draw.touched_pixel=1;valid=SUCCEEDED(device->SetPixelShader(shader));}
        for(DWORD i=0;valid&&i<draw.constant_count;++i){draw.touched_constants=i+1;valid=SUCCEEDED(SetConstant(device,draw.constants[i],false));}
        if(!valid){++current.stats.set_failures;return;}
        draw.active=true;*output=draw;++current.stats.applied[role];++current.stats.active_transactions;mutation.committed=true;
    }catch(...){Exception();}
}
void EndDraw(DrawTransaction* draw,HRESULT result) noexcept{
    Guard guard;try{Lock lock(mutex);if(!draw||(!draw->active&&!draw->unsafe_state))return;const bool applied=draw->active;bool restored=Restore(*draw);draw->unsafe_state=!restored;
        if(applied&&restored)++current.stats.restored_draws;if(applied&&FAILED(result))++current.stats.failed_draws;
        if(applied&&current.stats.active_transactions)--current.stats.active_transactions;
        if(applied&&current.stats.sample_count<current.stats.samples.size()&&current.sample_counts[draw->role-1]<8){auto& sample=current.stats.samples[current.stats.sample_count++];sample.role=draw->role;sample.epoch=current.stats.enable_changes;sample.constant_count=draw->constant_count;sample.constants=draw->constants;sample.restored=restored?1u:0u;sample.shader_changed=draw->touched_vertex||draw->touched_pixel;++current.sample_counts[draw->role-1];}
        draw->active=false;
        if(FAILED(result))EventReport();
    }catch(...){if(draw){Lock lock(mutex);if(draw->active&&current.stats.active_transactions)--current.stats.active_transactions;draw->unsafe_state=true;draw->active=false;current.unrestored=true;}Exception();}
}
bool RetryRestore(DrawTransaction* draw) noexcept{Guard guard;try{Lock lock(mutex);if(!draw||!draw->device)return false;draw->unsafe_state=!Restore(*draw);if(draw->active&&current.stats.active_transactions)--current.stats.active_transactions;draw->active=false;EventReport();return !draw->unsafe_state;}catch(...){if(draw){Lock lock(mutex);if(draw->active&&current.stats.active_transactions)--current.stats.active_transactions;draw->unsafe_state=true;draw->active=false;current.unrestored=true;}Exception();return false;}}
void EndFrame(IDirect3DDevice9* device) noexcept{Guard guard;try{Lock lock(mutex);if(current.device&&current.device!=device)return;ClearFrame();++current.stats.frames;if(current.stats.periodic_reporting&&current.stats.frames%120==0)Report();}catch(...){Exception();}}
void Reset(IDirect3DDevice9* device) noexcept{Guard guard;try{Lock lock(mutex);if(current.device&&current.device!=device)return;Clear();current.device=nullptr;++current.stats.resets;}catch(...){Exception();}}
void Flush() noexcept{Guard guard;try{Lock lock(mutex);Report();}catch(...){Exception();}}
void Shutdown() noexcept{Guard guard;try{Lock lock(mutex);if(current.stats.active_transactions||current.unrestored){++current.stats.context_rejections;return;}Clear();CloseFiles();if(current.algorithm){BCryptCloseAlgorithmProvider(current.algorithm,0);current.algorithm=nullptr;}current.stats.enabled_flags=0;current.stats.initialized_flags=0;current.stats.faulted=0;current.config.flags=0;current.device=nullptr;current.thread=0;Report();}catch(...){Exception();}}
bool ReadStats(Stats* output) noexcept{Guard guard;try{Lock lock(mutex);if(!output||output->size!=sizeof(Stats)||output->version!=1)return false;*output=current.stats;return true;}catch(...){return false;}}
#ifdef S7_MATERIAL_EFFECTS_FIXTURE
bool FixtureCompatibility(unsigned role,const unsigned char vertex[32],const unsigned char pixel[32],DWORD vertex_bytes,DWORD pixel_bytes,const unsigned char replacement[32]) noexcept{
    Guard guard;try{Lock lock(mutex);if(current.stats.initialized_flags||role>=roles.size()||!vertex||!pixel)return false;std::memcpy(roles[role].vertex.hash.data(),vertex,32);std::memcpy(roles[role].pixel.hash.data(),pixel,32);roles[role].vertex.bytes=vertex_bytes;roles[role].pixel.bytes=pixel_bytes;if(replacement)std::memcpy(roles[role].replacement.data(),replacement,32);fixture_override[role]=true;return true;}catch(...){return false;}}
#endif
}
