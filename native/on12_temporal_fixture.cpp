// Numerical sequences and shader reflection only. Never creates a GPU device.
#include "on12_temporal.h"
#include "on12_temporal_pass.h"
#include <d3dcompiler.h>
#include <d3d12shader.h>
#include <wrl/client.h>
#include <cstdio>
#include <cmath>
#include <limits>
#include <cstring>

using namespace On12Graphics::Temporal;
using Microsoft::WRL::ComPtr;
namespace {
unsigned checks=0;
bool Check(bool okay,unsigned line){++checks;if(!okay)std::fprintf(stderr,"temporal check failed at %u\n",line);return okay;}
#define REQUIRE(x) do{if(!Check(!!(x),__LINE__))return 20;}while(false)
bool Near(float a,float b,float tolerance=1e-5f){return std::fabs(a-b)<=tolerance;}
Frame Input(std::uint64_t sequence,unsigned width=9,unsigned height=9){Frame f;f.enabled=true;f.sequence=sequence;f.projection_epoch=1;f.width=width;f.height=height;f.proofs=AllProofs;auto j=Jitter(sequence);f.jitter_x=j[0];f.jitter_y=j[1];return f;}
Image Solid(unsigned w,unsigned h,Color color){Image im;im.width=w;im.height=h;im.color.assign(w*h,color);im.depth.assign(w*h,10);im.motion.assign(w*h,{0,0,10,1});im.reactive.assign(w*h,0);return im;}
float Variance(const std::vector<float>& values){float mean=0;for(float v:values)mean+=v/static_cast<float>(values.size());float result=0;for(float v:values)result+=(v-mean)*(v-mean)/static_cast<float>(values.size());return result;}
}
int wmain(){
    Parameters quality;HistoryState state;
    auto first=Input(1);auto d=state.Prepare(first,quality);REQUIRE(d.dispatch&&!d.reuse&&d.reset==Reset::FirstFrame);
    REQUIRE(!state.Commit(Input(2))&&state.Commit(first));
    auto second=Input(2);d=state.Prepare(second,quality);REQUIRE(d.dispatch&&d.reuse&&d.parameters.history_valid==1);
    REQUIRE(Near(d.parameters.previous_jitter_x,first.jitter_x)&&Near(d.parameters.previous_jitter_y,first.jitter_y));REQUIRE(state.Commit(second));
    auto cut=Input(3);cut.camera_cut=true;d=state.Prepare(cut,quality);REQUIRE(d.dispatch&&!d.reuse&&d.reset==Reset::CameraCut);REQUIRE(state.Commit(cut));
    auto resized=Input(4,12,9);d=state.Prepare(resized,quality);REQUIRE(!d.reuse&&d.reset==Reset::Extent);REQUIRE(state.Commit(resized));
    auto projection=Input(5,12,9);projection.projection_epoch=2;d=state.Prepare(projection,quality);REQUIRE(!d.reuse&&d.reset==Reset::Projection);REQUIRE(state.Commit(projection));
    auto skipped=Input(7,12,9);skipped.projection_epoch=2;d=state.Prepare(skipped,quality);REQUIRE(!d.reuse&&d.reset==Reset::Sequence);REQUIRE(state.Commit(skipped));
    auto missing=Input(8,12,9);missing.proofs&=~DynamicMotionAndReactive;d=state.Prepare(missing,quality);REQUIRE(!d.dispatch&&!d.reuse&&d.reset==Reset::MissingInputs);
    auto invalid=Input(9);invalid.jitter_x+=0.1f;d=state.Prepare(invalid,quality);REQUIRE(!d.dispatch&&d.reset==Reset::InvalidFrame);
    auto seed=Input(10);d=state.Prepare(seed,quality);REQUIRE(d.dispatch&&!d.reuse);REQUIRE(state.Commit(seed));state.Abort();
    d=state.Prepare(Input(11),quality);REQUIRE(d.dispatch&&!d.reuse&&d.reset==Reset::Aborted);
    for(std::uint32_t bit=1;bit<=DynamicMotionAndReactive;bit<<=1){HistoryState isolated;auto f=Input(1);f.proofs&=~bit;REQUIRE(!isolated.Prepare(f,quality).dispatch);}
    HistoryState changed;auto c1=Input(1);REQUIRE(changed.Prepare(c1,quality).dispatch&&changed.Commit(c1));auto changed_quality=quality;changed_quality.color_srgb=1;
    d=changed.Prepare(Input(2),changed_quality);REQUIRE(!d.reuse&&d.reset==Reset::Settings);
    d=changed.Prepare(Input(2),changed_quality);REQUIRE(d.dispatch&&!d.reuse); // Unfinished reset output must not become history.
    REQUIRE(changed.Commit(Input(2)));d=changed.Prepare(Input(3),changed_quality);REQUIRE(d.reuse);
    for(std::uint64_t i=1;i<=8;++i){auto j=Jitter(i);REQUIRE(std::fabs(j[0])<=0.5f&&std::fabs(j[1])<=0.5f&&j==Jitter(i+8));}
    Parameters p;p.width=p.height=9;Image image=Solid(9,9,{0.2f,0.4f,0.6f,0.3f}),out;History history,next;
    REQUIRE(Resolve(image,history,p,out,next));history=next;p.history_valid=1;
    REQUIRE(Resolve(image,history,p,out,next));for(const auto& c:out.color)REQUIRE(Near(c.r,0.2f)&&Near(c.g,0.4f)&&Near(c.b,0.6f)&&c.a==0.3f);
    // No history can leak into flat new regions or across a depth discontinuity.
    for(auto& c:history.color)c={1,0,1,1};
    REQUIRE(Resolve(image,history,p,out,next));for(const auto& c:out.color)REQUIRE(Near(c.r,0.2f)&&Near(c.g,0.4f)&&Near(c.b,0.6f));
    Image edges=Solid(9,9,{0,0,0,1});for(unsigned y=0;y<9;++y)for(unsigned x=4;x<9;++x)edges.color[y*9+x]={1,1,1,1};
    history.color.assign(81,{0.5f,0.5f,0.5f,1});history.depth.assign(81,1);REQUIRE(Resolve(edges,history,p,out,next));
    for(unsigned y=0;y<9;++y)for(unsigned x=0;x<9;++x)REQUIRE(out.color[y*9+x].r==(x<4?0.0f:1.0f));
    // Correct expected previous-view depth permits a camera moving forward.
    Image changing=Solid(9,9,{0.5f,0.5f,0.5f,1});for(unsigned i=0;i<81;++i)changing.color[i]=(i%2)?Color{1,1,1,1}:Color{0,0,0,1};changing.color[40]={0.5f,0.5f,0.5f,0.4f};
    history.color.assign(81,{0.6f,0.6f,0.6f,1});history.depth.assign(81,12);for(auto& m:changing.motion)m.previous_depth=12;
    REQUIRE(Resolve(changing,history,p,out,next)&&out.color[40].r>0.5f&&out.color[40].a==0.4f);
    changing.reactive[40]=1;REQUIRE(Resolve(changing,history,p,out,next)&&out.color[40].r==0.5f);changing.reactive[40]=0;
    changing.motion[40].confidence=0;REQUIRE(Resolve(changing,history,p,out,next)&&out.color[40].r==0.5f);changing.motion[40].confidence=1;
    changing.motion[40].x=100;REQUIRE(Resolve(changing,history,p,out,next)&&out.color[40].r==0.5f);changing.motion[40].x=0;
    changing.motion[40].previous_depth=1;REQUIRE(Resolve(changing,history,p,out,next)&&out.color[40].r==0.5f);changing.motion[40].previous_depth=12;
    changing.motion[40].x=(std::numeric_limits<float>::quiet_NaN)();REQUIRE(Resolve(changing,history,p,out,next)&&out.color[40].r==0.5f);changing.motion[40].x=0;
    // Fractional reprojection must not mix a different surface even when the
    // nearest history-depth sample is valid. Zero-weight taps are irrelevant.
    changing.motion[40].x=0.5f;REQUIRE(Resolve(changing,history,p,out,next)&&out.color[40].r>0.5f);
    history.depth[40]=1;REQUIRE(Resolve(changing,history,p,out,next)&&out.color[40].r==0.5f);
    history.depth[40]=12;history.depth[41]=1;changing.motion[40].x=0;
    REQUIRE(Resolve(changing,history,p,out,next)&&out.color[40].r>0.5f);history.depth[41]=12;
    // A moving sharp edge must stay sharp after reprojection, without a trail.
    p.history_valid=0;Image moving=Solid(16,8,{0,0,0,1});p.width=16;p.height=8;
    for(unsigned frame=0;frame<6;++frame){const unsigned boundary=3+frame;
        for(unsigned y=0;y<8;++y)for(unsigned x=0;x<16;++x){moving.color[y*16+x]=x>=boundary?Color{1,1,1,1}:Color{0,0,0,1};moving.motion[y*16+x]={-1,0,10,1};}
        REQUIRE(Resolve(moving,history,p,out,next));for(unsigned y=0;y<8;++y){REQUIRE(out.color[y*16+boundary].r>0.99f);REQUIRE(out.color[y*16+boundary-1].r<0.01f);}history=next;p.history_valid=1;
    }
    // Subpixel coverage of a high-frequency roof/fence pattern, alternating
    // between sample phases. Compare temporal variance, not a mirrored formula.
    p.width=p.height=9;p.history_valid=0;Image pattern=Solid(9,9,{0,0,0,1});std::vector<float> raw,filtered;
    for(unsigned frame=0;frame<40;++frame){for(unsigned y=0;y<9;++y)for(unsigned x=0;x<9;++x){const float value=((x+y+frame)&1)?1.0f:0.0f;pattern.color[y*9+x]={value,value,value,1};}
        p.jitter_x=frame&1?0.25f:-0.25f;p.previous_jitter_x=frame?-(p.jitter_x):p.jitter_x;
        REQUIRE(Resolve(pattern,history,p,out,next));if(frame>=8){raw.push_back(pattern.color[40].r);filtered.push_back(out.color[40].r);}history=next;p.history_valid=1;
    }
    const float before=Variance(raw),after=Variance(filtered);REQUIRE(after<before*0.55f);
    p.jitter_x=p.previous_jitter_x=0;p.color_srgb=1;p.history_valid=0;Image gamma=Solid(9,9,{0.5f,0.25f,0.75f,0.8f});REQUIRE(Resolve(gamma,history,p,out,next));
    REQUIRE(Near(out.color[40].r,0.5f)&&Near(next.color[40].r,0.214041f,1e-5f)&&out.color[40].a==0.8f);
    auto bad=p;bad.history_max=1;REQUIRE(!Valid(bad));bad=p;bad.variance_gamma=(std::numeric_limits<float>::quiet_NaN)();REQUIRE(!Valid(bad));
    REQUIRE(!Resolve(gamma,history,p,gamma,next));
    // Compile all shader stages and reflect the exact host ABI, on CPU only.
    constexpr UINT flags=D3DCOMPILE_ENABLE_STRICTNESS|D3DCOMPILE_WARNINGS_ARE_ERRORS|D3DCOMPILE_OPTIMIZATION_LEVEL3;
    ComPtr<ID3DBlob> vs,ps,error;HRESULT hr=D3DCompileFromFile(L"on12_temporal.hlsl",nullptr,nullptr,"VS","vs_5_0",flags,0,&vs,&error);
    if(FAILED(hr)&&error)std::fprintf(stderr,"%s",static_cast<const char*>(error->GetBufferPointer()));REQUIRE(SUCCEEDED(hr));error.Reset();
    hr=D3DCompileFromFile(L"on12_temporal.hlsl",nullptr,nullptr,"PS","ps_5_0",flags,0,&ps,&error);
    if(FAILED(hr)&&error)std::fprintf(stderr,"%s",static_cast<const char*>(error->GetBufferPointer()));REQUIRE(SUCCEEDED(hr));
    ComPtr<ID3D12ShaderReflection> reflection;REQUIRE(SUCCEEDED(D3DReflect(ps->GetBufferPointer(),ps->GetBufferSize(),IID_PPV_ARGS(&reflection))));
    D3D12_SHADER_BUFFER_DESC cb{};REQUIRE(SUCCEEDED(reflection->GetConstantBufferByName("Parameters")->GetDesc(&cb))&&cb.Size==sizeof(Parameters));
    const char* names[]{"Current","History","Motion","Depth","HistoryDepth","Reactive"};
    for(UINT i=0;i<6;++i){D3D12_SHADER_INPUT_BIND_DESC binding{};REQUIRE(SUCCEEDED(reflection->GetResourceBindingDescByName(names[i],&binding))&&binding.Type==D3D_SIT_TEXTURE&&binding.BindPoint==i&&binding.BindCount==1);}
    D3D12_SHADER_DESC shader{};REQUIRE(SUCCEEDED(reflection->GetDesc(&shader))&&shader.OutputParameters==3);
    ComPtr<ID3DBlob> signature;REQUIRE(SUCCEEDED(On12Graphics::TemporalPass::SerializeSignature(&signature))&&signature->GetBufferSize()>0);
    auto write=[](const wchar_t* path,ID3DBlob* bytes){FILE* f=nullptr;if(_wfopen_s(&f,path,L"wb")||!f)return false;const bool okay=std::fwrite(bytes->GetBufferPointer(),1,bytes->GetBufferSize(),f)==bytes->GetBufferSize();std::fclose(f);return okay;};
    REQUIRE(write(L"on12_temporal_vs.bin",vs.Get())&&write(L"on12_temporal_ps.bin",ps.Get()));
    std::printf("{\"passed\":true,\"checks\":%u,\"cpu_only\":true,\"gpu_device_created\":false,\"raw_variance\":%.9f,\"resolved_variance\":%.9f,\"sharp_edge_preserved\":true,\"reactive_rejection\":true,\"shader_compiled\":true,\"abi_bytes\":64}\n",checks,before,after);
    return 0;
}
