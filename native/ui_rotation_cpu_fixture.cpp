#include "ui_rotation_fix.h"
#include <fstream>
#include <memory>
#include <stdexcept>
#include <vector>
#include <cstring>
#include <cstdio>
#include <thread>

namespace {
unsigned checks=0;
void Check(bool value,const char* message){++checks;if(!value)throw std::runtime_error(message);}
std::vector<DWORD> Input(const std::wstring& path,size_t bytes){
    std::ifstream f(path,std::ios::binary);std::vector<DWORD> code(bytes/4);f.read(reinterpret_cast<char*>(code.data()),bytes);
    Check(f.gcount()==static_cast<std::streamsize>(bytes),"recipient input read");return code;
}
struct Shader{void** table=nullptr;ULONG refs=1;std::vector<DWORD> code;};
ULONG STDMETHODCALLTYPE Add(Shader* s){return ++s->refs;}
ULONG STDMETHODCALLTYPE Drop(Shader* s){Check(s->refs>0,"shader reference underflow");return --s->refs;}
HRESULT STDMETHODCALLTYPE Code(Shader* s,void* data,UINT* bytes){
    const UINT wanted=static_cast<UINT>(s->code.size()*4);if(!data){*bytes=wanted;return S_OK;}
    if(*bytes<wanted)return E_FAIL;std::memcpy(data,s->code.data(),wanted);*bytes=wanted;return S_OK;
}
struct Object{void** table=nullptr;ULONG refs=1;};
ULONG STDMETHODCALLTYPE ObjectAdd(Object* s){return ++s->refs;}
ULONG STDMETHODCALLTYPE ObjectDrop(Object* s){Check(s->refs>1,"object reference underflow");return --s->refs;}
HRESULT STDMETHODCALLTYPE SurfaceDesc(Object*,D3DSURFACE_DESC* d){*d={};d->Width=3840;d->Height=2160;d->Format=D3DFMT_A8R8G8B8;return S_OK;}
HRESULT STDMETHODCALLTYPE BufferDesc(Object*,D3DVERTEXBUFFER_DESC* d){*d={};d->Size=65536;d->Usage=D3DUSAGE_WRITEONLY|D3DUSAGE_DYNAMIC;return S_OK;}
bool bad_declaration=false;
HRESULT STDMETHODCALLTYPE Declaration(Object*,D3DVERTEXELEMENT9* data,UINT* count){
    const D3DVERTEXELEMENT9 expected[]={{0,0,D3DDECLTYPE_FLOAT2,D3DDECLMETHOD_DEFAULT,D3DDECLUSAGE_POSITION,0},
        {0,8,D3DDECLTYPE_SHORT4N,D3DDECLMETHOD_DEFAULT,D3DDECLUSAGE_TEXCOORD,0},
        {0,16,D3DDECLTYPE_SHORT4N,D3DDECLMETHOD_DEFAULT,D3DDECLUSAGE_TEXCOORD,1},
        {0,24,D3DDECLTYPE_D3DCOLOR,D3DDECLMETHOD_DEFAULT,D3DDECLUSAGE_COLOR,0},D3DDECL_END()};
    if(*count<5)return E_FAIL;std::memcpy(data,expected,sizeof(expected));*count=5;if(bad_declaration)data[2].Offset=18;return S_OK;
}
struct Device{
    void** table=nullptr;Shader* original=nullptr;Shader* current=nullptr;Shader* pixel=nullptr;Shader* latest=nullptr;
    Object *target=nullptr,*buffer=nullptr,*declaration=nullptr;void** shader_table=nullptr;
    bool fail_set=false,fail_restore=false,fail_create=false,fail_target=false;unsigned poison_readbacks=0;
    unsigned setters=0,creates=0;std::vector<std::unique_ptr<Shader>> generated;
};
HRESULT STDMETHODCALLTYPE Vertex(Device* d,IDirect3DVertexShader9** out){
    SetLastError(0xbad10001);if(d->current==d->latest&&d->poison_readbacks){--d->poison_readbacks;*out=reinterpret_cast<IDirect3DVertexShader9*>(1);return E_FAIL;}
    *out=reinterpret_cast<IDirect3DVertexShader9*>(d->current);if(d->current)Add(d->current);return S_OK;
}
HRESULT STDMETHODCALLTYPE Pixel(Device* d,IDirect3DPixelShader9** out){*out=reinterpret_cast<IDirect3DPixelShader9*>(d->pixel);Add(d->pixel);return S_OK;}
void Bind(Device* d,Shader* s){if(s==d->current)return;if(s)Add(s);if(d->current)Drop(d->current);d->current=s;}
HRESULT STDMETHODCALLTYPE SetVertex(Device* d,IDirect3DVertexShader9* value){
    ++d->setters;SetLastError(0xbad10002);auto*s=reinterpret_cast<Shader*>(value);
    if(d->fail_restore&&s==d->original)return E_FAIL;
    Bind(d,s);return d->fail_set&&s==d->latest?E_FAIL:S_OK;
}
HRESULT STDMETHODCALLTYPE Create(Device* d,const DWORD* code,IDirect3DVertexShader9** out){
    if(d->fail_create){*out=reinterpret_cast<IDirect3DVertexShader9*>(1);return E_FAIL;}
    auto shader=std::make_unique<Shader>();shader->table=d->shader_table;shader->code.assign(code,code+168);
    d->latest=shader.get();*out=reinterpret_cast<IDirect3DVertexShader9*>(shader.get());d->generated.push_back(std::move(shader));++d->creates;return S_OK;
}
HRESULT STDMETHODCALLTYPE Target(Device*d,DWORD,IDirect3DSurface9**out){
    if(d->fail_target){*out=reinterpret_cast<IDirect3DSurface9*>(1);return E_FAIL;}
    ObjectAdd(d->target);*out=reinterpret_cast<IDirect3DSurface9*>(d->target);return S_OK;
}
HRESULT STDMETHODCALLTYPE Viewport(Device*,D3DVIEWPORT9*out){*out={0,0,3840,2160,0,1};return S_OK;}
HRESULT STDMETHODCALLTYPE Constants(Device*,UINT index,float*out,UINT count){
    Check(index==64&&count==4,"only original GUI projection queried");std::memset(out,0,64);out[0]=2.f/3840;out[5]=-2.f/2160;out[15]=1;return S_OK;
}
HRESULT STDMETHODCALLTYPE Decl(Device*d,IDirect3DVertexDeclaration9**out){ObjectAdd(d->declaration);*out=reinterpret_cast<IDirect3DVertexDeclaration9*>(d->declaration);return S_OK;}
HRESULT STDMETHODCALLTYPE Stream(Device*d,UINT stream,IDirect3DVertexBuffer9**out,UINT*offset,UINT*stride){
    Check(stream==0,"only UI stream metadata queried");ObjectAdd(d->buffer);*out=reinterpret_cast<IDirect3DVertexBuffer9*>(d->buffer);*offset=0;*stride=28;return S_OK;
}
}
int wmain(int argc,wchar_t**argv){
    try{
        Check(argc==2,"use private recipient program directory");const std::wstring root=argv[1];
        std::array<void*,5> shader_table{};shader_table[1]=reinterpret_cast<void*>(Add);shader_table[2]=reinterpret_cast<void*>(Drop);shader_table[4]=reinterpret_cast<void*>(Code);
        Shader original{shader_table.data(),2,Input(root+L"/original-ui.vs.bin",484)},pixel{shader_table.data(),2,Input(root+L"/original-ui.ps.bin",1108)};
        std::array<void*,14> object_table{};object_table[1]=reinterpret_cast<void*>(ObjectAdd);object_table[2]=reinterpret_cast<void*>(ObjectDrop);
        auto surface_table=object_table,buffer_table=object_table,decl_table=object_table;
        surface_table[12]=reinterpret_cast<void*>(SurfaceDesc);buffer_table[13]=reinterpret_cast<void*>(BufferDesc);decl_table[4]=reinterpret_cast<void*>(Declaration);
        Object target{surface_table.data(),1},buffer{buffer_table.data(),1},decl{decl_table.data(),1};
        std::array<void*,119> table{};table[38]=reinterpret_cast<void*>(Target);table[48]=reinterpret_cast<void*>(Viewport);table[88]=reinterpret_cast<void*>(Decl);
        table[91]=reinterpret_cast<void*>(Create);table[92]=reinterpret_cast<void*>(SetVertex);table[93]=reinterpret_cast<void*>(Vertex);
        table[95]=reinterpret_cast<void*>(Constants);table[101]=reinterpret_cast<void*>(Stream);table[108]=reinterpret_cast<void*>(Pixel);
        Device d{};d.table=table.data();d.original=d.current=&original;d.pixel=&pixel;d.target=&target;d.buffer=&buffer;d.declaration=&decl;d.shader_table=shader_table.data();
        auto* device=reinterpret_cast<IDirect3DDevice9*>(&d);UiRotationFix::Initialize(3840,2160);UiRotationFix::Config mode{};
        unsigned calls=0;auto ordinary=[&](){++calls;Check(d.current==&original,"fallback original shader");return S_OK;};
        Check(UiRotationFix::Execute(device,ordinary)==S_OK&&d.setters==0,"off leaves device untouched");
        mode.enabled=1;Check(UiRotationFix::Configure(mode),"enable exact UI diagnostic");
        const auto mxcsr=_mm_getcsr();SetLastError(0x12345678);
        Check(UiRotationFix::Execute(device,[&](){++calls;Check(d.current==d.latest,"generated shader for one original call");Check(GetLastError()==0x12345678&&_mm_getcsr()==mxcsr,"original entry FP/error");SetLastError(0x34567890);return S_OK;})==S_OK,"corrected draw result");
        Check(GetLastError()==0x34567890&&_mm_getcsr()==mxcsr&&d.current==&original,"post-original FP/error and shader restore");
        Check(d.creates==1&&original.refs==2&&pixel.refs==2&&d.latest->refs==1,"bounded cache and balanced original references");
        const auto applied=UiRotationFix::Read().applications;
        bad_declaration=true;Check(UiRotationFix::Execute(device,ordinary)==S_OK&&UiRotationFix::Read().applications==applied,"wrong declaration declines correction");bad_declaration=false;
        d.fail_target=true;Check(UiRotationFix::Execute(device,ordinary)==S_OK,"poisoned failed target getter not dereferenced");d.fail_target=false;
        d.fail_set=true;Check(UiRotationFix::Execute(device,ordinary)==S_OK&&UiRotationFix::Safe(),"failed setter with changed binding restored before original fallback");d.fail_set=false;
        d.poison_readbacks=1;Check(UiRotationFix::Execute(device,ordinary)==S_OK&&UiRotationFix::Safe(),"poisoned failed verification getter restored before original");
        Check(UiRotationFix::Execute(device,[&](){++calls;return E_FAIL;})==E_FAIL&&d.current==&original,"failed original restored");
        bool caught=false;try{UiRotationFix::Execute(device,[]()->HRESULT{throw std::runtime_error("authored original exception");});}catch(const std::runtime_error&){caught=true;}
        Check(caught&&d.current==&original,"original exception propagates after restoration");
        d.fail_restore=true;const auto before=calls;
        Check(UiRotationFix::Execute(device,[&](){++calls;return S_OK;})==D3DERR_DEVICELOST&&!UiRotationFix::Safe(),"failed restoration blocks caller and retains backup");
        Check(UiRotationFix::Read().pending_refs==1&&original.refs==2,"unsafe original reference retained");
        Check(UiRotationFix::Execute(device,[&](){++calls;return S_OK;})==D3DERR_DEVICELOST&&calls==before+1,"unsafe state never calls original again");
        Check(!UiRotationFix::Reset(device),"unrestored backup cannot be discarded on Reset");d.fail_restore=false;
        Check(UiRotationFix::Reset(device)&&UiRotationFix::Safe()&&d.current==&original&&original.refs==2,"retry restores source before releasing backup/cache");
        d.fail_create=true;Check(UiRotationFix::Execute(device,ordinary)==S_OK&&UiRotationFix::Safe(),"poisoned failed creation does not become a cache pointer");d.fail_create=false;
        Check(UiRotationFix::Execute(device,[&](){++calls;return S_OK;})==S_OK,"later creation recovers safely");
        bool foreign_forwarded=false;std::thread worker([&](){foreign_forwarded=UiRotationFix::Execute(device,[&](){return d.current==&original?S_OK:E_FAIL;})==S_OK;});worker.join();Check(foreign_forwarded,"foreign render thread forwards without UI mutation");
        Check(UiRotationFix::Reset(device),"final source reset");
        const auto result=UiRotationFix::Read();Check(!result.pending_refs&&!result.retained_programs&&result.applications==result.restorations,"all completed transactions balanced after recovery");
        Check(original.refs==2&&pixel.refs==2&&target.refs==1&&buffer.refs==1&&decl.refs==1,"all observed source references balanced");
        for(const auto& s:d.generated)Check(s->refs==0,"every generated reference released");
        UiRotationFix::Initialize(1920,1080);Check(UiRotationFix::Configure(mode)&&!UiRotationFix::Enabled(),"ordinary dimensions never activate correction");
        std::printf("{\"format\":\"s7-ui-rotation-cpu-v1\",\"checks\":%u,\"passed\":true,\"game_started\":false,\"gpu_started\":false}\n",checks);return 0;
    }catch(const std::exception&e){std::fprintf(stderr,"ui rotation CPU fixture failed: %s\n",e.what());return 1;}
}
