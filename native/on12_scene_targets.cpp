#include "on12_scene_targets.h"
#include "water_on12_identity.h"
#include <wrl/client.h>
#include <vector>
#include <cstring>

namespace On12Graphics {
using Microsoft::WRL::ComPtr;
namespace {
// dxwrapper's pinned QueryInterface extension returns a BORROWED interface.
constexpr GUID RealInterface{0x00000000,0x1c77,0x4d40,{0xb0,0xcf,0x98,0xfe,0xfd,0xff,0xff,0xff}};
template<class T> HRESULT RealSurface(T* wrapped, IDirect3DDevice9* device, ComPtr<IDirect3DSurface9>& result) {
    IDirect3DSurface9* borrowed = nullptr;
    HRESULT hr = wrapped->QueryInterface(RealInterface, reinterpret_cast<void**>(&borrowed));
    if (FAILED(hr) || !WaterOn12Identity::Range(borrowed,sizeof(void*))) return E_NOINTERFACE;
    auto** table = *reinterpret_cast<void***>(borrowed); auto** dt = *reinterpret_cast<void***>(device);
    if (!WaterOn12Identity::Range(table,4*sizeof(void*))) return E_NOINTERFACE;
    MEMORY_BASIC_INFORMATION native{}; VirtualQuery(dt[9], &native, sizeof(native));
    for (UINT i=0;i<4;++i) if (!WaterOn12Identity::Belongs(table[i],static_cast<HMODULE>(native.AllocationBase))) return E_NOINTERFACE;
    ComPtr<IDirect3DDevice9> owner; hr = borrowed->GetDevice(&owner);
    if (FAILED(hr) || owner.Get()!=device) return E_NOINTERFACE;
    result = borrowed; // AddRef ourselves: the extension did not add one.
    return S_OK;
}
bool Same(IUnknown* a,IUnknown* b) {
    if(a==b)return true;if(!a||!b)return false;
    ComPtr<IUnknown> x,y;return SUCCEEDED(a->QueryInterface(IID_PPV_ARGS(&x)))&&SUCCEEDED(b->QueryInterface(IID_PPV_ARGS(&y)))&&x.Get()==y.Get();
}
LONG Coordinate(LONG v,UINT numerator,UINT denominator) { return static_cast<LONG>(static_cast<LONGLONG>(v)*numerator/denominator); }
}
struct SceneTargets::Impl {
    struct Target {
        ComPtr<IDirect3DSurface9> high,low,low_raw;
        ComPtr<IDirect3DTexture9> high_texture,low_texture;
        D3DSURFACE_DESC desc{}; UINT width=0,height=0; bool main=false,depth=false;
    };
    struct Bindings {
        std::array<ComPtr<IDirect3DSurface9>,4> targets;
        ComPtr<IDirect3DSurface9> depth;
        D3DVIEWPORT9 viewport{};
        RECT scissor{};
    };
    ComPtr<IDirect3DDevice9> wrapped;
    ComPtr<IDirect3DSurface9> main;
    FrameContext* context=nullptr;
    std::array<void*,119> functions{};
    std::vector<std::unique_ptr<Target>> targets;
    UINT sw=0,sh=0,nw=0,nh=0;DWORD thread=0;
    bool active=false,ui=false,physical_high=false,unsafe=false,suspended=false;
    SceneStats stats;
    template<class F> F Fn(UINT slot) const {return reinterpret_cast<F>(functions[slot]);}
    HRESULT SetRT(DWORD i,IDirect3DSurface9* s){return Fn<HRESULT(STDMETHODCALLTYPE*)(IDirect3DDevice9*,DWORD,IDirect3DSurface9*)>(37)(wrapped.Get(),i,s);}
    HRESULT GetRT(DWORD i,IDirect3DSurface9** s){return Fn<HRESULT(STDMETHODCALLTYPE*)(IDirect3DDevice9*,DWORD,IDirect3DSurface9**)>(38)(wrapped.Get(),i,s);}
    HRESULT SetDS(IDirect3DSurface9* s){return Fn<HRESULT(STDMETHODCALLTYPE*)(IDirect3DDevice9*,IDirect3DSurface9*)>(39)(wrapped.Get(),s);}
    HRESULT GetDS(IDirect3DSurface9** s){return Fn<HRESULT(STDMETHODCALLTYPE*)(IDirect3DDevice9*,IDirect3DSurface9**)>(40)(wrapped.Get(),s);}
    HRESULT SetVP(const D3DVIEWPORT9* v){return Fn<HRESULT(STDMETHODCALLTYPE*)(IDirect3DDevice9*,const D3DVIEWPORT9*)>(47)(wrapped.Get(),v);}
    HRESULT GetVP(D3DVIEWPORT9* v){return Fn<HRESULT(STDMETHODCALLTYPE*)(IDirect3DDevice9*,D3DVIEWPORT9*)>(48)(wrapped.Get(),v);}
    HRESULT SetTX(DWORD i,IDirect3DBaseTexture9* t){return Fn<HRESULT(STDMETHODCALLTYPE*)(IDirect3DDevice9*,DWORD,IDirect3DBaseTexture9*)>(65)(wrapped.Get(),i,t);}
    HRESULT GetTX(DWORD i,IDirect3DBaseTexture9** t){return Fn<HRESULT(STDMETHODCALLTYPE*)(IDirect3DDevice9*,DWORD,IDirect3DBaseTexture9**)>(64)(wrapped.Get(),i,t);}
    HRESULT SetSC(const RECT* r){return Fn<HRESULT(STDMETHODCALLTYPE*)(IDirect3DDevice9*,const RECT*)>(75)(wrapped.Get(),r);}
    HRESULT GetSC(RECT* r){return Fn<HRESULT(STDMETHODCALLTYPE*)(IDirect3DDevice9*,RECT*)>(76)(wrapped.Get(),r);}
    Target* FindHigh(IDirect3DSurface9* s)const{for(const auto& t:targets)if(t->high.Get()==s)return t.get();return nullptr;}
    Target* FindLow(IDirect3DSurface9* s)const{for(const auto& t:targets)if(t->low.Get()==s)return t.get();return nullptr;}
    bool Extent(const D3DSURFACE_DESC& d,UINT& w,UINT& h)const{
        if(d.Pool!=D3DPOOL_DEFAULT||d.MultiSampleType!=D3DMULTISAMPLE_NONE)return false;
        for(UINT shift=0;shift<5;++shift)if(d.Width==(nw>>shift)&&d.Height==(nh>>shift)){
            w=sw>>shift;h=sh>>shift;return w&&h;
        }return false;
    }
    HRESULT Fail(HRESULT hr){if(SUCCEEDED(stats.failure))stats.failure=hr;unsafe=true;return hr;}
    Target* Map(IDirect3DSurface9* s){
        if(!s)return nullptr;if(auto* existing=FindHigh(s))return existing;
        D3DSURFACE_DESC d{};UINT w=0,h=0;
        if(FAILED(s->GetDesc(&d))||!Extent(d,w,h)||!(d.Usage&(D3DUSAGE_RENDERTARGET|D3DUSAGE_DEPTHSTENCIL)))return nullptr;
        if(targets.size()>=64){Fail(E_OUTOFMEMORY);return nullptr;}
        auto t=std::make_unique<Target>();t->high=s;t->desc=d;t->width=w;t->height=h;t->main=Same(s,main.Get());t->depth=(d.Usage&D3DUSAGE_DEPTHSTENCIL)!=0;
        HRESULT hr=s->GetContainer(IID_PPV_ARGS(&t->high_texture));
        if(SUCCEEDED(hr)&&t->high_texture){
            D3DSURFACE_DESC base{};
            if(t->high_texture->GetLevelCount()!=1||FAILED(t->high_texture->GetLevelDesc(0,&base))||base.Width!=d.Width||base.Height!=d.Height)return nullptr;
            if(base.Usage&~(D3DUSAGE_RENDERTARGET|D3DUSAGE_DEPTHSTENCIL))return nullptr;
            hr=wrapped->CreateTexture(w,h,1,base.Usage,base.Format,D3DPOOL_DEFAULT,&t->low_texture,nullptr);
            if(SUCCEEDED(hr))hr=t->low_texture->GetSurfaceLevel(0,&t->low);
        }else if(t->depth)hr=wrapped->CreateDepthStencilSurface(w,h,d.Format,D3DMULTISAMPLE_NONE,0,TRUE,&t->low,nullptr);
        else hr=wrapped->CreateRenderTarget(w,h,d.Format,D3DMULTISAMPLE_NONE,0,FALSE,&t->low,nullptr);
        if(FAILED(hr)){Fail(hr);return nullptr;}
        hr=RealSurface(t->low.Get(),context->NativeDevice(),t->low_raw);if(FAILED(hr)){Fail(hr);return nullptr;}
        auto* result=t.get();targets.push_back(std::move(t));stats.resources=static_cast<UINT>(targets.size());return result;
    }
    HRESULT Read(Bindings& b){
        for(DWORD i=0;i<4;++i){HRESULT hr=GetRT(i,&b.targets[i]);if(FAILED(hr)&&i==0)return hr;if(auto* t=FindLow(b.targets[i].Get()))b.targets[i]=t->high;}
        GetDS(&b.depth);if(auto* t=FindLow(b.depth.Get()))b.depth=t->high;
        HRESULT hr=GetVP(&b.viewport);if(FAILED(hr))return hr;
        hr=GetSC(&b.scissor);if(FAILED(hr))return hr;
        ComPtr<IDirect3DSurface9> actual;hr=GetRT(0,&actual);if(FAILED(hr))return hr;
        if(auto* t=FindLow(actual.Get())){
            b.viewport.X=static_cast<DWORD>(static_cast<UINT64>(b.viewport.X)*t->desc.Width/t->width);
            b.viewport.Y=static_cast<DWORD>(static_cast<UINT64>(b.viewport.Y)*t->desc.Height/t->height);
            b.viewport.Width=static_cast<DWORD>(static_cast<UINT64>(b.viewport.Width)*t->desc.Width/t->width);
            b.viewport.Height=static_cast<DWORD>(static_cast<UINT64>(b.viewport.Height)*t->desc.Height/t->height);
            b.scissor={Coordinate(b.scissor.left,t->desc.Width,t->width),Coordinate(b.scissor.top,t->desc.Height,t->height),Coordinate(b.scissor.right,t->desc.Width,t->width),Coordinate(b.scissor.bottom,t->desc.Height,t->height)};
        }return S_OK;
    }
    HRESULT Bind(const Bindings& b,bool scaled_targets){
        std::array<IDirect3DSurface9*,4> color{};IDirect3DSurface9* depth=b.depth.Get();Target* first=nullptr;
        for(UINT i=0;i<4;++i){auto* t=scaled_targets?Map(b.targets[i].Get()):nullptr;if(unsafe&&scaled_targets)return stats.failure;
            color[i]=t?t->low.Get():b.targets[i].Get();if(i==0)first=t;}
        if(auto* t=scaled_targets?Map(depth):nullptr)depth=t->low.Get();if(unsafe&&scaled_targets)return stats.failure;
        HRESULT hr=SetDS(nullptr);if(FAILED(hr))return Fail(hr);
        for(DWORD i=1;i<4;++i){hr=SetRT(i,nullptr);if(FAILED(hr))return Fail(hr);}
        hr=SetRT(0,color[0]);if(FAILED(hr))return Fail(hr);
        for(DWORD i=1;i<4;++i)if(color[i]){hr=SetRT(i,color[i]);if(FAILED(hr))return Fail(hr);}
        hr=SetDS(depth);if(FAILED(hr))return Fail(hr);
        auto vp=b.viewport;
        if(first){vp.X=static_cast<DWORD>(static_cast<UINT64>(vp.X)*first->width/first->desc.Width);
            vp.Y=static_cast<DWORD>(static_cast<UINT64>(vp.Y)*first->height/first->desc.Height);
            vp.Width=static_cast<DWORD>(static_cast<UINT64>(vp.Width)*first->width/first->desc.Width);
            vp.Height=static_cast<DWORD>(static_cast<UINT64>(vp.Height)*first->height/first->desc.Height);}
        hr=SetVP(&vp);if(FAILED(hr))return Fail(hr);auto sc=first?Rectangle(b.scissor,*first):b.scissor;
        hr=SetSC(&sc);if(FAILED(hr))return Fail(hr);physical_high=!scaled_targets;return S_OK;
    }
    HRESULT Textures(bool low){
        for(DWORD slot=0;slot<16;++slot){ComPtr<IDirect3DBaseTexture9> value;HRESULT hr=GetTX(slot,&value);if(FAILED(hr))return Fail(hr);
            for(const auto& t:targets){if(!t->high_texture)continue;
                if((low&&value.Get()==t->high_texture.Get())||(!low&&value.Get()==t->low_texture.Get())){
                    hr=SetTX(slot,low?t->low_texture.Get():t->high_texture.Get());if(FAILED(hr))return Fail(hr);break;}}
        }return S_OK;
    }
    RECT Rectangle(const RECT& r,const Target& t)const{return{Coordinate(r.left,t.width,t.desc.Width),Coordinate(r.top,t.height,t.desc.Height),Coordinate(r.right,t.width,t.desc.Width),Coordinate(r.bottom,t.height,t.desc.Height)};}
};
SceneTargets::SceneTargets():impl_(new Impl){}
SceneTargets::~SceneTargets(){if(impl_&&FAILED(Shutdown()))(void)impl_.release();}
HRESULT SceneTargets::Initialize(IDirect3DDevice9* device,FrameContext* context,const std::array<void*,119>& functions,UINT sw,UINT sh,UINT nw,UINT nh){
    auto& p=*impl_;if(p.wrapped||!device||!context||!context->NativeDevice()||!sw||!sh||sw>=nw||sh>=nh||nw>4096||nh>4096||static_cast<UINT64>(sw)*nh!=static_cast<UINT64>(sh)*nw)return E_INVALIDARG;
    p.wrapped=device;p.context=context;p.functions=functions;p.sw=sw;p.sh=sh;p.nw=nw;p.nh=nh;p.thread=GetCurrentThreadId();p.targets.reserve(64);
    HRESULT hr=device->GetBackBuffer(0,0,D3DBACKBUFFER_TYPE_MONO,&p.main);if(FAILED(hr))return hr;
    D3DSURFACE_DESC desc{};hr=p.main->GetDesc(&desc);if(FAILED(hr))return hr;
    return desc.Width==nw&&desc.Height==nh&&desc.MultiSampleType==D3DMULTISAMPLE_NONE?S_OK:E_INVALIDARG;
}
bool SceneTargets::Active()const{return impl_->active;}
bool SceneTargets::Ui()const{return impl_->ui;}
bool SceneTargets::Suspended()const{return impl_->suspended;}
bool SceneTargets::Safe()const{return !impl_->unsafe;}
bool SceneTargets::MapsTexture(IDirect3DBaseTexture9* texture)const{if(!texture||!impl_->active)return false;for(const auto& t:impl_->targets)if(t->high_texture.Get()==texture)return true;return false;}
SceneStats SceneTargets::Stats()const{return impl_->stats;}
HRESULT SceneTargets::BeginFrame(){auto& p=*impl_;if(GetCurrentThreadId()!=p.thread)return RPC_E_WRONG_THREAD;if(p.unsafe)return p.stats.failure;
    if(!p.main){HRESULT hr=p.wrapped->GetBackBuffer(0,0,D3DBACKBUFFER_TYPE_MONO,&p.main);if(FAILED(hr))return p.Fail(hr);}
    Impl::Bindings b;HRESULT hr=p.Read(b);if(FAILED(hr))return p.Fail(hr);p.ui=false;p.active=true;hr=p.Bind(b,true);if(SUCCEEDED(hr))hr=p.Textures(true);if(SUCCEEDED(hr))++p.stats.frames;return hr;}
HRESULT SceneTargets::BeginUi(const SpatialSettings& settings){auto& p=*impl_;if(!p.active||p.ui)return S_FALSE;if(p.unsafe)return p.stats.failure;
    Impl::Bindings b;HRESULT hr=p.Read(b);if(FAILED(hr))return p.Fail(hr);auto* scene=p.FindHigh(p.main.Get());
    if(!Same(b.targets[0].Get(),p.main.Get()))return p.Fail(D3DERR_INVALIDCALL);
    hr=p.Bind(b,false);if(FAILED(hr))return hr;p.ui=true;
    if(scene){ComPtr<IDirect3DSurface9> output;hr=p.context->NativeDevice()->GetBackBuffer(0,0,D3DBACKBUFFER_TYPE_MONO,&output);
        if(SUCCEEDED(hr))hr=p.context->Render(scene->low_raw.Get(),output.Get(),settings);
        if(hr==S_FALSE){hr=p.context->Drain(5000);if(SUCCEEDED(hr))hr=p.context->Render(scene->low_raw.Get(),output.Get(),settings);}
        if(hr!=S_OK)return p.Fail(FAILED(hr)?hr:E_PENDING);++p.stats.upscaled;}
    p.stats.ui_width=p.nw;p.stats.ui_height=p.nh;++p.stats.ui_draws;return S_OK;}
HRESULT SceneTargets::BeforeCapture(){auto& p=*impl_;if(!p.active)return S_FALSE;Impl::Bindings b;HRESULT hr=p.Read(b);if(SUCCEEDED(hr))hr=p.Bind(b,false);if(SUCCEEDED(hr))hr=p.Textures(false);if(SUCCEEDED(hr))p.suspended=true;return hr;}
HRESULT SceneTargets::AfterCapture(){auto& p=*impl_;p.suspended=false;if(!p.active)return S_FALSE;Impl::Bindings b;HRESULT hr=p.Read(b);if(SUCCEEDED(hr))hr=p.Bind(b,!p.ui);if(SUCCEEDED(hr))hr=p.Textures(true);return hr;}
HRESULT SceneTargets::AfterApply(){auto& p=*impl_;if(!p.active)return S_FALSE;Impl::Bindings b;HRESULT hr=p.Read(b);
    // Captures use the engine's virtual native viewport/scissor. State blocks
    // do not restore render targets, which may still be physically smaller.
    if(SUCCEEDED(hr))hr=p.GetVP(&b.viewport);if(SUCCEEDED(hr))hr=p.GetSC(&b.scissor);
    if(SUCCEEDED(hr))hr=p.Bind(b,!p.ui);if(SUCCEEDED(hr))hr=p.Textures(true);return hr;}
HRESULT SceneTargets::ReleaseForReset(){auto& p=*impl_;if(!p.wrapped)return S_OK;if(GetCurrentThreadId()!=p.thread)return RPC_E_WRONG_THREAD;
    if(p.context&&!p.context->SafeForD3D9())return E_UNEXPECTED;
    if(p.active){Impl::Bindings b;HRESULT hr=p.Read(b);if(FAILED(hr))return hr;hr=p.Bind(b,false);if(FAILED(hr))return hr;hr=p.Textures(false);if(FAILED(hr))return hr;}
    p.active=false;p.ui=false;p.suspended=false;p.targets.clear();p.main.Reset();p.stats.resources=0;return S_OK;}
HRESULT SceneTargets::Shutdown(){if(!impl_->wrapped)return S_OK;HRESULT hr=ReleaseForReset();if(FAILED(hr))return hr;impl_->wrapped.Reset();impl_->context=nullptr;return S_OK;}
void SceneTargets::ResetSucceeded(){impl_->unsafe=false;impl_->stats.failure=S_OK;}
HRESULT SceneTargets::GetTarget(DWORD i,IDirect3DSurface9** s){auto& p=*impl_;HRESULT hr=p.GetRT(i,s);if(SUCCEEDED(hr)&&s&&*s)if(auto* t=p.FindLow(*s)){(*s)->Release();*s=t->high.Get();(*s)->AddRef();}return hr;}
HRESULT SceneTargets::GetDepth(IDirect3DSurface9** s){auto& p=*impl_;HRESULT hr=p.GetDS(s);if(SUCCEEDED(hr)&&s&&*s)if(auto* t=p.FindLow(*s)){(*s)->Release();*s=t->high.Get();(*s)->AddRef();}return hr;}
HRESULT SceneTargets::SetTarget(DWORD i,IDirect3DSurface9* s){auto& p=*impl_;if(!p.active||p.suspended)return p.SetRT(i,s);Impl::Bindings before;HRESULT hr=p.Read(before);if(FAILED(hr))return p.Fail(hr);
    hr=p.Bind(before,false);if(FAILED(hr))return hr;const HRESULT original=p.SetRT(i,s);Impl::Bindings after;hr=p.Read(after);if(SUCCEEDED(hr))hr=p.Bind(after,!p.ui);if(FAILED(hr))return hr;++p.stats.mapped_targets;return original;}
HRESULT SceneTargets::SetDepth(IDirect3DSurface9* s){auto& p=*impl_;if(!p.active||p.suspended)return p.SetDS(s);Impl::Bindings before;HRESULT hr=p.Read(before);if(FAILED(hr))return p.Fail(hr);
    hr=p.Bind(before,false);if(FAILED(hr))return hr;const HRESULT original=p.SetDS(s);Impl::Bindings after;hr=p.Read(after);if(SUCCEEDED(hr))hr=p.Bind(after,!p.ui);return FAILED(hr)?hr:original;}
HRESULT SceneTargets::SetViewport(const D3DVIEWPORT9* v){auto& p=*impl_;if(!v||!p.active||p.ui||p.suspended)return p.SetVP(v);auto adjusted=*v;ComPtr<IDirect3DSurface9> rt;HRESULT hr=p.GetRT(0,&rt);if(FAILED(hr))return hr;
    if(auto* t=p.FindLow(rt.Get())){adjusted.X=static_cast<DWORD>(static_cast<UINT64>(v->X)*t->width/t->desc.Width);adjusted.Y=static_cast<DWORD>(static_cast<UINT64>(v->Y)*t->height/t->desc.Height);adjusted.Width=static_cast<DWORD>(static_cast<UINT64>(v->Width)*t->width/t->desc.Width);adjusted.Height=static_cast<DWORD>(static_cast<UINT64>(v->Height)*t->height/t->desc.Height);}return p.SetVP(&adjusted);}
HRESULT SceneTargets::GetViewport(D3DVIEWPORT9* v){auto& p=*impl_;if(!v)return p.GetVP(v);Impl::Bindings b;HRESULT hr=p.Read(b);if(SUCCEEDED(hr))*v=b.viewport;return hr;}
HRESULT SceneTargets::SetScissor(const RECT* r){auto& p=*impl_;if(!r||!p.active||p.ui||p.suspended)return p.SetSC(r);ComPtr<IDirect3DSurface9> rt;HRESULT hr=p.GetRT(0,&rt);if(FAILED(hr))return hr;auto scaled=*r;if(auto* t=p.FindLow(rt.Get()))scaled=p.Rectangle(*r,*t);return p.SetSC(&scaled);}
HRESULT SceneTargets::GetScissor(RECT* r){auto& p=*impl_;if(!r)return p.GetSC(r);Impl::Bindings b;HRESULT hr=p.Read(b);if(SUCCEEDED(hr))*r=b.scissor;return hr;}
HRESULT SceneTargets::SetTexture(DWORD i,IDirect3DBaseTexture9* t){auto& p=*impl_;if(p.active&&!p.suspended)for(const auto& row:p.targets)if(row->high_texture.Get()==t){++p.stats.mapped_textures;return p.SetTX(i,row->low_texture.Get());}return p.SetTX(i,t);}
HRESULT SceneTargets::GetTexture(DWORD i,IDirect3DBaseTexture9** t){auto& p=*impl_;HRESULT hr=p.GetTX(i,t);if(SUCCEEDED(hr)&&t&&*t)for(const auto& row:p.targets)if(row->low_texture.Get()==*t){(*t)->Release();*t=row->high_texture.Get();(*t)->AddRef();break;}return hr;}
HRESULT SceneTargets::Stretch(IDirect3DSurface9* s,const RECT* sr,IDirect3DSurface9* d,const RECT* dr,D3DTEXTUREFILTERTYPE f){auto& p=*impl_;auto* a=p.active?p.FindHigh(s):nullptr;auto* b=p.active&&!p.ui?p.Map(d):nullptr;RECT x{},y{};
    if(a&&sr)x=p.Rectangle(*sr,*a);if(b&&dr)y=p.Rectangle(*dr,*b);
    return p.Fn<HRESULT(STDMETHODCALLTYPE*)(IDirect3DDevice9*,IDirect3DSurface9*,const RECT*,IDirect3DSurface9*,const RECT*,D3DTEXTUREFILTERTYPE)>(34)(p.wrapped.Get(),a?a->low.Get():s,a&&sr?&x:sr,b?b->low.Get():d,b&&dr?&y:dr,f);}
HRESULT SceneTargets::Fill(IDirect3DSurface9* s,const RECT* r,D3DCOLOR c){auto& p=*impl_;auto* t=p.active&&!p.ui?p.Map(s):nullptr;RECT x{};if(t&&r)x=p.Rectangle(*r,*t);
    return p.Fn<HRESULT(STDMETHODCALLTYPE*)(IDirect3DDevice9*,IDirect3DSurface9*,const RECT*,D3DCOLOR)>(35)(p.wrapped.Get(),t?t->low.Get():s,t&&r?&x:r,c);}
HRESULT SceneTargets::Clear(DWORD count,const D3DRECT* rectangles,DWORD flags,D3DCOLOR color,float z,DWORD stencil){auto& p=*impl_;std::array<D3DRECT,64> scaled{};const D3DRECT* selected=rectangles;
    if(p.active&&!p.ui&&rectangles&&count){if(count>scaled.size())return D3DERR_INVALIDCALL;ComPtr<IDirect3DSurface9> rt;HRESULT hr=p.GetRT(0,&rt);if(FAILED(hr))return hr;
        if(auto* t=p.FindLow(rt.Get())){for(DWORD i=0;i<count;++i){RECT r{rectangles[i].x1,rectangles[i].y1,rectangles[i].x2,rectangles[i].y2};r=p.Rectangle(r,*t);scaled[i]={r.left,r.top,r.right,r.bottom};}selected=scaled.data();}}
    return p.Fn<HRESULT(STDMETHODCALLTYPE*)(IDirect3DDevice9*,DWORD,const D3DRECT*,DWORD,D3DCOLOR,float,DWORD)>(43)(p.wrapped.Get(),count,selected,flags,color,z,stencil);}
}
