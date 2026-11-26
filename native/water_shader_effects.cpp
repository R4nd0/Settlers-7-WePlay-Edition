#include "water_shader_effects.h"
#include "water_shader_palette_contract.h"
#include <bcrypt.h>
#include <algorithm>
#include <cstring>
#include <cwchar>
#include <float.h>
#include <intrin.h>
#include <string>
#include <vector>

namespace WaterShaderEffects {
namespace {
constexpr size_t MaxCache=128,VertexBytes=2592,PixelBytes=3740,ProgramBytes=3840;
constexpr char VertexText[]="f190d66ee1ae43637341a76e5b644cc63bf3c2a5b5c94ea9286764e13f1c16c0";
constexpr char PixelText[]="dd65ba0b9d79c7c4d9ad9904404388230556b194eedb8cbdfbd4fa99f44eee5b";
constexpr char ProgramText[]="8a27791a9907f73d4f42c847bc74a0a25b57da48b81f8e9cb566eca59bfcf0d1";
constexpr wchar_t Recipe[]=L"river-depth-fresnel-v1-candidate";
constexpr char BalancedText[]="a8a591b346911622bfc9899fa28cbb022a42152df41ed822ef3cfff7c254e2f4";
constexpr wchar_t BalancedRecipe[]=L"river-depth-fresnel-v2-balanced";
struct Guard {
    alignas(16) unsigned char fp[512];DWORD error=GetLastError();DWORD padding[3]{};
    Guard(){_fxsave(fp);unsigned ignored=0;_controlfp_s(&ignored,_MCW_EM|_RC_NEAR,_MCW_EM|_MCW_RC);}
    ~Guard(){_fxrstor(fp);SetLastError(error);}
};
SRWLOCK mutex=SRWLOCK_INIT;
struct Lock {Lock(){AcquireSRWLockExclusive(&mutex);}~Lock(){ReleaseSRWLockExclusive(&mutex);}};
template<class T> struct Ref {
    T* value=nullptr;
    ~Ref(){if(value)value->Release();}
    T* take(){T* result=value;value=nullptr;return result;}
};
struct File {
    HANDLE value=INVALID_HANDLE_VALUE;
    ~File(){if(value!=INVALID_HANDLE_VALUE)CloseHandle(value);}
    HANDLE take(){HANDLE result=value;value=INVALID_HANDLE_VALUE;return result;}
};
struct Algorithm {
    BCRYPT_ALG_HANDLE value=nullptr;
    ~Algorithm(){if(value)BCryptCloseAlgorithmProvider(value,0);}
    BCRYPT_ALG_HANDLE take(){auto result=value;value=nullptr;return result;}
};
struct Cached {IUnknown* object=nullptr;DWORD stage=0;};
struct State {
    Stats stats{};Config config{};
    IDirect3DDevice9* device=nullptr;DWORD thread=0;
    IDirect3DPixelShader9* program=nullptr;
    ConfigPalette palette_config{};
    std::array<IDirect3DPixelShader9*,3> palette_programs{};
    std::array<HANDLE,3> palette_files{INVALID_HANDLE_VALUE,INVALID_HANDLE_VALUE,INVALID_HANDLE_VALUE};
    alignas(4) std::array<std::array<unsigned char,ProgramBytes>,3> palette_code{};
    DWORD current_drawn_variant=MAXDWORD;
    std::uint64_t current_successful=0,current_failed=0;
    IDirect3DPixelShader9* original_pixel=nullptr; // owns the pending reference independently of caller scope
    DrawTransaction* transaction=nullptr; // opaque identity token, never asynchronously dereferenced
    BCRYPT_ALG_HANDLE algorithm=nullptr;
    HANDLE directory=INVALID_HANDLE_VALUE,file=INVALID_HANDLE_VALUE;
    std::wstring root,final_root;
    std::array<unsigned char,32> vertex_hash{},pixel_hash{},program_hash{};
    std::array<Cached,MaxCache> cache{};size_t count=0;
    alignas(4) std::array<unsigned char,ProgramBytes> code{};
    std::array<unsigned char,PixelBytes> scratch{};
};
State current;
#ifdef S7_WATER_SHADER_FIXTURE
bool fixture_identities=false;
std::array<std::array<unsigned char,32>,3> fixture_palette{};
std::array<bool,3> fixture_palette_set{};
std::array<unsigned char,32> fixture_vertex{},fixture_pixel{},fixture_program{};
#endif
void Hex(const char* text,std::array<unsigned char,32>& result){
    auto digit=[](char c){return c<='9'?c-'0':c-'a'+10;};
    for(size_t i=0;i<32;++i)result[i]=static_cast<unsigned char>((digit(text[i*2])<<4)|digit(text[i*2+1]));
}
bool Hash(const void* data,size_t bytes,std::array<unsigned char,32>& result){
    BCRYPT_HASH_HANDLE hash=nullptr;
    bool valid=current.algorithm && bytes<=MAXDWORD && BCryptCreateHash(current.algorithm,&hash,nullptr,0,nullptr,0,0)>=0;
    if(valid)valid=BCryptHashData(hash,const_cast<PUCHAR>(static_cast<const unsigned char*>(data)),static_cast<ULONG>(bytes),0)>=0;
    if(valid)valid=BCryptFinishHash(hash,result.data(),32,0)>=0;
    if(hash)BCryptDestroyHash(hash);return valid;
}
void Fault(DWORD reason){current.stats.faulted=1;current.stats.enabled=0;current.stats.last_failure=reason;}
void ClearFrame(){
    for(size_t i=0;i<current.count;++i){auto* object=current.cache[i].object;current.cache[i]={};if(object)object->Release();}
    current.count=0;current.stats.cached_refs=0;
}
void ClearPrograms(){
    ClearFrame();auto* program=current.program;current.program=nullptr;current.stats.retained_programs=0;
    if(program)program->Release();
    for(auto*& value:current.palette_programs){auto* held=value;value=nullptr;if(held)held->Release();}
}
void ClearProof(){
    current.stats.current_applications=current.stats.current_restorations=0;
    current.stats.completed_frame=current.stats.completed_applications=current.stats.completed_restorations=0;
    current.stats.last_applied_frame=current.stats.last_restored_frame=0;
    current.current_drawn_variant=current.stats.completed_drawn_variant=MAXDWORD;
    current.current_successful=current.current_failed=0;
    current.stats.completed_successful_draws=current.stats.completed_failed_draws=0;
}
bool Quiescent(){return !current.transaction&&!current.original_pixel&&!current.stats.unsafe_state&&!current.stats.active_transactions;}
bool Dimensions(DWORD width,DWORD height){return width>=800&&height>=600&&width<=3840&&height<=2160;}
std::wstring Extended(const std::wstring& path){return L"\\\\?\\"+path;}
bool Canonical(const wchar_t* path,std::wstring& result){
    const size_t length=path?wcsnlen_s(path,512):0;
    if(length<3||length>=512||!((path[0]>=L'A'&&path[0]<=L'Z')||(path[0]>=L'a'&&path[0]<=L'z'))
        ||path[1]!=L':'||path[2]!=L'\\')return false;
    for(size_t i=3;i<length;++i)if(path[i]==L':'||path[i]==L'/'||path[i]<32)return false;
    wchar_t full[512]{};const DWORD count=GetFullPathNameW(path,512,full,nullptr);
    if(!count||count>=512||_wcsicmp(path,full))return false;
    result=full;return true;
}
bool NoReparse(const std::wstring& path){
    for(size_t i=3;i<=path.size();++i)if(i==path.size()||path[i]==L'\\'){
        const DWORD attributes=GetFileAttributesW(Extended(path.substr(0,i)).c_str());
        if(attributes==INVALID_FILE_ATTRIBUTES||(attributes&FILE_ATTRIBUTE_REPARSE_POINT))return false;
    }
    return true;
}
bool FinalPath(HANDLE file,std::wstring& result){
    wchar_t path[1024]{};const DWORD length=GetFinalPathNameByHandleW(file,path,1024,FILE_NAME_NORMALIZED|VOLUME_NAME_DOS);
    if(!length||length>=1024)return false;result.assign(path,length);return true;
}
std::wstring ProgramName(const std::array<unsigned char,32>& selected){
    std::wstring hash;for(size_t i=0;i<32;++i){constexpr wchar_t digits[]=L"0123456789abcdef";
        hash+=digits[selected[i]>>4];hash+=digits[selected[i]&15];}
    return hash+L".ps.bin";
}
bool SelectProgram(const Config& config,std::array<unsigned char,32>& selected,const wchar_t*& recipe,DWORD& variant){
    std::array<unsigned char,32> a{},b{};Hex(ProgramText,a);Hex(BalancedText,b);
#ifdef S7_WATER_SHADER_FIXTURE
    if(fixture_identities&&!std::memcmp(config.pixel_sha256,fixture_program.data(),32)){selected=fixture_program;recipe=Recipe;variant=1;return true;}
#endif
    if(!std::memcmp(config.pixel_sha256,a.data(),32)){selected=a;recipe=Recipe;variant=1;return true;}
    if(!std::memcmp(config.pixel_sha256,b.data(),32)){selected=b;recipe=BalancedRecipe;variant=2;return true;}
    return false;
}
bool LoadPinned(const wchar_t* input_path,const std::array<unsigned char,32>& selected,const wchar_t* recipe,
                std::array<unsigned char,ProgramBytes>& code,HANDLE& retained,const std::array<unsigned char,32>* path_hash=nullptr){
    std::wstring path;if(!Canonical(input_path,path)||!NoReparse(path))return false;
    const auto ending=std::wstring(L"\\MaterialShaders\\")+recipe+L"\\"+ProgramName(path_hash?*path_hash:selected);
    if(_wcsicmp(path.c_str(),(current.root+ending).c_str())
        &&_wcsicmp(path.c_str(),(current.root+L"\\LocalData"+ending).c_str()))return false;
    File file{CreateFileW(Extended(path).c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,
                          FILE_ATTRIBUTE_NORMAL|FILE_FLAG_OPEN_REPARSE_POINT,nullptr)};
    if(file.value==INVALID_HANDLE_VALUE)return false;
    BY_HANDLE_FILE_INFORMATION info{};LARGE_INTEGER size{};std::wstring final;
    if(!GetFileInformationByHandle(file.value,&info)||(info.dwFileAttributes&(FILE_ATTRIBUTE_REPARSE_POINT|FILE_ATTRIBUTE_DIRECTORY))
        ||!GetFileSizeEx(file.value,&size)||size.QuadPart!=ProgramBytes||!FinalPath(file.value,final))return false;
    const auto prefix=current.final_root+L"\\";
    if(final.size()<=prefix.size()||_wcsnicmp(final.c_str(),prefix.c_str(),prefix.size()))return false;
    DWORD got=0;if(!ReadFile(file.value,code.data(),static_cast<DWORD>(code.size()),&got,nullptr)||got!=code.size())return false;
    std::array<unsigned char,32> hash{};
    if(!Hash(code.data(),code.size(),hash)||hash!=selected)return false;
    DWORD version=0,last=0;std::memcpy(&version,code.data(),4);std::memcpy(&last,code.data()+code.size()-4,4);
    if(version!=0xffff0300||last!=0xffff)return false;
    retained=file.take();return true;
}
bool LoadProgram(const Config& config,std::array<unsigned char,ProgramBytes>& code,HANDLE& retained,DWORD& variant){
    std::array<unsigned char,32> selected{};const wchar_t* recipe=nullptr;
    return SelectProgram(config,selected,recipe,variant)&&LoadPinned(config.pixel_path,selected,recipe,code,retained);
}

bool Context(IDirect3DDevice9* device){
    Ref<IDirect3DSurface9> target;D3DSURFACE_DESC desc{};D3DVIEWPORT9 viewport{};DWORD blend=0,alpha=0,srgb=0;
    return SUCCEEDED(device->GetRenderTarget(0,&target.value))&&target.value&&SUCCEEDED(target.value->GetDesc(&desc))
        &&desc.Width==current.config.render_width&&desc.Height==current.config.render_height
        &&desc.Format==D3DFMT_A8R8G8B8&&desc.MultiSampleType==D3DMULTISAMPLE_NONE
        &&SUCCEEDED(device->GetViewport(&viewport))&&!viewport.X&&!viewport.Y
        &&viewport.Width==desc.Width&&viewport.Height==desc.Height&&viewport.MinZ==0&&viewport.MaxZ==1
        &&SUCCEEDED(device->GetRenderState(D3DRS_ALPHABLENDENABLE,&blend))&&!blend
        &&SUCCEEDED(device->GetRenderState(D3DRS_ALPHATESTENABLE,&alpha))&&!alpha
        &&SUCCEEDED(device->GetRenderState(D3DRS_SRGBWRITEENABLE,&srgb))&&!srgb;
}
template<class Shader> bool Classify(Shader* shader,DWORD stage){
    if(!shader)return false;
    for(size_t i=0;i<current.count;++i)if(current.cache[i].object==static_cast<IUnknown*>(shader)&&current.cache[i].stage==stage)return true;
    UINT bytes=0;const UINT expected=stage==1?static_cast<UINT>(VertexBytes):static_cast<UINT>(PixelBytes);
    if(FAILED(shader->GetFunction(nullptr,&bytes))){++current.stats.query_failures;return false;}
    if(bytes!=expected)return false; // negative identities never consume retained slots
    std::array<unsigned char,32> hash{};
    if(FAILED(shader->GetFunction(current.scratch.data(),&bytes))||bytes!=expected||!Hash(current.scratch.data(),bytes,hash)){
        ++current.stats.query_failures;return false;
    }
    if(hash!=(stage==1?current.vertex_hash:current.pixel_hash))return false;
    if(current.count<MaxCache){shader->AddRef();current.cache[current.count++]={shader,stage};current.stats.cached_refs=static_cast<DWORD>(current.count);}
    else ++current.stats.cache_overflows; // positive uncached match is still eligible
    return true;
}
void ClosePending(){
    auto* original=current.original_pixel;current.original_pixel=nullptr;
    current.transaction=nullptr;current.stats.active_transactions=0;current.stats.unsafe_state=0;
    current.stats.pending_original_refs=0;
    if(original)original->Release();
}
bool RestorePending(){
    bool valid=false;
    try{
        if(!current.device||!current.original_pixel)return false;
        const HRESULT set=current.device->SetPixelShader(current.original_pixel);
        Ref<IDirect3DPixelShader9> got;const HRESULT read=current.device->GetPixelShader(&got.value);
        valid=SUCCEEDED(set)&&SUCCEEDED(read)&&got.value==current.original_pixel;
    }catch(...){++current.stats.exceptions;}
    current.stats.unsafe_state=valid?0u:1u;
    if(!valid){++current.stats.restore_failures;Fault(7);}else ClosePending();
    return valid;
}
bool Restore(DrawTransaction& draw){
    const bool valid=RestorePending();draw.unsafe_state=!valid;
    if(valid){draw.original_pixel=nullptr;draw.active=false;draw.touched=false;}
    return valid;
}
void RecoverException(DrawTransaction* draw) noexcept{
    try{Lock lock;++current.stats.exceptions;Fault(8);
        if(draw&&current.transaction==draw&&draw->touched)Restore(*draw);
    }catch(...){current.stats.unsafe_state=1;if(draw)draw->unsafe_state=true;}
}
}

bool Initialize(const wchar_t* trusted_report,DWORD width,DWORD height) noexcept{
    Guard guard;try{Lock lock;if(current.stats.initialized||!Quiescent()||!Dimensions(width,height))return false;
        std::wstring report;if(!Canonical(trusted_report,report))return false;
        constexpr wchar_t suffix[]=L".private.json";
        if(report.size()<sizeof(suffix)/sizeof(wchar_t)-1||_wcsicmp(report.c_str()+report.size()-(sizeof(suffix)/sizeof(wchar_t)-1),suffix))return false;
        const auto split=report.find_last_of(L'\\');if(split==std::wstring::npos)return false;
        const auto root=report.substr(0,split);if(!NoReparse(root))return false;
        File directory{CreateFileW(Extended(root).c_str(),FILE_READ_ATTRIBUTES,FILE_SHARE_READ|FILE_SHARE_WRITE,nullptr,OPEN_EXISTING,
                                  FILE_FLAG_BACKUP_SEMANTICS|FILE_FLAG_OPEN_REPARSE_POINT,nullptr)};
        std::wstring final;if(directory.value==INVALID_HANDLE_VALUE||!FinalPath(directory.value,final))return false;
        Algorithm algorithm;if(BCryptOpenAlgorithmProvider(&algorithm.value,BCRYPT_SHA256_ALGORITHM,nullptr,0)<0)return false;
        current.root=root;current.final_root=final;
        current.config.render_width=width;current.config.render_height=height;
        Hex(VertexText,current.vertex_hash);Hex(PixelText,current.pixel_hash);Hex(ProgramText,current.program_hash);
#ifdef S7_WATER_SHADER_FIXTURE
        if(fixture_identities){current.vertex_hash=fixture_vertex;current.pixel_hash=fixture_pixel;current.program_hash=fixture_program;}
#endif
        current.directory=directory.take();current.algorithm=algorithm.take();current.stats=Stats{};
        current.stats.initialized=1;return true;
    }catch(...){RecoverException(nullptr);return false;}
}

bool Configure(const Config* config) noexcept{
    Guard guard;try{Lock lock;
        if(!current.stats.initialized||!config||config->size!=sizeof(Config)||config->version!=1||config->enabled>1||current.stats.config_version==2
            ||config->render_width!=current.config.render_width||config->render_height!=current.config.render_height
            ||!Quiescent()||(current.thread&&current.thread!=GetCurrentThreadId())){++current.stats.config_rejections;return false;}
        if(!config->enabled){
            const bool empty_path=!config->pixel_path[0];
            bool empty_hash=true;for(unsigned char byte:config->pixel_sha256)if(byte)empty_hash=false;
            if(!empty_path||!empty_hash){
                std::array<unsigned char,ProgramBytes> check{};HANDLE file=INVALID_HANDLE_VALUE;DWORD variant=0;
                if(!LoadProgram(*config,check,file,variant)){++current.stats.config_rejections;return false;}
                CloseHandle(file);
            }
            ClearPrograms();current.stats.requested=current.stats.enabled=0;current.stats.faulted=0;current.stats.last_failure=0;
            ++current.stats.configuration_epoch;ClearProof();return true;
        }
        std::array<unsigned char,ProgramBytes> code{};HANDLE retained=INVALID_HANDLE_VALUE;DWORD variant=0;
        if(!LoadProgram(*config,code,retained,variant)){++current.stats.config_rejections;return false;}
        File lease{retained};ClearPrograms();
        if(current.file!=INVALID_HANDLE_VALUE)CloseHandle(current.file);
        current.file=lease.take();current.code=code;current.config=*config;current.thread=GetCurrentThreadId();
        current.stats.configured=1;current.stats.variant=variant;current.stats.requested=current.stats.enabled=1;current.stats.faulted=0;current.stats.last_failure=0;
        ++current.stats.configuration_epoch;ClearProof();return true;
    }catch(...){RecoverException(nullptr);return false;}
}

bool ConfigurePalette(const ConfigPalette* config) noexcept{
    Guard guard;try{Lock lock;
        if(!current.stats.initialized||!config||!WaterShaderPaletteContract::Validate(*config)
            ||config->render_width!=current.config.render_width||config->render_height!=current.config.render_height
            ||!Quiescent()||(current.thread&&current.thread!=GetCurrentThreadId())
            ||(current.stats.configured&&current.stats.config_version!=2)){
            ++current.stats.config_rejections;return false;}
        if(current.stats.configured){
            if(!WaterShaderPaletteContract::SamePinnedConfig(*config,current.palette_config)){++current.stats.config_rejections;return false;}
        }else{
            std::array<std::array<unsigned char,ProgramBytes>,3> code{};std::array<File,3> leases{};
            std::array<std::array<unsigned char,32>,3> hashes{};
            for(unsigned i=0;i<3;++i){Hex(WaterShaderPaletteContract::Digests[i],hashes[i]);const auto canonical_hash=hashes[i];
#ifdef S7_WATER_SHADER_FIXTURE
                if(fixture_palette_set[i])hashes[i]=fixture_palette[i];
#endif
                if(!LoadPinned(config->program_paths[i],hashes[i],WaterShaderPaletteContract::Recipes[i],code[i],leases[i].value,&canonical_hash)){
                    ++current.stats.config_rejections;return false;}}
            current.palette_code=code;for(unsigned i=0;i<3;++i)current.palette_files[i]=leases[i].take();
            current.stats.variant_sha256=hashes;current.stats.config_version=2;current.stats.configured=1;
        }
        // No shader/file replacement for subsequent selection/OFF requests.
        ClearFrame();current.palette_config=*config;current.thread=GetCurrentThreadId();
        current.stats.selected_variant=config->selected_variant;current.stats.variant=2+config->selected_variant;
        current.stats.requested=current.stats.enabled=config->enabled;current.stats.faulted=0;current.stats.last_failure=0;
        ++current.stats.configuration_epoch;ClearProof();return true;
    }catch(...){RecoverException(nullptr);return false;}
}

void BeginDraw(IDirect3DDevice9* device,DrawTransaction* output) noexcept{
    Guard guard;try{Lock lock;if(!output)return;
        if(current.transaction==output){output->unsafe_state=true;current.stats.unsafe_state=1;Fault(9);return;}*output={};
        if(!Quiescent()){output->unsafe_state=true;++current.stats.context_rejections;return;}
        if(!device||!current.stats.enabled||current.stats.faulted)return;
        ++current.stats.draws;
        if((current.device&&current.device!=device)||(current.thread&&current.thread!=GetCurrentThreadId())){++current.stats.context_rejections;return;}
        Ref<IDirect3DVertexShader9> vertex;Ref<IDirect3DPixelShader9> pixel;
        if(FAILED(device->GetVertexShader(&vertex.value))||FAILED(device->GetPixelShader(&pixel.value))){++current.stats.query_failures;return;}
        if(!Classify(vertex.value,1)||!Classify(pixel.value,2))return;
        ++current.stats.matched;if(!Context(device)){++current.stats.context_rejections;return;}
        current.device=device;current.thread=GetCurrentThreadId();
        IDirect3DPixelShader9* selected_program=nullptr;
        if(current.stats.config_version==2){
            for(unsigned i=0;i<3;++i){if(current.palette_programs[i])continue;
                Ref<IDirect3DPixelShader9> created;const HRESULT result=device->CreatePixelShader(reinterpret_cast<const DWORD*>(current.palette_code[i].data()),&created.value);
                if(FAILED(result)||!created.value){++current.stats.shader_failures;Fault(4);return;}
                current.palette_programs[i]=created.take();++current.stats.shader_creations;++current.stats.variant_creations[i];++current.stats.retained_programs;}
            output->selected_variant=current.stats.selected_variant;
            selected_program=current.palette_programs[output->selected_variant];
        }else{
            if(!current.program){
                Ref<IDirect3DPixelShader9> created;const HRESULT result=device->CreatePixelShader(reinterpret_cast<const DWORD*>(current.code.data()),&created.value);
                if(FAILED(result)||!created.value){++current.stats.shader_failures;Fault(4);return;}
                current.program=created.take();++current.stats.shader_creations;current.stats.retained_programs=1;
            }selected_program=current.program;
        }
        current.original_pixel=pixel.take();current.stats.pending_original_refs=1;
        output->device=device;output->original_pixel=current.original_pixel;output->owner_thread=GetCurrentThreadId();output->touched=true;
        current.transaction=output;current.stats.active_transactions=1;
        HRESULT result=device->SetPixelShader(selected_program);
        Ref<IDirect3DPixelShader9> got;HRESULT read=device->GetPixelShader(&got.value);
        if(FAILED(result)||FAILED(read)||got.value!=selected_program){++current.stats.set_failures;Fault(5);Restore(*output);return;}
        output->active=true;++current.stats.applications;
        if(output->selected_variant<3)++current.stats.variant_applications[output->selected_variant];
    }catch(...){RecoverException(output);}
}

void EndDraw(DrawTransaction* draw,HRESULT result) noexcept{
    Guard guard;try{Lock lock;if(!draw||!draw->touched)return;
        if(current.transaction!=draw||draw->owner_thread!=GetCurrentThreadId()){++current.stats.context_rejections;return;}
        const bool applied=draw->active;const auto selected=draw->selected_variant;const bool restored=Restore(*draw);
        if(applied){if(SUCCEEDED(result)){++current.stats.successful_draws;++current.stats.current_applications;current.stats.last_applied_frame=current.stats.frames+1;}
            else ++current.stats.failed_draws;}
        if(applied){if(SUCCEEDED(result))++current.current_successful;else ++current.current_failed;
            if(selected<3){if(SUCCEEDED(result))++current.stats.variant_successful_draws[selected];else ++current.stats.variant_failed_draws[selected];}}
        if(applied&&restored&&selected<3){++current.stats.variant_restorations[selected];if(SUCCEEDED(result))current.current_drawn_variant=selected;}
        if(applied&&restored){++current.stats.restorations;if(SUCCEEDED(result)){++current.stats.current_restorations;current.stats.last_restored_frame=current.stats.frames+1;}}
    }catch(...){RecoverException(draw);}
}

bool RetryRestore(DrawTransaction* draw) noexcept{
    Guard guard;try{Lock lock;if(!draw)return false;if(!draw->touched&&!draw->unsafe_state)return true;
        if(current.transaction!=draw||draw->owner_thread!=GetCurrentThreadId())return false;
        ++current.stats.recovery_attempts;const bool recovered=Restore(*draw);if(recovered)++current.stats.recoveries;return recovered;
    }catch(...){RecoverException(draw);return false;}
}

bool RetryPendingRestore(IDirect3DDevice9* device) noexcept{
    Guard guard;try{Lock lock;
        if(!current.stats.unsafe_state||!current.original_pixel||device!=current.device||GetCurrentThreadId()!=current.thread)return false;
        ++current.stats.recovery_attempts;const bool recovered=RestorePending();if(recovered)++current.stats.recoveries;return recovered;
    }catch(...){RecoverException(nullptr);return false;}
}

void EndFrame(IDirect3DDevice9* device) noexcept{
    Guard guard;try{Lock lock;if(!current.stats.initialized)return;
        if(!Quiescent()||(current.device&&current.device!=device)||(current.thread&&current.thread!=GetCurrentThreadId())){Fault(10);return;}
        ClearFrame();++current.stats.frames;current.stats.completed_frame=current.stats.frames;
        current.stats.completed_applications=current.stats.current_applications;current.stats.completed_restorations=current.stats.current_restorations;
        current.stats.current_applications=current.stats.current_restorations=0;
        current.stats.completed_drawn_variant=current.current_drawn_variant;
        current.stats.completed_successful_draws=current.current_successful;current.stats.completed_failed_draws=current.current_failed;
        current.current_drawn_variant=MAXDWORD;current.current_successful=current.current_failed=0;
    }catch(...){RecoverException(nullptr);}
}

bool Reset(IDirect3DDevice9* device) noexcept{
    Guard guard;try{Lock lock;if(!current.stats.initialized)return true;
        if(!Quiescent()||(current.device&&current.device!=device)){Fault(11);return false;}
        ClearPrograms();current.device=nullptr;++current.stats.resets;return true;
    }catch(...){RecoverException(nullptr);return false;}
}
void InvalidateFrame() noexcept{Guard guard;try{Lock lock;if(!current.stats.initialized)return;
    if(!Quiescent()){Fault(12);return;}ClearProof();current.thread=GetCurrentThreadId();
}catch(...){RecoverException(nullptr);}}
bool Shutdown() noexcept{
    Guard guard;try{Lock lock;if(!Quiescent())return false;ClearPrograms();
        if(current.file!=INVALID_HANDLE_VALUE)CloseHandle(current.file);
        for(auto& file:current.palette_files){if(file!=INVALID_HANDLE_VALUE)CloseHandle(file);file=INVALID_HANDLE_VALUE;}
        if(current.directory!=INVALID_HANDLE_VALUE)CloseHandle(current.directory);
        if(current.algorithm)BCryptCloseAlgorithmProvider(current.algorithm,0);
        auto history=current.stats;history.initialized=history.configured=history.requested=history.enabled=0;
        history.active_transactions=history.unsafe_state=history.cached_refs=history.retained_programs=history.pending_original_refs=0;
        current=State{};current.stats=history;return true;
    }catch(...){RecoverException(nullptr);return false;}
}
bool ReadStats(Stats* output) noexcept{
    Guard guard;try{Lock lock;if(!output||output->size!=sizeof(Stats)||output->version!=2)return false;*output=current.stats;return true;}
    catch(...){return false;}
}
#ifdef S7_WATER_SHADER_FIXTURE
bool FixtureIdentities(const unsigned char vertex[32],const unsigned char pixel[32],const unsigned char replacement[32]) noexcept{
    Guard guard;Lock lock;if(current.stats.initialized||!vertex||!pixel||!replacement)return false;
    std::memcpy(fixture_vertex.data(),vertex,32);std::memcpy(fixture_pixel.data(),pixel,32);std::memcpy(fixture_program.data(),replacement,32);
    fixture_identities=true;return true;
}
bool FixturePaletteIdentity(DWORD index,const unsigned char replacement[32]) noexcept{
    Guard guard;Lock lock;if(current.stats.initialized||index>2||!replacement)return false;
    std::memcpy(fixture_palette[index].data(),replacement,32);fixture_palette_set[index]=true;return true;
}

#endif
}
