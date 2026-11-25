#include "material_comparison.h"
#include <bcrypt.h>
#include <cstring>
#include <float.h>
#include <intrin.h>

namespace MaterialComparison {
namespace {
constexpr size_t MaxCode=3740,MaxCache=128;
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
    T* release(){T* result=value;value=nullptr;return result;}
};
struct Cached {IUnknown* shader=nullptr;DWORD stage=0;bool matched=false;};
struct Internal {
    State state{};BCRYPT_ALG_HANDLE algorithm=nullptr;IDirect3DDevice9* device=nullptr;
    DWORD thread=0;Transaction* transaction=nullptr;
    std::array<Cached,MaxCache> cache{};size_t count=0;
    std::array<unsigned char,MaxCode> scratch{};
};
Internal current;
struct Identity {DWORD bytes;const char* sha;};
constexpr Identity originals[2]={{2592,"f190d66ee1ae43637341a76e5b644cc63bf3c2a5b5c94ea9286764e13f1c16c0"},
    {3740,"dd65ba0b9d79c7c4d9ad9904404388230556b194eedb8cbdfbd4fa99f44eee5b"}};
#ifdef S7_MATERIAL_COMPARISON_FIXTURE
bool fixture=false;std::array<std::array<unsigned char,32>,2> fixture_hash{};
std::array<DWORD,2> fixture_bytes{};
#endif
void Refresh(){current.state.active=current.state.initialized&&current.state.requested&&!current.state.faulted?1u:0u;}
void Fault(){current.state.faulted=1;Refresh();}
void Exception(){Lock lock;++current.state.exceptions;Fault();}
bool Busy(){return current.transaction || current.state.active_transactions || current.state.unsafe_state;}
void ClearFrameEvidence(){
    current.state.frame_applications=0;current.state.frame_restorations=0;
    current.state.completed_frame_applications=0;current.state.completed_frame_restorations=0;
    current.state.last_applied_frame=0;current.state.last_restored_frame=0;
}
void ClearRefs(){
    for(size_t i=0;i<current.count;++i){IUnknown* p=current.cache[i].shader;current.cache[i]=Cached{};if(p)p->Release();}
    current.count=0;current.state.cached_refs=0;
}
void Close(){ClearRefs();if(current.algorithm){BCryptCloseAlgorithmProvider(current.algorithm,0);current.algorithm=nullptr;}}
bool Vector(const std::array<float,4>& values){for(const float& f:values){DWORD bits=0;std::memcpy(&bits,&f,4);if((bits&0x7f800000u)==0x7f800000u)return false;}return true;}
bool Hash(const unsigned char* bytes,DWORD count,std::array<unsigned char,32>& output){
    struct HashHandle {BCRYPT_HASH_HANDLE value=nullptr;~HashHandle(){if(value)BCryptDestroyHash(value);}} hash;
    return current.algorithm&&BCryptCreateHash(current.algorithm,&hash.value,nullptr,0,nullptr,0,0)>=0
        && BCryptHashData(hash.value,const_cast<PUCHAR>(bytes),count,0)>=0
        && BCryptFinishHash(hash.value,output.data(),32,0)>=0;
}
bool IdentityMatches(DWORD stage,DWORD bytes,const std::array<unsigned char,32>& hash){
#ifdef S7_MATERIAL_COMPARISON_FIXTURE
    if(fixture)return bytes==fixture_bytes[stage-1]&&hash==fixture_hash[stage-1];
#endif
    const auto& expected=originals[stage-1];if(bytes!=expected.bytes)return false;
    const auto digit=[](char c){return c<='9'?c-'0':c-'a'+10;};
    for(size_t i=0;i<32;++i)if(hash[i]!=static_cast<unsigned char>((digit(expected.sha[i*2])<<4)|digit(expected.sha[i*2+1])))return false;
    return true;
}
DWORD ExpectedBytes(DWORD stage){
#ifdef S7_MATERIAL_COMPARISON_FIXTURE
    if(fixture)return fixture_bytes[stage-1];
#endif
    return originals[stage-1].bytes;
}
template<class T> bool Classify(Ref<T>& shader,DWORD stage){
    if(!shader.value)return false;
    for(size_t i=0;i<current.count;++i)if(current.cache[i].shader==static_cast<IUnknown*>(shader.value)&&current.cache[i].stage==stage)return current.cache[i].matched;
    UINT bytes=0;std::array<unsigned char,32> hash{};
    bool valid=SUCCEEDED(shader.value->GetFunction(nullptr,&bytes));
    if(!valid){++current.state.query_failures;return false;}
    bool matched=false;
    if(bytes==ExpectedBytes(stage)&&bytes&&bytes<=MaxCode&&!(bytes%4)){
        const UINT expected=bytes;
        valid=SUCCEEDED(shader.value->GetFunction(current.scratch.data(),&bytes))&&bytes==expected
            &&Hash(current.scratch.data(),bytes,hash);
        if(!valid){++current.state.query_failures;return false;}
        matched=IdentityMatches(stage,bytes,hash);
    }
    // Negative programs must not starve a later real River pair. In particular,
    // same-sized wrong hashes consume no retained reference or identity slot.
    if(!matched)return false;
    // A full positive cache affects reuse cost only, never matching eligibility.
    // The caller's temporary query ref remains owned by Ref and is released.
    if(current.count==MaxCache){++current.state.cache_overflows;return true;}
    current.cache[current.count++]={shader.release(),stage,true};current.state.cached_refs=static_cast<DWORD>(current.count);
    return true;
}
bool Context(IDirect3DDevice9* device){
    Ref<IDirect3DSurface9> target;D3DSURFACE_DESC desc{};D3DVIEWPORT9 viewport{};
    DWORD blend=0,alpha=0,srgb=0;
    return SUCCEEDED(device->GetRenderTarget(0,&target.value))&&target.value&&SUCCEEDED(target.value->GetDesc(&desc))
        &&desc.Width==current.state.render_width&&desc.Height==current.state.render_height&&desc.Format==D3DFMT_A8R8G8B8
        &&SUCCEEDED(device->GetViewport(&viewport))&&!viewport.X&&!viewport.Y&&viewport.Width==desc.Width&&viewport.Height==desc.Height
        &&SUCCEEDED(device->GetRenderState(D3DRS_ALPHABLENDENABLE,&blend))&&!blend
        &&SUCCEEDED(device->GetRenderState(D3DRS_ALPHATESTENABLE,&alpha))&&!alpha
        &&SUCCEEDED(device->GetRenderState(D3DRS_SRGBWRITEENABLE,&srgb))&&!srgb;
}
bool Restore(Transaction& draw){
    bool valid=SUCCEEDED(draw.device->SetPixelShaderConstantF(80,draw.original.data(),1));
    std::array<float,4> actual{};
    if(FAILED(draw.device->GetPixelShaderConstantF(80,actual.data(),1))||std::memcmp(actual.data(),draw.original.data(),16))valid=false;
    if(!valid){++current.state.restore_failures;Fault();draw.unsafe_state=true;current.state.unsafe_state=1;return false;}
    ++current.state.restorations;++current.state.frame_restorations;current.state.last_restored_frame=current.state.frames+1;
    draw.touched=false;draw.active=false;draw.unsafe_state=false;
    current.state.active_transactions=0;current.state.unsafe_state=0;current.transaction=nullptr;return true;
}
void Unsafe(Transaction& draw){draw.active=false;draw.unsafe_state=true;current.state.unsafe_state=1;++current.state.restore_failures;Fault();}
struct Mutation {
    Transaction& draw;bool committed=false;
    ~Mutation(){if(committed||!draw.touched)return;try{Restore(draw);}catch(...){++current.state.exceptions;Unsafe(draw);}}
};
}
bool Initialize(DWORD width,DWORD height,bool enabled) noexcept{
    Guard guard;try{Lock lock;
        if(Busy()||width<320||height<200||width>8192||height>8192||static_cast<std::uint64_t>(width)*height>32u*1024u*1024u)return false;
        Close();current=Internal{};current.state.initialized=1;current.state.render_width=width;current.state.render_height=height;
        current.state.requested=enabled?1u:0u;current.state.epoch=1;
        if(enabled&&BCryptOpenAlgorithmProvider(&current.algorithm,BCRYPT_SHA256_ALGORITHM,nullptr,0)<0){Fault();return false;}
        Refresh();return true;
    }catch(...){Exception();return false;}
}
bool Configure(const Config* config) noexcept{
    Guard guard;try{Lock lock;if(!config||config->size!=sizeof(Config)||config->version!=1||config->enabled>1||!current.state.initialized||Busy())return false;
        ClearRefs();current.device=nullptr;current.thread=0;current.state.pinned=0;current.state.pinned_ps80={};
        current.state.pin_committed_frame=0;
        ClearFrameEvidence();
        current.state.requested=config->enabled;current.state.faulted=0;++current.state.epoch;
        if(config->enabled&&!current.algorithm&&BCryptOpenAlgorithmProvider(&current.algorithm,BCRYPT_SHA256_ALGORITHM,nullptr,0)<0){Fault();return false;}
        Refresh();return true;
    }catch(...){Exception();return false;}
}
void BeginDraw(IDirect3DDevice9* device,Transaction* output) noexcept{
    Guard guard;try{Lock lock;if(!output)return;
        if(current.transaction==output){Fault();return;}
        *output=Transaction{};if(!device||!current.state.active)return;
        if(Busy()){++current.state.context_rejections;Fault();output->unsafe_state=true;return;}
        if((current.device&&current.device!=device)||(current.thread&&current.thread!=GetCurrentThreadId())){++current.state.context_rejections;return;}
        ++current.state.draws;
        Ref<IDirect3DVertexShader9> vertex;Ref<IDirect3DPixelShader9> pixel;
        if(FAILED(device->GetVertexShader(&vertex.value))||FAILED(device->GetPixelShader(&pixel.value))){++current.state.query_failures;return;}
        const bool vs=Classify(vertex,1),ps=Classify(pixel,2);if(!vs||!ps)return;++current.state.matched;
        if(!Context(device)){++current.state.context_rejections;return;}
        if(FAILED(device->GetPixelShaderConstantF(80,output->original.data(),1))){++current.state.query_failures;return;}
        if(!Vector(output->original)){++current.state.input_rejections;return;}
        // Unrelated shaders/contexts must not choose the comparison's render
        // thread before the first actual eligible original River transaction.
        current.device=device;current.thread=GetCurrentThreadId();
        output->pin_candidate=!current.state.pinned;
        std::memcpy(output->applied.data(),current.state.pinned?current.state.pinned_ps80.data():output->original.data(),16);
        output->device=device;output->touched=true;current.transaction=output;current.state.active_transactions=1;
        Mutation mutation{*output};
        if(FAILED(device->SetPixelShaderConstantF(80,output->applied.data(),1))){++current.state.set_failures;Fault();return;}
        std::array<float,4> actual{};
        if(FAILED(device->GetPixelShaderConstantF(80,actual.data(),1))||std::memcmp(actual.data(),output->applied.data(),16)){++current.state.set_failures;Fault();return;}
        output->active=true;++current.state.applications;++current.state.frame_applications;
        current.state.last_applied_frame=current.state.frames+1;mutation.committed=true;
    }catch(...){Exception();}
}
void EndDraw(Transaction* draw,HRESULT result) noexcept{
    Guard guard;try{Lock lock;if(!draw||current.transaction!=draw||!draw->touched)return;
        if(FAILED(result))++current.state.failed_draws;
        if(Restore(*draw)&&SUCCEEDED(result)&&draw->pin_candidate&&!current.state.faulted){
            std::memcpy(current.state.pinned_ps80.data(),draw->applied.data(),16);current.state.pinned=1;
            current.state.pin_committed_frame=current.state.frames+1;
        }
    }catch(...){if(draw){Lock lock;++current.state.exceptions;Unsafe(*draw);}else Exception();}
}
bool RetryRestore(Transaction* draw) noexcept{
    Guard guard;try{Lock lock;if(!draw||current.transaction!=draw||!draw->device||!draw->touched)return false;return Restore(*draw);
    }catch(...){if(draw){Lock lock;++current.state.exceptions;Unsafe(*draw);}else Exception();return false;}
}
void EndFrame(IDirect3DDevice9* device) noexcept{
    Guard guard;try{Lock lock;if(!current.state.initialized||(current.device&&current.device!=device))return;if(Busy()){Fault();return;}ClearRefs();
        ++current.state.frames;current.state.completed_frame_applications=current.state.frame_applications;
        current.state.completed_frame_restorations=current.state.frame_restorations;
        current.state.frame_applications=0;current.state.frame_restorations=0;
    }catch(...){Exception();}
}
void Reset(IDirect3DDevice9* device) noexcept{
    Guard guard;try{Lock lock;if(current.device&&current.device!=device)return;if(Busy()){Fault();return;}ClearRefs();current.device=nullptr;current.thread=0;
        ++current.state.resets;
    }catch(...){Exception();}
}
void InvalidateFrame() noexcept{
    Guard guard;try{Lock lock;if(Busy()){Fault();return;}ClearFrameEvidence();}catch(...){Exception();}
}
void Shutdown() noexcept{
    Guard guard;try{Lock lock;if(Busy()){Fault();return;}Close();current=Internal{};}catch(...){Exception();}
}
bool ReadState(State* output) noexcept{
    Guard guard;try{Lock lock;if(!output||output->size!=sizeof(State)||output->version!=1)return false;*output=current.state;return true;}catch(...){Exception();return false;}
}
#ifdef S7_MATERIAL_COMPARISON_FIXTURE
bool FixtureIdentity(const unsigned char vertex[32],DWORD vertex_bytes,const unsigned char pixel[32],DWORD pixel_bytes) noexcept{
    Guard guard;try{Lock lock;if(current.state.initialized||!vertex||!pixel||!vertex_bytes||!pixel_bytes||vertex_bytes>MaxCode||pixel_bytes>MaxCode||(vertex_bytes%4)||(pixel_bytes%4))return false;
        std::memcpy(fixture_hash[0].data(),vertex,32);std::memcpy(fixture_hash[1].data(),pixel,32);fixture_bytes={vertex_bytes,pixel_bytes};fixture=true;return true;
    }catch(...){Exception();return false;}
}
#endif
}
