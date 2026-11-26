#include "ambient_occlusion_effects.h"
#include <bcrypt.h>
#include <float.h>
#include <intrin.h>

namespace AmbientOcclusionEffects {
namespace {
struct Guard {
    alignas(16) unsigned char fp[512];DWORD error=GetLastError();DWORD padding[3]{};
    Guard(){_fxsave(fp);unsigned ignored=0;_controlfp_s(&ignored,_MCW_EM|_RC_NEAR,_MCW_EM|_MCW_RC);}
    ~Guard(){_fxrstor(fp);SetLastError(error);}
};
SRWLOCK mutex=SRWLOCK_INIT;
struct Lock {Lock(){AcquireSRWLockExclusive(&mutex);}~Lock(){ReleaseSRWLockExclusive(&mutex);}};
template<class T> struct Ref {T* value=nullptr;~Ref(){if(value)value->Release();}};
struct Cached {IUnknown* object=nullptr;DWORD stage=0;};
struct State {
    Stats stats{};DWORD width=0,height=0,thread=0;IDirect3DDevice9* device=nullptr;
    DrawTransaction* transaction=nullptr; // opaque token; only same-callback APIs dereference caller
    std::array<float,4> original{};bool backup_valid=false;
    BCRYPT_ALG_HANDLE algorithm=nullptr;
    std::array<unsigned char,32> vertex{},pixel{};
    std::array<Cached,32> cache{};size_t count=0;
    std::array<unsigned char,5104> scratch{};
};
State current;
#ifdef S7_AMBIENT_OCCLUSION_FIXTURE
bool fixture=false;std::array<unsigned char,32> fixture_vertex{},fixture_pixel{};
#endif
void Hex(const char* text,std::array<unsigned char,32>& value){
    const auto d=[](char c){return c<='9'?c-'0':c-'a'+10;};
    for(size_t i=0;i<32;++i)value[i]=static_cast<unsigned char>((d(text[2*i])<<4)|d(text[2*i+1]));
}
bool Hash(const void* data,ULONG bytes,std::array<unsigned char,32>& output){
    BCRYPT_HASH_HANDLE hash=nullptr;
    bool valid=current.algorithm&&BCryptCreateHash(current.algorithm,&hash,nullptr,0,nullptr,0,0)>=0;
    if(valid)valid=BCryptHashData(hash,const_cast<PUCHAR>(static_cast<const unsigned char*>(data)),bytes,0)>=0;
    if(valid)valid=BCryptFinishHash(hash,output.data(),32,0)>=0;
    if(hash)BCryptDestroyHash(hash);return valid;
}
void Fault(DWORD reason){current.stats.faulted=1;current.stats.enabled=0;current.stats.last_failure=reason;if(current.backup_valid)current.stats.unsafe_state=1;}
bool Quiescent(){return !current.transaction&&!current.backup_valid&&!current.stats.active_transactions&&!current.stats.unsafe_state;}
void ClearCache(){
    for(size_t i=0;i<current.count;++i){auto* object=current.cache[i].object;current.cache[i]={};if(object)object->Release();}
    current.count=0;current.stats.cached_refs=0;
}
void ClearProof(){
    current.stats.current_applications=current.stats.current_restorations=0;
    current.stats.completed_frame=current.stats.completed_applications=current.stats.completed_restorations=0;
    current.stats.last_applied_frame=current.stats.last_restored_frame=0;
}
template<class Shader> bool Classify(Shader* shader,DWORD stage){
    if(!shader)return false;
    for(size_t i=0;i<current.count;++i)if(current.cache[i].object==static_cast<IUnknown*>(shader)&&current.cache[i].stage==stage)return true;
    UINT size=0;const UINT expected=stage==1?452u:5104u;
    if(FAILED(shader->GetFunction(nullptr,&size))){++current.stats.query_failures;return false;}
    if(size!=expected)return false;
    std::array<unsigned char,32> hash{};
    if(FAILED(shader->GetFunction(current.scratch.data(),&size))||size!=expected||!Hash(current.scratch.data(),size,hash)){
        ++current.stats.query_failures;return false;
    }
    if(hash!=(stage==1?current.vertex:current.pixel))return false;
    return true;
}
template<class Shader> void Remember(Shader* shader,DWORD stage){
    for(size_t i=0;i<current.count;++i)if(current.cache[i].object==static_cast<IUnknown*>(shader)&&current.cache[i].stage==stage)return;
    // Only a fully eligible draw retains positive identities. Capacity never
    // prevents a fresh hash match and local query refs remain alive meanwhile.
    if(current.count<current.cache.size()){
        shader->AddRef();current.cache[current.count++]={shader,stage};current.stats.cached_refs=static_cast<DWORD>(current.count);
    }else ++current.stats.cache_bypasses;
}
bool Context(IDirect3DDevice9* device,AmbientOcclusionPolicy::Input& input){
    Ref<IDirect3DSurface9> target;D3DSURFACE_DESC desc{};D3DVIEWPORT9 viewport{};
    if(FAILED(device->GetRenderTarget(0,&target.value))||!target.value||FAILED(target.value->GetDesc(&desc))
        ||desc.MultiSampleType!=D3DMULTISAMPLE_NONE||FAILED(device->GetViewport(&viewport))||viewport.MinZ!=0||viewport.MaxZ!=1)return false;
    input.render_width=current.width;input.render_height=current.height;
    input.target_width=desc.Width;input.target_height=desc.Height;input.target_format=desc.Format;
    input.viewport_x=viewport.X;input.viewport_y=viewport.Y;input.viewport_width=viewport.Width;input.viewport_height=viewport.Height;
    return true;
}
bool Map(IDirect3DDevice9* device,AmbientOcclusionPolicy::Input& input){
    Ref<IDirect3DBaseTexture9> texture;D3DSURFACE_DESC desc{};
    if(FAILED(device->GetTexture(0,&texture.value))||!texture.value||texture.value->GetType()!=D3DRTYPE_TEXTURE)return false;
    auto* map=static_cast<IDirect3DTexture9*>(texture.value);
    if(FAILED(map->GetLevelDesc(0,&desc)))return false;
    input.map_type=D3DRTYPE_TEXTURE;input.map_format=desc.Format;input.map_width=desc.Width;input.map_height=desc.Height;input.map_levels=map->GetLevelCount();
    return true;
}
bool States(IDirect3DDevice9* device,AmbientOcclusionPolicy::Input& input){
    struct Entry {D3DRENDERSTATETYPE key;std::uint32_t* output;};
    Entry entries[]={{D3DRS_ALPHABLENDENABLE,&input.alpha_blend},{D3DRS_ALPHATESTENABLE,&input.alpha_test},
        {D3DRS_SRGBWRITEENABLE,&input.srgb_write},{D3DRS_ZENABLE,&input.depth_enable},{D3DRS_ZWRITEENABLE,&input.depth_write},
        {D3DRS_STENCILENABLE,&input.stencil_enable},{D3DRS_STENCILFUNC,&input.stencil_function},{D3DRS_STENCILREF,&input.stencil_reference},
        {D3DRS_STENCILFAIL,&input.stencil_fail},{D3DRS_STENCILZFAIL,&input.stencil_zfail},{D3DRS_STENCILPASS,&input.stencil_pass},
        {D3DRS_COLORWRITEENABLE,&input.color_write}};
    for(auto& entry:entries){DWORD value=0;if(FAILED(device->GetRenderState(entry.key,&value)))return false;*entry.output=value;}
    // Captured state is read, not changed. Reject masked/two-sided stencil or
    // scissoring; they were absent from the observed eligible pass.
    DWORD mask=0,write=0,two=0,scissor=0,cull=0;
    return SUCCEEDED(device->GetRenderState(D3DRS_STENCILMASK,&mask))&&mask==0xffffffffu
        &&SUCCEEDED(device->GetRenderState(D3DRS_STENCILWRITEMASK,&write))&&write==0xffffffffu
        &&SUCCEEDED(device->GetRenderState(D3DRS_TWOSIDEDSTENCILMODE,&two))&&!two
        &&SUCCEEDED(device->GetRenderState(D3DRS_SCISSORTESTENABLE,&scissor))&&!scissor
        &&SUCCEEDED(device->GetRenderState(D3DRS_CULLMODE,&cull))&&cull==D3DCULL_NONE;
}
bool Uniforms(IDirect3DDevice9* device,AmbientOcclusionPolicy::Input& input){
    return SUCCEEDED(device->GetPixelShaderConstantF(81,input.strength.data(),1));
}
bool SamplerState(IDirect3DDevice9* device){
    struct Entry {D3DSAMPLERSTATETYPE key;DWORD* output;};
    Entry entries[]={{D3DSAMP_MINFILTER,&current.stats.sampler_min},{D3DSAMP_MAGFILTER,&current.stats.sampler_mag},
        {D3DSAMP_MIPFILTER,&current.stats.sampler_mip},{D3DSAMP_ADDRESSU,&current.stats.sampler_address_u},{D3DSAMP_ADDRESSV,&current.stats.sampler_address_v}};
    current.stats.sampler_available=0;
    for(unsigned i=0;i<5;++i){*entries[i].output=0;if(SUCCEEDED(device->GetSamplerState(0,entries[i].key,entries[i].output)))current.stats.sampler_available|=1u<<i;}
    return current.stats.sampler_available==31;
}
void ClosePending(){
    current.transaction=nullptr;current.backup_valid=false;current.stats.active_transactions=0;current.stats.unsafe_state=0;
    current.original={};
}
bool RestorePending(){
    bool restored=false;
    try{
        if(!current.device||!current.backup_valid)return false;
        const HRESULT set=current.device->SetPixelShaderConstantF(81,current.original.data(),1);
        std::array<float,4> read{};const HRESULT get=current.device->GetPixelShaderConstantF(81,read.data(),1);
        restored=SUCCEEDED(set)&&SUCCEEDED(get)&&!std::memcmp(read.data(),current.original.data(),16);
    }catch(...){++current.stats.exceptions;}
    if(!restored){current.stats.unsafe_state=1;++current.stats.restore_failures;Fault(7);return false;}
    ++current.stats.restorations;++current.stats.current_restorations;current.stats.last_restored_frame=current.stats.frames+1;
    ClosePending();return true;
}
bool Restore(DrawTransaction* transaction){
    const bool restored=RestorePending();transaction->unsafe_state=!restored;
    if(restored)transaction->active=false;return restored;
}
void OnException(DrawTransaction* transaction) noexcept{
    try{Lock lock;++current.stats.exceptions;Fault(8);
        if(transaction&&current.transaction==transaction&&current.backup_valid)Restore(transaction);
    }catch(...){current.stats.unsafe_state=1;if(transaction)transaction->unsafe_state=true;}
}
}
bool Initialize(DWORD width,DWORD height,bool authorized) noexcept{
    Guard guard;try{Lock lock;
        if(current.stats.initialized||!Quiescent()||!authorized||width<800||height<600||width>3840||height>2160)return false;
        BCRYPT_ALG_HANDLE algorithm=nullptr;
        if(BCryptOpenAlgorithmProvider(&algorithm,BCRYPT_SHA256_ALGORITHM,nullptr,0)<0)return false;
        current.stats=Stats{};current.width=width;current.height=height;current.thread=0;current.device=nullptr;current.algorithm=algorithm;
        Hex("f7ddf52720df92c869594da9c7826f275834f9a44ed2694226c42133763a0d11",current.vertex);
        Hex("aaaed0141362683d3c82f27557e1e63813e385ad43b21bb6cea2ee517562163c",current.pixel);
#ifdef S7_AMBIENT_OCCLUSION_FIXTURE
        if(fixture){current.vertex=fixture_vertex;current.pixel=fixture_pixel;}
#endif
        current.stats.initialized=current.stats.authorized=1;return true;
    }catch(...){OnException(nullptr);return false;}
}
bool Configure(const Config* config) noexcept{
    Guard guard;try{Lock lock;
        if(!current.stats.initialized||!current.stats.authorized||!config||config->size!=sizeof(Config)||(config->version!=1&&config->version!=2)||config->enabled>1
            ||!Quiescent()||(current.thread&&current.thread!=GetCurrentThreadId())){++current.stats.rejections;return false;}
        ClearCache();ClearProof();current.device=nullptr;current.thread=GetCurrentThreadId();
        current.stats.profile_version=config->version;current.stats.requested=current.stats.enabled=config->enabled;current.stats.faulted=current.stats.last_failure=0;
        ++current.stats.epoch;return true;
    }catch(...){OnException(nullptr);return false;}
}
void BeginDraw(IDirect3DDevice9* device,DrawTransaction* transaction) noexcept{
    Guard guard;if(!transaction)return;
    try{Lock lock;
        if(!Quiescent()){
            ++current.stats.rejections;
            if(transaction!=current.transaction)*transaction={};
            transaction->unsafe_state=true;Fault(10);return;
        }
        *transaction={};
        if(!current.stats.initialized||!current.stats.enabled||current.stats.faulted||!device||current.thread!=GetCurrentThreadId()
            ||(current.device&&current.device!=device))return;
        ++current.stats.draws;
        Ref<IDirect3DVertexShader9> vertex;Ref<IDirect3DPixelShader9> pixel;
        if(FAILED(device->GetVertexShader(&vertex.value))||FAILED(device->GetPixelShader(&pixel.value))){++current.stats.query_failures;return;}
        if(!Classify(vertex.value,1)||!Classify(pixel.value,2))return;
        ++current.stats.matched;
        AmbientOcclusionPolicy::Input input;
        input.enabled=input.observe_authorized=input.explicit_lab_request=input.nonzero_geometry=true;
        input.profile_version=current.stats.profile_version;
        // Classify is the authority. Policy still pins the production identities;
        // fixture identities exercise COM paths without weakening production.
        Hex("f7ddf52720df92c869594da9c7826f275834f9a44ed2694226c42133763a0d11",input.vertex_sha);
        Hex("aaaed0141362683d3c82f27557e1e63813e385ad43b21bb6cea2ee517562163c",input.pixel_sha);
        input.vertex_bytes=452;input.pixel_bytes=5104;
        if(!Context(device,input)||!Map(device,input)||!States(device,input)||!Uniforms(device,input)){
            ++current.stats.context_rejections;return;
        }
        input.observation_mask=31;const auto plan=AmbientOcclusionPolicy::Create(input);
        current.stats.last_policy_decision=static_cast<DWORD>(plan.decision);
        if(plan.decision!=AmbientOcclusionPolicy::Decision::Apply){++current.stats.policy_rejections;return;}
        if(!SamplerState(device)){++current.stats.query_failures;return;}
        Remember(vertex.value,1);Remember(pixel.value,2);
        std::memcpy(current.stats.last_original_bits.data(),plan.original.data(),16);
        std::memcpy(current.stats.last_applied_bits.data(),plan.applied.data(),16);
        current.device=device;current.transaction=transaction;current.backup_valid=true;
        std::memcpy(current.original.data(),plan.original.data(),16);current.stats.active_transactions=1;
        transaction->device=device;transaction->owner_thread=current.thread;transaction->active=true;
        // Publish backup BEFORE the setter: failed HRESULT/exception may mutate.
        const HRESULT set=device->SetPixelShaderConstantF(81,plan.applied.data(),1);
        std::array<float,4> read{};const HRESULT get=device->GetPixelShaderConstantF(81,read.data(),1);
        if(FAILED(set)||FAILED(get)||std::memcmp(read.data(),plan.applied.data(),16)){
            ++current.stats.set_failures;Fault(6);Restore(transaction);return;
        }
        ++current.stats.applications;++current.stats.current_applications;current.stats.last_applied_frame=current.stats.frames+1;
    }catch(...){OnException(transaction);}
}
void EndDraw(DrawTransaction* transaction,HRESULT original_result) noexcept{
    Guard guard;if(!transaction)return;
    try{Lock lock;
        if(current.transaction!=transaction)return;
        if(current.thread!=GetCurrentThreadId()||transaction->device!=current.device){transaction->unsafe_state=true;current.stats.unsafe_state=1;Fault(11);return;}
        if(SUCCEEDED(original_result))++current.stats.successful_draws;else ++current.stats.failed_draws;
        Restore(transaction);
    }catch(...){OnException(transaction);}
}
bool RetryRestore(DrawTransaction* transaction) noexcept{
    Guard guard;try{Lock lock;
        if(!transaction||current.transaction!=transaction||!current.backup_valid||!current.stats.unsafe_state
            ||current.thread!=GetCurrentThreadId()||transaction->device!=current.device)return false;
        ++current.stats.recovery_attempts;const bool restored=Restore(transaction);if(restored)++current.stats.recoveries;return restored;
    }catch(...){OnException(transaction);return false;}
}
bool RetryPendingRestore(IDirect3DDevice9* device) noexcept{
    Guard guard;try{Lock lock;
        if(!device||device!=current.device||current.thread!=GetCurrentThreadId()||!current.backup_valid||!current.stats.unsafe_state)return false;
        ++current.stats.recovery_attempts;const bool restored=RestorePending();if(restored)++current.stats.recoveries;return restored;
    }catch(...){OnException(nullptr);return false;}
}
void EndFrame(IDirect3DDevice9* device) noexcept{
    Guard guard;try{Lock lock;
        if(!current.stats.initialized||current.thread!=GetCurrentThreadId()||(current.device&&current.device!=device))return;
        ++current.stats.frames;current.stats.completed_frame=current.stats.frames;
        current.stats.completed_applications=current.stats.current_applications;current.stats.completed_restorations=current.stats.current_restorations;
        current.stats.current_applications=current.stats.current_restorations=0;
        if(Quiescent())ClearCache();else Fault(12);
    }catch(...){OnException(nullptr);}
}
bool Reset(IDirect3DDevice9* device) noexcept{
    Guard guard;try{Lock lock;
        if(!Quiescent()){Fault(13);return false;}
        if(current.device&&current.device!=device)return true;
        ClearCache();current.device=nullptr;return true; // ordinary Release: keep copied proof + owner
    }catch(...){OnException(nullptr);return false;}
}
bool InvalidateFrame() noexcept{
    Guard guard;try{Lock lock;if(!Quiescent()){Fault(14);return false;}
        ClearCache();ClearProof();current.device=nullptr;current.thread=GetCurrentThreadId();return true;
    }catch(...){OnException(nullptr);return false;}
}
bool Shutdown() noexcept{
    Guard guard;try{Lock lock;if(!Quiescent()){Fault(15);return false;}
        ClearCache();if(current.algorithm)BCryptCloseAlgorithmProvider(current.algorithm,0);current.algorithm=nullptr;
        current.device=nullptr;current.thread=0;current.stats.initialized=current.stats.authorized=current.stats.enabled=current.stats.requested=0;return true;
    }catch(...){OnException(nullptr);return false;}
}
bool ReadStats(Stats* output) noexcept{
    Guard guard;try{Lock lock;if(!output||output->size!=sizeof(Stats)||output->version!=2)return false;*output=current.stats;return true;}
    catch(...){return false;}
}
#ifdef S7_AMBIENT_OCCLUSION_FIXTURE
bool FixtureIdentities(const unsigned char vertex[32],const unsigned char pixel[32]) noexcept{
    Guard guard;Lock lock;if(!vertex||!pixel||current.stats.initialized||!Quiescent())return false;
    std::memcpy(fixture_vertex.data(),vertex,32);std::memcpy(fixture_pixel.data(),pixel,32);fixture=true;return true;
}
#endif
}
