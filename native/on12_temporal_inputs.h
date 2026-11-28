#pragma once
#include <d3d9.h>
#include <wrl/client.h>
#include <array>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <iomanip>
#include <limits>
#include <ostream>

namespace On12Graphics {
// Read-only candidates, never proof of depth/motion/jitter correctness. Both
// views matter: the engine sees native extents, the On12 device sees scene size.
struct TemporalSurfaceInfo {
    HRESULT result=E_FAIL;
    UINT width=0,height=0,format=0,usage=0,pool=0,multisample=0;
};
struct TemporalDeviceInputs {
    HRESULT matrix_result=E_FAIL,viewport_result=E_FAIL;
    std::array<float,16> candidate_view_projection{};
    std::array<float,4> candidate_eye{},candidate_camera{},candidate_screen_size{};
    D3DVIEWPORT9 viewport{};
    std::array<TemporalSurfaceInfo,4> targets{};
    TemporalSurfaceInfo depth;
    std::array<TemporalSurfaceInfo,16> textures{};
};
struct TemporalBoundaryInputs {
    TemporalDeviceInputs logical,physical;
    bool jitter_for_frame=false;float jitter_x=0,jitter_y=0;
    HRESULT native_query=E_NOINTERFACE;std::uint64_t native_shifted_resolves=0;
    std::uint64_t projection_applies=0,projection_restores=0;
    std::uint64_t projection_verified_frame=0;UINT projection_verified_banks=0;HRESULT projection_readback=E_PENDING;
};
struct TemporalInputPair {
    std::uint64_t frame=0;
    TemporalBoundaryInputs composite,ui;
};
struct TemporalInputCommand {DWORD size=16,version=1,sequence=0,frames=0;};
static_assert(sizeof(TemporalInputCommand)==16,"Temporal input command ABI");
struct TemporalInputDepthCommand {DWORD size=24,version=2,sequence=0,frames=0,flags=1,reserved=0;};
static_assert(sizeof(TemporalInputDepthCommand)==24,"Temporal depth command ABI");
struct TemporalDepthInfo {
    bool requested=false,attempted=false,written=false;HRESULT result=E_PENDING;
    std::uint64_t frame=0;UINT width=0,height=0,finite=0,interior=0;float minimum=0,maximum=0,scale=0;
};
struct TemporalDepthHeader {
    char magic[8]={'S','7','D','3','2','V','1',0};
    DWORD version=1,width=0,height=0,format=D3DFMT_R32F;
    std::uint64_t frame=0;float normalized_depth_scale=0;DWORD reserved=0;
};
static_assert(sizeof(TemporalDepthHeader)==40,"Private R32 depth ABI");

inline TemporalSurfaceInfo TemporalDescribe(IDirect3DSurface9* surface,HRESULT result){
    TemporalSurfaceInfo info;info.result=result;
    if(FAILED(result)||!surface)return info;
    D3DSURFACE_DESC d{};info.result=surface->GetDesc(&d);
    if(SUCCEEDED(info.result)){info.width=d.Width;info.height=d.Height;info.format=d.Format;info.usage=d.Usage;info.pool=d.Pool;info.multisample=d.MultiSampleType;}
    return info;
}
inline TemporalDeviceInputs ReadTemporalDeviceInputs(IDirect3DDevice9* device){
    using Microsoft::WRL::ComPtr;
    TemporalDeviceInputs value;if(!device)return value;
    // c12-c15 are candidates from the original scene shader's CTAB. A read
    // here does not establish that a fullscreen shader still uses them.
    std::array<float,28> constants{};value.matrix_result=device->GetVertexShaderConstantF(12,constants.data(),7);
    std::copy_n(constants.begin(),16,value.candidate_view_projection.begin());
    std::copy_n(constants.begin()+16,4,value.candidate_eye.begin());std::copy_n(constants.begin()+20,4,value.candidate_camera.begin());
    std::copy_n(constants.begin()+24,4,value.candidate_screen_size.begin());
    value.viewport_result=device->GetViewport(&value.viewport);
    for(DWORD i=0;i<4;++i){ComPtr<IDirect3DSurface9> surface;const HRESULT hr=device->GetRenderTarget(i,&surface);value.targets[i]=TemporalDescribe(surface.Get(),hr);}
    {ComPtr<IDirect3DSurface9> surface;const HRESULT hr=device->GetDepthStencilSurface(&surface);value.depth=TemporalDescribe(surface.Get(),hr);}
    for(DWORD i=0;i<16;++i){ComPtr<IDirect3DBaseTexture9> base;auto& info=value.textures[i];info.result=device->GetTexture(i,&base);
        if(FAILED(info.result)||!base)continue;
        if(base->GetType()!=D3DRTYPE_TEXTURE){info.result=E_NOINTERFACE;continue;}
        ComPtr<IDirect3DTexture9> texture;info.result=base.As(&texture);if(FAILED(info.result)||!texture)continue;
        ComPtr<IDirect3DSurface9> surface;const HRESULT hr=texture->GetSurfaceLevel(0,&surface);info=TemporalDescribe(surface.Get(),hr);
    }return value;
}

class TemporalInputProbe {
public:
    bool Arm(const TemporalInputCommand& c){
        if(c.size!=sizeof(c)||c.version!=1||!c.sequence||c.sequence<=sequence_||!c.frames||c.frames>pairs_.size()){++rejected_;return false;}
        sequence_=c.sequence;requested_=c.frames;count_=0;pending_={};pairs_={};pending_valid_=false;depth_={};return true;
    }
    bool Arm(const TemporalInputDepthCommand& c){
        if(c.size!=sizeof(c)||c.reserved||!((c.version==2&&c.flags==1)||((c.version==3||c.version==4)&&(c.flags==2||c.flags==3)))){++rejected_;return false;}
        TemporalInputCommand base;base.sequence=c.sequence;base.frames=c.frames;
        if(!Arm(base))return false;depth_.requested=(c.flags&1)!=0;return true;
    }
    bool BeginDepth(std::uint64_t frame){
        if(!Active()||!pending_valid_||pending_.frame!=frame||!depth_.requested||depth_.attempted)return false;
        depth_.attempted=true;depth_.frame=frame;depth_.scale=pending_.composite.physical.candidate_camera[3];return true;
    }
    void DepthResult(const TemporalDepthInfo& value){if(depth_.attempted&&value.frame==depth_.frame){const float scale=depth_.scale;depth_=value;depth_.requested=depth_.attempted=true;depth_.scale=scale;}}
    TemporalDepthInfo Depth()const{return depth_;}
    template<class Read> bool Composite(std::uint64_t frame,Read read){
        if(!frame||count_>=requested_||(count_&&pairs_[count_-1].frame>=frame)||(pending_valid_&&pending_.frame>=frame))return false;
        if(pending_valid_){++discarded_;pending_valid_=false;}
        pending_={};pending_.frame=frame;pending_.composite=read();pending_valid_=true;return true;
    }
    template<class Read> bool Ui(std::uint64_t frame,Read read){
        if(count_>=requested_||!pending_valid_||pending_.frame!=frame)return false;
        pending_.ui=read();pairs_[count_++]=pending_;pending_valid_=false;return true;
    }
    void Invalidate(){if(pending_valid_)++discarded_;pending_valid_=false;requested_=count_;}
    DWORD Sequence()const{return sequence_;}
    UINT Count()const{return count_;}
    bool Active()const{return count_<requested_;}
    void Json(std::ostream& out)const{
        out<<std::setprecision((std::numeric_limits<float>::max_digits10));
        out<<"{\"format\":\"s7-on12-temporal-inputs-v1\",\"read_only\":true,\"game_taa_enabled\":false,\"proofs\":0,\"resource_contents_verified\":false"
           <<",\"sequence\":"<<sequence_<<",\"requested\":"<<requested_<<",\"captured\":"<<count_<<",\"discarded\":"<<discarded_<<",\"rejected\":"<<rejected_
           <<",\"depth_capture\":{\"requested\":"<<(depth_.requested?"true":"false")<<",\"attempted\":"<<(depth_.attempted?"true":"false")<<",\"written\":"<<(depth_.written?"true":"false")
           <<",\"hr\":"<<static_cast<long>(depth_.result)<<",\"frame\":"<<depth_.frame<<",\"width\":"<<depth_.width<<",\"height\":"<<depth_.height<<",\"finite\":"<<depth_.finite
           <<",\"interior\":"<<depth_.interior<<",\"minimum\":"<<depth_.minimum<<",\"maximum\":"<<depth_.maximum<<",\"candidate_scale\":";
        if(std::isfinite(depth_.scale))out<<depth_.scale;else out<<"null";out<<"},\"pairs\":[";
        for(UINT i=0;i<count_;++i){if(i)out<<',';out<<"{\"frame\":"<<pairs_[i].frame<<",\"composite\":";BoundaryJson(out,pairs_[i].composite);out<<",\"ui\":";BoundaryJson(out,pairs_[i].ui);out<<'}';}out<<"]}";
    }
private:
    static void SurfaceJson(std::ostream& out,const TemporalSurfaceInfo& s){
        out<<"{\"hr\":"<<static_cast<long>(s.result)<<",\"width\":"<<s.width<<",\"height\":"<<s.height<<",\"format\":"<<s.format<<",\"usage\":"<<s.usage<<",\"pool\":"<<s.pool<<",\"multisample\":"<<s.multisample<<'}';
    }
    static void DeviceJson(std::ostream& out,const TemporalDeviceInputs& d){
        out<<"{\"matrix_hr\":"<<static_cast<long>(d.matrix_result)<<",\"candidate_c12\":[";
        for(UINT i=0;i<16;++i){if(i)out<<',';if(std::isfinite(d.candidate_view_projection[i]))out<<d.candidate_view_projection[i];else out<<"null";}
        out<<"],\"candidate_c16\":";VectorJson(out,d.candidate_eye);out<<",\"candidate_c17\":";VectorJson(out,d.candidate_camera);
        out<<",\"candidate_c18\":";VectorJson(out,d.candidate_screen_size);
        out<<",\"viewport_hr\":"<<static_cast<long>(d.viewport_result)<<",\"viewport\":["<<d.viewport.X<<','<<d.viewport.Y<<','<<d.viewport.Width<<','<<d.viewport.Height<<"],\"targets\":[";
        for(UINT i=0;i<4;++i){if(i)out<<',';SurfaceJson(out,d.targets[i]);}out<<"],\"depth\":";SurfaceJson(out,d.depth);out<<",\"textures\":[";
        for(UINT i=0;i<16;++i){if(i)out<<',';SurfaceJson(out,d.textures[i]);}out<<"]}";
    }
    static void VectorJson(std::ostream& out,const std::array<float,4>& v){out<<'[';for(UINT i=0;i<4;++i){if(i)out<<',';if(std::isfinite(v[i]))out<<v[i];else out<<"null";}out<<']';}
    static void BoundaryJson(std::ostream& out,const TemporalBoundaryInputs& b){out<<"{\"logical\":";DeviceJson(out,b.logical);out<<",\"physical\":";DeviceJson(out,b.physical);
        out<<",\"jitter_for_frame\":"<<(b.jitter_for_frame?"true":"false")<<",\"jitter\":["<<b.jitter_x<<','<<b.jitter_y<<"],\"native_query_hr\":"<<static_cast<long>(b.native_query)<<",\"native_shifted_resolves\":"<<b.native_shifted_resolves
           <<",\"projection_applies\":"<<b.projection_applies<<",\"projection_restores\":"<<b.projection_restores
           <<",\"projection_verified_frame\":"<<b.projection_verified_frame<<",\"projection_verified_banks\":"<<b.projection_verified_banks
           <<",\"projection_readback_hr\":"<<static_cast<long>(b.projection_readback)<<'}';}
    std::array<TemporalInputPair,4> pairs_{};TemporalInputPair pending_{};
    TemporalDepthInfo depth_{};
    DWORD sequence_=0;UINT requested_=0,count_=0;std::uint64_t discarded_=0,rejected_=0;bool pending_valid_=false;
};
}
