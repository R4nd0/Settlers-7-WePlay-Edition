// SPDX-License-Identifier: MIT
#include "shader_debug_overlay.h"
#include <algorithm>
#include <array>
#include <cstring>
#include <float.h>
#include <intrin.h>
#include <vector>

namespace ShaderDebugOverlay { namespace {
struct Guard {
    alignas(16) unsigned char fp[512];DWORD error=GetLastError();
    Guard(){_fxsave(fp);unsigned ignored=0;_controlfp_s(&ignored,_MCW_EM|_RC_NEAR,_MCW_EM|_MCW_RC);}
    ~Guard(){_fxrstor(fp);SetLastError(error);}
};
SRWLOCK lock=SRWLOCK_INIT,input_lock=SRWLOCK_INIT;
struct Lock { SRWLOCK* value;explicit Lock(SRWLOCK* p):value(p){AcquireSRWLockExclusive(value);}~Lock(){ReleaseSRWLockExclusive(value);} };
struct Saved {
    IDirect3DStateBlock9* block=nullptr;
    std::array<IDirect3DSurface9*,4> targets{};
    IDirect3DSurface9* depth=nullptr;
    D3DVIEWPORT9 viewport{};DWORD count=0;
    bool touched=false,scene=false;
};
struct State {
    IDirect3DDevice9* device=nullptr;DWORD thread=0;
    IDirect3DTexture9* font=nullptr;Saved saved{};Stats stats{};
} current;
Selection queued{};bool has_click=false;
constexpr DWORD AtlasWidth=256,AtlasHeight=128;
constexpr LONG PanelX=16,RowY=60,PanelWidth=360,RowHeight=32;
struct Vertex { float x,y,z,rhw;DWORD color;float u,v; };

bool Geometry(DWORD w,DWORD h){return w>=400&&h>=280&&w<=8192&&h<=8192&&std::uint64_t(w)*h<=32u*1024u*1024u;}
template<class T>void Release(T*& value){auto* old=value;value=nullptr;if(old)old->Release();}
template<class T>struct LocalRef { T* value=nullptr;~LocalRef(){Release(value);} };
void DropSaved(){
    Release(current.saved.block);for(auto*& p:current.saved.targets)Release(p);Release(current.saved.depth);
    current.saved=Saved{};current.stats.pending_restore=current.stats.retained_original_refs=0;
}
void Counts(){
    current.stats.retained_original_refs=current.saved.block?1u:0u;
    for(auto* p:current.saved.targets)if(p)++current.stats.retained_original_refs;
    if(current.saved.depth)++current.stats.retained_original_refs;
    current.stats.pending_restore=current.saved.touched?1u:0u;
}
bool Restore(){
    auto& s=current.saved;auto* d=current.device;
    if(!s.touched){DropSaved();return true;}
    bool okay=true;
    if(s.scene){if(FAILED(d->EndScene()))okay=false;else s.scene=false;}
    if(s.scene){current.stats.unsafe_state=1;Counts();++current.stats.restore_failures;return false;}
    // Detach incompatible low/high MRT/depth combinations before changing RT0.
    for(DWORD i=1;i<s.count;++i)if(FAILED(d->SetRenderTarget(i,nullptr)))okay=false;
    if(FAILED(d->SetDepthStencilSurface(nullptr)))okay=false;
    if(FAILED(d->SetRenderTarget(0,s.targets[0])))okay=false;
    for(DWORD i=1;i<s.count;++i)if(FAILED(d->SetRenderTarget(i,s.targets[i])))okay=false;
    if(FAILED(d->SetDepthStencilSurface(s.depth)))okay=false;
    if(!s.block||FAILED(s.block->Apply()))okay=false;
    if(FAILED(d->SetViewport(&s.viewport)))okay=false;
    // State blocks do not contain RT/depth bindings. Verify explicit restores.
    for(DWORD i=0;i<s.count;++i){
        LocalRef<IDirect3DSurface9> observed;const HRESULT hr=d->GetRenderTarget(i,&observed.value);
        const bool available=SUCCEEDED(hr)||(hr==D3DERR_NOTFOUND&&!observed.value&&i>0);
        if(!available||observed.value!=s.targets[i])okay=false;
    }
    LocalRef<IDirect3DSurface9> depth;const HRESULT hr=d->GetDepthStencilSurface(&depth.value);
    if((FAILED(hr)&&!(hr==D3DERR_NOTFOUND&&!depth.value))||depth.value!=s.depth)okay=false;
    D3DVIEWPORT9 viewport{};
    if(FAILED(d->GetViewport(&viewport))||std::memcmp(&viewport,&s.viewport,sizeof(viewport)))okay=false;
    if(!okay){current.stats.unsafe_state=1;Counts();++current.stats.restore_failures;return false;}
    ++current.stats.restorations;current.stats.unsafe_state=0;DropSaved();return true;
}
bool Save(IDirect3DDevice9* d){
    D3DCAPS9 caps{};if(FAILED(d->GetDeviceCaps(&caps))||caps.NumSimultaneousRTs<1||caps.NumSimultaneousRTs>4)return false;
    auto& s=current.saved;s.count=caps.NumSimultaneousRTs;
    if(FAILED(d->CreateStateBlock(D3DSBT_ALL,&s.block))||!s.block||FAILED(s.block->Capture()))return false;
    for(DWORD i=0;i<s.count;++i){
        const HRESULT result=d->GetRenderTarget(i,&s.targets[i]);
        if(FAILED(result)&&!(result==D3DERR_NOTFOUND&&!s.targets[i]&&i>0))return false;
        if(!i&&!s.targets[i])return false;
    }
    const HRESULT depth=d->GetDepthStencilSurface(&s.depth);
    if(FAILED(depth)&&!(depth==D3DERR_NOTFOUND&&!s.depth))return false;
    return SUCCEEDED(d->GetViewport(&s.viewport));
}
bool Atlas(IDirect3DDevice9* d){
    if(current.font)return true;
    struct Gdi {
        HDC dc=nullptr;HBITMAP bitmap=nullptr;HFONT font=nullptr;HGDIOBJ old_bitmap=nullptr,old_font=nullptr;
        ~Gdi(){if(old_font&&old_font!=HGDI_ERROR)SelectObject(dc,old_font);if(old_bitmap&&old_bitmap!=HGDI_ERROR)SelectObject(dc,old_bitmap);
            if(font)DeleteObject(font);if(bitmap)DeleteObject(bitmap);if(dc)DeleteDC(dc);}
    } gdi;
    struct Texture { IDirect3DTexture9* value=nullptr;~Texture(){Release(value);} } texture;
    HDC dc=gdi.dc=CreateCompatibleDC(nullptr);if(!dc)return false;
    BITMAPINFO info{};info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);info.bmiHeader.biWidth=AtlasWidth;
    info.bmiHeader.biHeight=-static_cast<LONG>(AtlasHeight);info.bmiHeader.biPlanes=1;info.bmiHeader.biBitCount=32;info.bmiHeader.biCompression=BI_RGB;
    void* pixels=nullptr;HBITMAP bitmap=gdi.bitmap=CreateDIBSection(dc,&info,DIB_RGB_COLORS,&pixels,nullptr,0);
    HFONT font=gdi.font=CreateFontW(-14,0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,ANSI_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,ANTIALIASED_QUALITY,FIXED_PITCH,L"Consolas");
    bool okay=bitmap&&pixels&&font;
    if(okay){
        gdi.old_bitmap=SelectObject(dc,bitmap);gdi.old_font=SelectObject(dc,font);
        okay=gdi.old_bitmap&&gdi.old_font&&gdi.old_bitmap!=HGDI_ERROR&&gdi.old_font!=HGDI_ERROR;
        if(okay){
            std::memset(pixels,0,AtlasWidth*AtlasHeight*4);SetBkColor(dc,RGB(0,0,0));SetTextColor(dc,RGB(255,255,255));SetBkMode(dc,OPAQUE);
            for(unsigned c=32;c<127&&okay;++c){const wchar_t ch=static_cast<wchar_t>(c);const unsigned i=c-32;
                okay=TextOutW(dc,static_cast<int>((i%16)*12+1),static_cast<int>((i/16)*18),&ch,1)!=FALSE;}
            GdiFlush();
            if(okay){
                const HRESULT created=d->CreateTexture(AtlasWidth,AtlasHeight,1,0,D3DFMT_A8R8G8B8,D3DPOOL_MANAGED,&texture.value,nullptr);
                okay=SUCCEEDED(created)&&texture.value;
                D3DLOCKED_RECT locked{};
                if(okay){
                    const HRESULT result=texture.value->LockRect(0,&locked,nullptr,0);
                    okay=SUCCEEDED(result)&&locked.pBits&&locked.Pitch>=static_cast<INT>(AtlasWidth*4);
                    if(SUCCEEDED(result)){
                        if(okay)for(DWORD y=0;y<AtlasHeight;++y){
                            auto* row=reinterpret_cast<DWORD*>(static_cast<unsigned char*>(locked.pBits)+std::size_t(y)*locked.Pitch);
                            const auto* source=static_cast<const DWORD*>(pixels)+y*AtlasWidth;
                            for(DWORD x=0;x<AtlasWidth;++x){const DWORD p=source[x];const DWORD a=std::max({p&255u,(p>>8)&255u,(p>>16)&255u});row[x]=(a<<24)|0xffffffu;}
                            if(y==AtlasHeight-1)row[AtlasWidth-1]=0xffffffffu;
                        }
                        if(FAILED(texture.value->UnlockRect(0)))okay=false;
                    }
                }
            }
        }
    }
    if(okay){current.font=texture.value;texture.value=nullptr;}
    current.stats.font_ready=current.font?1u:0u;return okay;
}
void Quad(std::vector<Vertex>& out,float x,float y,float w,float h,DWORD color,float u,float v,float u1,float v1){
    if(out.size()+6>4096)throw 1;
    const Vertex a{x-.5f,y-.5f,0,1,color,u,v},b{x+w-.5f,y-.5f,0,1,color,u1,v},c{x+w-.5f,y+h-.5f,0,1,color,u1,v1},e{x-.5f,y+h-.5f,0,1,color,u,v1};
    out.insert(out.end(),{a,b,c,a,c,e});
}
void Solid(std::vector<Vertex>& out,float x,float y,float w,float h,DWORD color){Quad(out,x,y,w,h,color,255.5f/256,127.5f/128,255.5f/256,127.5f/128);}
void Text(std::vector<Vertex>& out,float x,float y,const char* text,DWORD color){
    for(unsigned count=0;text[count]&&count<64;++count){unsigned c=static_cast<unsigned char>(text[count]);if(c<32||c>126)c='?';const unsigned i=c-32;
        const float u=static_cast<float>((i%16)*12)/256,v=static_cast<float>((i/16)*18)/128;
        Quad(out,x+static_cast<float>(count*8),y,12,18,color,u,v,u+12.f/256,v+18.f/128);}
}
bool SetUp(IDirect3DDevice9* d,IDirect3DSurface9* target,DWORD w,DWORD h){
    for(DWORD i=1;i<current.saved.count;++i)if(FAILED(d->SetRenderTarget(i,nullptr)))return false;
    if(FAILED(d->SetDepthStencilSurface(nullptr))||FAILED(d->SetRenderTarget(0,target)))return false;
    const D3DVIEWPORT9 vp{0,0,w,h,0,1};
    if(FAILED(d->SetViewport(&vp))||FAILED(d->SetVertexShader(nullptr))||FAILED(d->SetPixelShader(nullptr))
        ||FAILED(d->SetFVF(D3DFVF_XYZRHW|D3DFVF_DIFFUSE|D3DFVF_TEX1))||FAILED(d->SetStreamSourceFreq(0,1))||FAILED(d->SetTexture(0,current.font)))return false;
    const std::pair<D3DRENDERSTATETYPE,DWORD> render[]={
        {D3DRS_ZENABLE,FALSE},{D3DRS_ZWRITEENABLE,FALSE},{D3DRS_ALPHATESTENABLE,FALSE},{D3DRS_LIGHTING,FALSE},
        {D3DRS_FOGENABLE,FALSE},{D3DRS_STENCILENABLE,FALSE},{D3DRS_CULLMODE,D3DCULL_NONE},{D3DRS_SCISSORTESTENABLE,FALSE},{D3DRS_FILLMODE,D3DFILL_SOLID},
        {D3DRS_ALPHABLENDENABLE,TRUE},{D3DRS_SRCBLEND,D3DBLEND_SRCALPHA},{D3DRS_DESTBLEND,D3DBLEND_INVSRCALPHA},
        {D3DRS_BLENDOP,D3DBLENDOP_ADD},{D3DRS_SEPARATEALPHABLENDENABLE,FALSE},{D3DRS_COLORWRITEENABLE,15},{D3DRS_SRGBWRITEENABLE,FALSE}};
    for(const auto& p:render)if(FAILED(d->SetRenderState(p.first,p.second)))return false;
    if(FAILED(d->SetTextureStageState(0,D3DTSS_COLOROP,D3DTOP_MODULATE))||FAILED(d->SetTextureStageState(0,D3DTSS_COLORARG1,D3DTA_TEXTURE))
        ||FAILED(d->SetTextureStageState(0,D3DTSS_COLORARG2,D3DTA_DIFFUSE))||FAILED(d->SetTextureStageState(0,D3DTSS_ALPHAOP,D3DTOP_MODULATE))
        ||FAILED(d->SetTextureStageState(0,D3DTSS_ALPHAARG1,D3DTA_TEXTURE))||FAILED(d->SetTextureStageState(0,D3DTSS_ALPHAARG2,D3DTA_DIFFUSE))
        ||FAILED(d->SetTextureStageState(0,D3DTSS_TEXCOORDINDEX,0))||FAILED(d->SetTextureStageState(0,D3DTSS_TEXTURETRANSFORMFLAGS,D3DTTFF_DISABLE))
        ||FAILED(d->SetTextureStageState(1,D3DTSS_COLOROP,D3DTOP_DISABLE))||FAILED(d->SetTextureStageState(1,D3DTSS_ALPHAOP,D3DTOP_DISABLE)))return false;
    const std::pair<D3DSAMPLERSTATETYPE,DWORD> sampler[]={{D3DSAMP_MINFILTER,D3DTEXF_POINT},{D3DSAMP_MAGFILTER,D3DTEXF_POINT},
        {D3DSAMP_MIPFILTER,D3DTEXF_NONE},{D3DSAMP_ADDRESSU,D3DTADDRESS_CLAMP},{D3DSAMP_ADDRESSV,D3DTADDRESS_CLAMP},{D3DSAMP_SRGBTEXTURE,FALSE}};
    for(const auto& p:sampler)if(FAILED(d->SetSamplerState(0,p.first,p.second)))return false;
    return true;
}
} // namespace
bool QueueClick(LONG x,LONG y,DWORD w,DWORD h) noexcept{
    Guard guard;if(!Geometry(w,h)||x<PanelX||x>=PanelX+PanelWidth||y<RowY||y>=RowY+4*RowHeight)return false;
    const DWORD row=static_cast<DWORD>((y-RowY)/RowHeight);Lock held(&input_lock);
    queued={row?1u:0u,row?row-1:0u};has_click=true;return true;
}
bool HitTest(LONG x,LONG y,DWORD w,DWORD h) noexcept{Guard guard;return Geometry(w,h)&&x>=16&&x<376&&y>=16&&y<228;}
bool ConsumeSelection(Selection* output) noexcept{Guard guard;if(!output)return false;Lock held(&input_lock);if(!has_click)return false;*output=queued;has_click=false;return true;}
bool Draw(IDirect3DDevice9* d,IDirect3DSurface9* output,DWORD w,DWORD h,const Status& status) noexcept{
    Guard guard;Lock held(&lock);bool painted=false;
    if(!d||!output||!Geometry(w,h)||current.stats.unsafe_state||current.saved.touched
        ||(current.device&&current.device!=d)||(current.thread&&current.thread!=GetCurrentThreadId())
        ||status.applied_valid>1||status.applied_enabled>1||status.applied_variant>2||status.pending>1||status.pending_enabled>1||status.pending_variant>2){++current.stats.skipped;return false;}
    current.device=d;current.thread=GetCurrentThreadId();
    try{
        D3DSURFACE_DESC desc{};
        if(FAILED(output->GetDesc(&desc))||desc.Width!=w||desc.Height!=h||desc.Type!=D3DRTYPE_SURFACE||!(desc.Usage&D3DUSAGE_RENDERTARGET)
            ||(desc.Format!=D3DFMT_A8R8G8B8&&desc.Format!=D3DFMT_X8R8G8B8)||!Atlas(d)||!Save(d)){DropSaved();++current.stats.failures;return false;}
        current.saved.touched=true;Counts();
        if(SetUp(d,output,w,h)&&SUCCEEDED(d->BeginScene())){
            current.saved.scene=true;std::vector<Vertex> vertices;vertices.reserve(2048);
            Solid(vertices,16,16,360,212,0xe018202cu);Text(vertices,28,28,"SHADER-AUSWAHL",0xffdce8ffu);
            const char* names[]={"Original","Wasser B","Klareres Ufer","Mehr Glanz"};
            for(DWORD row=0;row<4;++row){
                const bool applied=status.applied_valid&&(row?status.applied_enabled&&status.applied_variant==row-1:!status.applied_enabled);
                const bool pending=status.pending&&(row?status.pending_enabled&&status.pending_variant==row-1:!status.pending_enabled);
                const float y=static_cast<float>(RowY+row*RowHeight);
                Solid(vertices,20,y,352,30,pending?0xdd685125u:applied?0xdd245c45u:0xcc26303cu);
                Text(vertices,28,y+6,names[row],0xffffffffu);
                if(pending)Text(vertices,284,y+6,"WARTET",0xffffd482u);else if(applied)Text(vertices,284,y+6,"AKTIV",0xff9cf3bbu);
            }
            Text(vertices,28,198,"Strg+Umschalt+F9: anzeigen/verbergen",0xffb8c6d8u);
            painted=SUCCEEDED(d->DrawPrimitiveUP(D3DPT_TRIANGLELIST,static_cast<UINT>(vertices.size()/3),vertices.data(),sizeof(Vertex)));
        }
        const bool restored=Restore();if(painted&&restored){++current.stats.draws;return true;}
        ++current.stats.failures;return false;
    }catch(...){++current.stats.failures;try{Restore();}catch(...){current.stats.unsafe_state=1;Counts();}return false;}
}
bool RetryRestore(IDirect3DDevice9* d) noexcept{
    Guard guard;Lock held(&lock);if(!current.stats.unsafe_state||current.device!=d||current.thread!=GetCurrentThreadId())return false;
    try{return Restore();}catch(...){current.stats.unsafe_state=1;Counts();return false;}
}
bool Reset(IDirect3DDevice9* d) noexcept{
    Guard guard;Lock held(&lock);if(current.saved.touched||current.stats.unsafe_state||(current.device&&current.device!=d))return false;
    try{DropSaved();Release(current.font);current.device=nullptr;current.thread=0;current.stats.font_ready=0;return true;}catch(...){current.stats.unsafe_state=1;return false;}
}
bool Shutdown() noexcept{
    Guard guard;Lock held(&lock);if(current.saved.touched||current.stats.unsafe_state)return false;
    try{DropSaved();Release(current.font);current.device=nullptr;current.thread=0;current.stats.font_ready=0;Lock input(&input_lock);has_click=false;return true;}catch(...){current.stats.unsafe_state=1;return false;}
}
bool ReadStats(Stats* out) noexcept{Guard guard;if(!out||out->size!=sizeof(Stats)||out->version!=1)return false;Lock held(&lock);Counts();*out=current.stats;return true;}
bool HasUnsafe() noexcept{Guard guard;Lock held(&lock);return current.stats.unsafe_state!=0;}
}
