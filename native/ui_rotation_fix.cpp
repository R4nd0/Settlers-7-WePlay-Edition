#include "ui_rotation_fix.h"
#include "ui_rotation_program.h"
#include <bcrypt.h>
#include <vector>
#include <cstring>
#include <cmath>

namespace UiRotationFix {
namespace {
struct State {
    DWORD width=0,height=0;bool initialized=false;Stats stats{};
    IDirect3DDevice9* device=nullptr;DWORD thread=0;
    IDirect3DVertexShader9* generated=nullptr;IDirect3DVertexShader9* pending=nullptr;
    bool pending_applied=false;
    std::uint64_t applications=0,restorations=0,failed=0;
    std::array<D3DVERTEXELEMENT9,16> declaration{};UINT declaration_count=0;
} state;
template<class T>struct Ref {T* value=nullptr;~Ref(){if(value)value->Release();}};
bool Hash(const void* data,ULONG bytes,const char* expected) {
    BCRYPT_ALG_HANDLE algorithm=nullptr;BCRYPT_HASH_HANDLE hash=nullptr;std::array<unsigned char,32> digest{};
    if(BCryptOpenAlgorithmProvider(&algorithm,BCRYPT_SHA256_ALGORITHM,nullptr,0)<0)return false;
    bool valid=BCryptCreateHash(algorithm,&hash,nullptr,0,nullptr,0,0)>=0;
    if(valid)valid=BCryptHashData(hash,static_cast<PUCHAR>(const_cast<void*>(data)),bytes,0)>=0
        &&BCryptFinishHash(hash,digest.data(),32,0)>=0;
    if(hash)BCryptDestroyHash(hash);BCryptCloseAlgorithmProvider(algorithm,0);
    const auto digit=[](char c){return c<='9'?c-'0':c-'a'+10;};
    for(size_t i=0;valid&&i<digest.size();++i)valid=digest[i]==static_cast<unsigned char>((digit(expected[i*2])<<4)|digit(expected[i*2+1]));
    return valid;
}
bool Restore(IDirect3DDevice9* device,IDirect3DVertexShader9* original) {
    const HRESULT set=device->SetVertexShader(original);Ref<IDirect3DVertexShader9> got;
    if(FAILED(set))return false;
    if(FAILED(device->GetVertexShader(&got.value))){got.value=nullptr;return false;}
    return got.value==original;
}
bool Context(IDirect3DDevice9* device) {
    Ref<IDirect3DSurface9> target;D3DSURFACE_DESC desc{};D3DVIEWPORT9 viewport{};
    if(FAILED(device->GetRenderTarget(0,&target.value))){target.value=nullptr;return false;}
    if(!target.value||FAILED(target.value->GetDesc(&desc))
        ||desc.Width!=state.width||desc.Height!=state.height||desc.Format!=D3DFMT_A8R8G8B8
        ||desc.MultiSampleType!=D3DMULTISAMPLE_NONE||FAILED(device->GetViewport(&viewport))
        ||viewport.X||viewport.Y||viewport.Width!=state.width||viewport.Height!=state.height)return false;
    std::array<float,16> matrix{};
    if(FAILED(device->GetVertexShaderConstantF(64,matrix.data(),4)))return false;
    for(auto value:matrix)if(!std::isfinite(value))return false;
    if(std::fabs(matrix[0]-2.0f/static_cast<float>(state.width))>1e-7f
        ||std::fabs(matrix[5]+2.0f/static_cast<float>(state.height))>1e-7f)return false;
    Ref<IDirect3DVertexDeclaration9> declaration;
    if(FAILED(device->GetVertexDeclaration(&declaration.value))){declaration.value=nullptr;return false;}
    if(!declaration.value)return false;
    std::array<D3DVERTEXELEMENT9,16> elements{};UINT element_count=static_cast<UINT>(elements.size());
    if(FAILED(declaration.value->GetDeclaration(elements.data(),&element_count))||element_count>elements.size()) {
        state.declaration_count=0;return false;
    }
    state.declaration=elements;state.declaration_count=element_count;
    if(state.declaration_count!=5)return false;
    const D3DVERTEXELEMENT9 expected[]={{0,0,D3DDECLTYPE_FLOAT2,D3DDECLMETHOD_DEFAULT,D3DDECLUSAGE_POSITION,0},
        {0,8,D3DDECLTYPE_SHORT4N,D3DDECLMETHOD_DEFAULT,D3DDECLUSAGE_TEXCOORD,0},
        {0,16,D3DDECLTYPE_SHORT4N,D3DDECLMETHOD_DEFAULT,D3DDECLUSAGE_TEXCOORD,1},
        {0,24,D3DDECLTYPE_D3DCOLOR,D3DDECLMETHOD_DEFAULT,D3DDECLUSAGE_COLOR,0},D3DDECL_END()};
    if(std::memcmp(expected+4,state.declaration.data()+4,sizeof(D3DVERTEXELEMENT9)))return false;
    std::array<bool,4> used{};
    for(size_t i=0;i<4;++i) {
        bool found=false;
        for(size_t j=0;j<4;++j)if(!used[j]&&!std::memcmp(expected+i,state.declaration.data()+j,sizeof(D3DVERTEXELEMENT9))) {
            used[j]=true;found=true;break;
        }
        if(!found)return false;
    }
    Ref<IDirect3DVertexBuffer9> buffer;UINT offset=0,stride=0;D3DVERTEXBUFFER_DESC buffer_desc{};
    if(FAILED(device->GetStreamSource(0,&buffer.value,&offset,&stride))){buffer.value=nullptr;return false;}
    return buffer.value&&stride==28&&SUCCEEDED(buffer.value->GetDesc(&buffer_desc))
        &&offset<=buffer_desc.Size&&buffer_desc.Size-offset>=84;
}
bool Retry(IDirect3DDevice9* device) {
    if(!state.pending)return !state.stats.unsafe;
    if(device!=state.device||GetCurrentThreadId()!=state.thread||!Restore(device,state.pending))return false;
    state.pending->Release();state.pending=nullptr;
    if(state.pending_applied){++state.stats.restorations;++state.restorations;}
    state.pending_applied=false;state.stats.pending_refs=state.stats.unsafe=0;return true;
}
}
void Initialize(DWORD width,DWORD height) noexcept {
    state.width=width;state.height=height;state.initialized=true;
}
bool Configure(const Config& config) noexcept {
    if(!state.initialized||config.size!=sizeof(Config)||config.version!=3||config.enabled>1||state.stats.unsafe)return false;
    state.stats.requested=config.enabled;
    state.stats.enabled=config.enabled&&state.width>3276&&state.width<=3840&&state.height<=2160?1u:0u;return true;
}
bool Begin(IDirect3DDevice9* device,Transaction& transaction) noexcept {
    if(state.stats.unsafe)return false;
    if(!state.stats.enabled)return true;
    ++state.stats.draws;
    try {
        if(!device||(state.device&&state.device!=device)||(state.thread&&state.thread!=GetCurrentThreadId()))return true;
        Ref<IDirect3DVertexShader9> vertex;Ref<IDirect3DPixelShader9> pixel;UINT vertex_bytes=0,pixel_bytes=0;
        if(FAILED(device->GetVertexShader(&vertex.value))){vertex.value=nullptr;++state.stats.query_failures;return true;}
        if(!vertex.value)return true;
        if(FAILED(vertex.value->GetFunction(nullptr,&vertex_bytes))||vertex_bytes!=484)return true;
        if(FAILED(device->GetPixelShader(&pixel.value))){pixel.value=nullptr;++state.stats.query_failures;return true;}
        if(!pixel.value||FAILED(pixel.value->GetFunction(nullptr,&pixel_bytes))||pixel_bytes!=1108)return true;
        std::vector<std::uint32_t> original(121);std::array<unsigned char,1108> pixel_code{};
        if(FAILED(vertex.value->GetFunction(original.data(),&vertex_bytes))||vertex_bytes!=484
            ||FAILED(pixel.value->GetFunction(pixel_code.data(),&pixel_bytes))||pixel_bytes!=1108) {++state.stats.query_failures;return true;}
        if(!Hash(original.data(),484,"0fafd8564b812ef5d8609329052df4be741f015c3c3b51b0946420e0fe6880f3")
            ||!Hash(pixel_code.data(),1108,"69b5a31786c16faf4c06b1592c6925f745230ac27d7729db78d330fdf084aae6"))return true;
        ++state.stats.matched;
        if(!Context(device)){++state.stats.context_rejections;return true;}
        state.device=device;state.thread=GetCurrentThreadId();
        if(!state.generated) {
            std::vector<std::uint32_t> patched;
            if(!UiRotationProgram::Patch(original,patched)||!Hash(patched.data(),672,
                "ffa1684dd31f91e2c371f0263109d32bbaad5e05559600c6eca276a2ef1e7304")) {
                ++state.stats.shader_failures;return true;
            }
            IDirect3DVertexShader9* created=nullptr;
            if(FAILED(device->CreateVertexShader(reinterpret_cast<const DWORD*>(patched.data()),&created))||!created) {
                ++state.stats.shader_failures;return true;
            }
            state.generated=created;
            ++state.stats.creations;state.stats.retained_programs=1;
        }
        transaction.original=vertex.value;vertex.value=nullptr;
        const HRESULT set=device->SetVertexShader(state.generated);Ref<IDirect3DVertexShader9> got;
        const HRESULT queried=device->GetVertexShader(&got.value);
        if(FAILED(queried))got.value=nullptr;
        const bool confirmed=SUCCEEDED(queried)&&got.value==state.generated;
        if(FAILED(set)||!confirmed) {
            ++state.stats.set_failures;
            if(!Restore(device,transaction.original)) {
                state.pending=transaction.original;state.pending_applied=false;transaction.original=nullptr;state.stats.pending_refs=state.stats.unsafe=1;
                ++state.stats.restore_failures;return false;
            }
            transaction.original->Release();transaction.original=nullptr;return true;
        }
        transaction.applied=true;++state.stats.applications;++state.applications;return true;
    }catch(...) {
        ++state.stats.exceptions;
        if(transaction.original) {
            bool restored=false;try{restored=Restore(device,transaction.original);}catch(...){++state.stats.exceptions;}
            if(!restored) {
                state.pending=transaction.original;state.pending_applied=transaction.applied;transaction.original=nullptr;state.stats.pending_refs=state.stats.unsafe=1;return false;
            }
            transaction.original->Release();transaction.original=nullptr;
        }
        return true;
    }
}
bool End(IDirect3DDevice9* device,Transaction& transaction,HRESULT result) noexcept {
    if(!transaction.original)return !state.stats.unsafe;
    bool restored=false;
    try {restored=Restore(device,transaction.original);}catch(...){++state.stats.exceptions;}
    if(!restored) {
        state.pending=transaction.original;state.pending_applied=transaction.applied;transaction.original=nullptr;transaction.applied=false;
        state.stats.pending_refs=state.stats.unsafe=1;++state.stats.restore_failures;return false;
    }
    transaction.original->Release();transaction.original=nullptr;
    if(transaction.applied) {
        ++state.stats.restorations;++state.restorations;
        if(FAILED(result)){++state.stats.failed_draws;++state.failed;}
    }
    transaction.applied=false;return true;
}
bool Reset(IDirect3DDevice9* device) noexcept {
    try {
        if(!Retry(device))return false;
        if(state.generated){state.generated->Release();state.generated=nullptr;}
        state.stats.retained_programs=0;state.device=nullptr;state.thread=0;return true;
    }catch(...){++state.stats.exceptions;state.stats.unsafe=1;return false;}
}
bool Safe() noexcept{return !state.stats.unsafe;}
bool Enabled() noexcept{return state.stats.enabled!=0;}
void EndFrame(std::uint64_t frame) noexcept {
    state.stats.frame=frame;state.stats.frame_applications=state.applications;state.stats.frame_restorations=state.restorations;
    state.stats.frame_failed_draws=state.failed;state.applications=state.restorations=state.failed=0;
}
Stats Read() noexcept{return state.stats;}
std::string Json() {
    const auto& s=state.stats;
    std::string j="{\"requested\":"+std::to_string(s.requested)+",\"enabled\":"+std::to_string(s.enabled)+",\"unsafe\":"+std::to_string(s.unsafe)
        +",\"pending_refs\":"+std::to_string(s.pending_refs)+",\"retained_programs\":"+std::to_string(s.retained_programs)
        +",\"generated_vertex_sha256\":\"ffa1684dd31f91e2c371f0263109d32bbaad5e05559600c6eca276a2ef1e7304\""
        +",\"draws\":"+std::to_string(s.draws)+",\"matched\":"+std::to_string(s.matched)+",\"applications\":"+std::to_string(s.applications)
        +",\"restorations\":"+std::to_string(s.restorations)+",\"failed_draws\":"+std::to_string(s.failed_draws)
        +",\"context_rejections\":"+std::to_string(s.context_rejections)+",\"query_failures\":"+std::to_string(s.query_failures)
        +",\"shader_failures\":"+std::to_string(s.shader_failures)+",\"set_failures\":"+std::to_string(s.set_failures)
        +",\"restore_failures\":"+std::to_string(s.restore_failures)+",\"exceptions\":"+std::to_string(s.exceptions)
        +",\"creations\":"+std::to_string(s.creations)+",\"frame\":"+std::to_string(s.frame)
        +",\"frame_applications\":"+std::to_string(s.frame_applications)+",\"frame_restorations\":"+std::to_string(s.frame_restorations)
        +",\"frame_failed_draws\":"+std::to_string(s.frame_failed_draws)+",\"declaration\":[";
    const UINT count=state.declaration_count<=state.declaration.size()?state.declaration_count:0;
    for(UINT i=0;i<count;++i){const auto& d=state.declaration[i];if(i)j+=',';j+='['+std::to_string(d.Stream)+','+std::to_string(d.Offset)
        +','+std::to_string(d.Type)+','+std::to_string(d.Method)+','+std::to_string(d.Usage)+','+std::to_string(d.UsageIndex)+']';}
    return j+"]}";
}
}
