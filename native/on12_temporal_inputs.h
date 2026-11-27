#pragma once
#include <d3d9.h>
#include <wrl/client.h>
#include <array>
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
    D3DVIEWPORT9 viewport{};
    std::array<TemporalSurfaceInfo,4> targets{};
    TemporalSurfaceInfo depth;
    std::array<TemporalSurfaceInfo,16> textures{};
};
struct TemporalBoundaryInputs {TemporalDeviceInputs logical,physical;};
struct TemporalInputPair {
    std::uint64_t frame=0;
    TemporalBoundaryInputs composite,ui;
};
struct TemporalInputCommand {DWORD size=16,version=1,sequence=0,frames=0;};
static_assert(sizeof(TemporalInputCommand)==16,"Temporal input command ABI");

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
    value.matrix_result=device->GetVertexShaderConstantF(12,value.candidate_view_projection.data(),4);
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
        sequence_=c.sequence;requested_=c.frames;count_=0;pending_={};pairs_={};pending_valid_=false;return true;
    }
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
           <<",\"sequence\":"<<sequence_<<",\"requested\":"<<requested_<<",\"captured\":"<<count_<<",\"discarded\":"<<discarded_<<",\"rejected\":"<<rejected_<<",\"pairs\":[";
        for(UINT i=0;i<count_;++i){if(i)out<<',';out<<"{\"frame\":"<<pairs_[i].frame<<",\"composite\":";BoundaryJson(out,pairs_[i].composite);out<<",\"ui\":";BoundaryJson(out,pairs_[i].ui);out<<'}';}out<<"]}";
    }
private:
    static void SurfaceJson(std::ostream& out,const TemporalSurfaceInfo& s){
        out<<"{\"hr\":"<<static_cast<long>(s.result)<<",\"width\":"<<s.width<<",\"height\":"<<s.height<<",\"format\":"<<s.format<<",\"usage\":"<<s.usage<<",\"pool\":"<<s.pool<<",\"multisample\":"<<s.multisample<<'}';
    }
    static void DeviceJson(std::ostream& out,const TemporalDeviceInputs& d){
        out<<"{\"matrix_hr\":"<<static_cast<long>(d.matrix_result)<<",\"candidate_c12\":[";
        for(UINT i=0;i<16;++i){if(i)out<<',';if(std::isfinite(d.candidate_view_projection[i]))out<<d.candidate_view_projection[i];else out<<"null";}
        out<<"],\"viewport_hr\":"<<static_cast<long>(d.viewport_result)<<",\"viewport\":["<<d.viewport.X<<','<<d.viewport.Y<<','<<d.viewport.Width<<','<<d.viewport.Height<<"],\"targets\":[";
        for(UINT i=0;i<4;++i){if(i)out<<',';SurfaceJson(out,d.targets[i]);}out<<"],\"depth\":";SurfaceJson(out,d.depth);out<<",\"textures\":[";
        for(UINT i=0;i<16;++i){if(i)out<<',';SurfaceJson(out,d.textures[i]);}out<<"]}";
    }
    static void BoundaryJson(std::ostream& out,const TemporalBoundaryInputs& b){out<<"{\"logical\":";DeviceJson(out,b.logical);out<<",\"physical\":";DeviceJson(out,b.physical);out<<'}';}
    std::array<TemporalInputPair,4> pairs_{};TemporalInputPair pending_{};
    DWORD sequence_=0;UINT requested_=0,count_=0;std::uint64_t discarded_=0,rejected_=0;bool pending_valid_=false;
};
}
