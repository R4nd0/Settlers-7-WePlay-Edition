// Owned, bounded GPU qualification fixture. Geometry, textures, constants and
// controls and rendered shaders below are authored. The separately staged real
// recipient-generated PS is checked/created only; it is never drawn here.
#include "water_shader_effects.h"
#include <bcrypt.h>
#include <d3dcompiler.h>
#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>
#include <cwchar>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
constexpr DWORD Width=800,Height=600;
constexpr char RendererHash[]="265888c31ca78dffa290c39cb7e50bfb02762590e41927906e46fb32f01497fa";
constexpr char ProductionHash[]="8a27791a9907f73d4f42c847bc74a0a25b57da48b81f8e9cb566eca59bfcf0d1";
template<class T> struct Com {
    T* value=nullptr;
    ~Com(){if(value)value->Release();}
    T* operator->()const{return value;}
    void clear(){auto* old=value;value=nullptr;if(old)old->Release();}
};
void Check(bool value,const char* text){if(!value)throw std::runtime_error(text);}
void HR(HRESULT result,const char* text){if(FAILED(result)){std::fprintf(stderr,"HRESULT %08lx: %s\n",static_cast<DWORD>(result),text);throw std::runtime_error(text);}}
std::string Hash(const void* data,size_t bytes){
    BCRYPT_ALG_HANDLE algorithm=nullptr;BCRYPT_HASH_HANDLE hash=nullptr;std::array<unsigned char,32> result{};
    Check(BCryptOpenAlgorithmProvider(&algorithm,BCRYPT_SHA256_ALGORITHM,nullptr,0)>=0,"hash provider");
    bool valid=bytes<=MAXDWORD&&BCryptCreateHash(algorithm,&hash,nullptr,0,nullptr,0,0)>=0;
    if(valid)valid=BCryptHashData(hash,const_cast<PUCHAR>(static_cast<const unsigned char*>(data)),static_cast<ULONG>(bytes),0)>=0;
    if(valid)valid=BCryptFinishHash(hash,result.data(),32,0)>=0;
    if(hash)BCryptDestroyHash(hash);BCryptCloseAlgorithmProvider(algorithm,0);Check(valid,"file digest");
    std::string text;for(auto byte:result){constexpr char digits[]="0123456789abcdef";text+=digits[byte>>4];text+=digits[byte&15];}return text;
}
std::vector<unsigned char> Read(const std::wstring& path,size_t maximum){
    HANDLE file=CreateFileW(path.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,
                            FILE_ATTRIBUTE_NORMAL|FILE_FLAG_OPEN_REPARSE_POINT,nullptr);
    Check(file!=INVALID_HANDLE_VALUE,"private input exists");LARGE_INTEGER size{};BY_HANDLE_FILE_INFORMATION info{};
    bool valid=GetFileSizeEx(file,&size)&&size.QuadPart>0&&size.QuadPart<=static_cast<LONGLONG>(maximum)
        &&GetFileInformationByHandle(file,&info)&&!(info.dwFileAttributes&(FILE_ATTRIBUTE_REPARSE_POINT|FILE_ATTRIBUTE_DIRECTORY));
    std::vector<unsigned char> bytes;if(valid)bytes.resize(static_cast<size_t>(size.QuadPart));DWORD got=0;
    if(valid)valid=ReadFile(file,bytes.data(),static_cast<DWORD>(bytes.size()),&got,nullptr)&&got==bytes.size();
    CloseHandle(file);Check(valid,"bounded non-reparse input read");return bytes;
}
void Write(const std::wstring& path,const void* data,size_t bytes){
    HANDLE file=CreateFileW(path.c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);
    Check(file!=INVALID_HANDLE_VALUE,"fresh private output");DWORD wrote=0;
    const bool valid=bytes<=MAXDWORD&&WriteFile(file,data,static_cast<DWORD>(bytes),&wrote,nullptr)&&wrote==bytes;
    CloseHandle(file);Check(valid,"complete private output");
}
std::wstring Canonical(const wchar_t* path){
    wchar_t full[1024]{};const DWORD count=GetFullPathNameW(path,1024,full,nullptr);
    Check(count&&count<1024,"canonical input length");return full;
}
std::wstring CasePath(const wchar_t* path){
    const auto result=Canonical(path);wchar_t executable[1024]{};Check(GetModuleFileNameW(nullptr,executable,1024)>0,"own executable path");
    std::wstring work=executable;work.resize(work.find_last_of(L'\\'));work.resize(work.find_last_of(L'\\'));
    Check(work.size()>5&&!_wcsicmp(work.c_str()+work.size()-5,L"\\work"),"fixture build below repository work");
    work+=L'\\';Check(result.size()>work.size()&&!_wcsnicmp(result.c_str(),work.c_str(),work.size()),"owned case below same work root");
    for(size_t i=3;i<=result.size();++i)if(i==result.size()||result[i]==L'\\'){
        const DWORD attributes=GetFileAttributesW(result.substr(0,i).c_str());
        Check(attributes!=INVALID_FILE_ATTRIBUTES&&!(attributes&FILE_ATTRIBUTE_REPARSE_POINT),"no case path reparse ancestor");
    }
    return result;
}
std::array<unsigned char,32> Hex(const std::string& text){
    std::array<unsigned char,32> bytes{};auto digit=[](char c){return c<='9'?c-'0':c-'a'+10;};
    for(size_t i=0;i<32;++i)bytes[i]=static_cast<unsigned char>((digit(text[2*i])<<4)|digit(text[2*i+1]));return bytes;
}
std::vector<unsigned char> Compile(const char* source,const char* target,size_t size,DWORD marker){
    Com<ID3DBlob> code,errors;const HRESULT result=D3DCompile(source,std::strlen(source),"authored-water-fixture",nullptr,nullptr,"main",target,
        D3DCOMPILE_OPTIMIZATION_LEVEL3,0,&code.value,&errors.value);
    if(FAILED(result)&&errors.value)std::fprintf(stderr,"%s\n",static_cast<const char*>(errors->GetBufferPointer()));
    HR(result,"authored shader compilation");const size_t bytes=code->GetBufferSize();
    Check(bytes%4==0&&bytes+8<=size&&size%4==0,"authored SM3 padding budget");
    std::vector<unsigned char> output(size);std::memcpy(output.data(),code->GetBufferPointer(),bytes-4);
    const DWORD comment=static_cast<DWORD>((size-bytes-4)/4)<<16|0xfffe;
    std::memcpy(output.data()+bytes-4,&comment,4);
    for(size_t at=bytes;at<size-4;at+=4)std::memcpy(output.data()+at,&marker,4);
    const DWORD end=0xffff;std::memcpy(output.data()+size-4,&end,4);return output;
}
LRESULT CALLBACK Window(HWND window,UINT message,WPARAM w,LPARAM l){return DefWindowProcW(window,message,w,l);}
struct App {
    HMODULE renderer=nullptr;HANDLE renderer_lease=INVALID_HANDLE_VALUE;HWND window=nullptr;std::wstring root;
    Com<IDirect3D9> d3d;Com<IDirect3DDevice9> device;
    Com<IDirect3DVertexShader9> vertex;Com<IDirect3DPixelShader9> pixel;Com<IDirect3DVertexDeclaration9> declaration;
    std::array<Com<IDirect3DTexture9>,8> textures;Com<IDirect3DCubeTexture9> cube;
    D3DPRESENT_PARAMETERS parameters{};WaterShaderEffects::Config config{};
    ~App(){
        // Even fixture error cleanup keeps the DLL mapped if verified restore
        // cannot be recovered. Process exit reclaims it; never unload live COM.
        if(!WaterShaderEffects::Shutdown())return;
        if(device.value){for(DWORD i=0;i<8;++i)device->SetTexture(i,nullptr);device->SetVertexShader(nullptr);device->SetPixelShader(nullptr);device->SetVertexDeclaration(nullptr);}
        for(auto& texture:textures)texture.clear();cube.clear();declaration.clear();pixel.clear();vertex.clear();device.clear();d3d.clear();
        if(window)DestroyWindow(window);if(renderer)FreeLibrary(renderer);if(renderer_lease!=INVALID_HANDLE_VALUE)CloseHandle(renderer_lease);
    }
};
void Texture(App& app,DWORD slot,const std::array<float,4>& color){
    HR(app.device->CreateTexture(1,1,1,0,D3DFMT_A32B32G32R32F,D3DPOOL_MANAGED,&app.textures[slot].value,nullptr),"authored float texture");
    D3DLOCKED_RECT lock{};HR(app.textures[slot]->LockRect(0,&lock,nullptr,0),"authored texture lock");
    Check(lock.pBits&&lock.Pitch>=16,"authored texture pitch");std::memcpy(lock.pBits,color.data(),16);HR(app.textures[slot]->UnlockRect(0),"authored texture unlock");
}
void Create(App& app){
    const auto dll=app.root+L"\\d3d9.dll";
    app.renderer_lease=CreateFileW(dll.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL|FILE_FLAG_OPEN_REPARSE_POINT,nullptr);
    Check(app.renderer_lease!=INVALID_HANDLE_VALUE,"own DXVK deny-write lease");const auto renderer=Read(dll,32*1024*1024);
    Check(Hash(renderer.data(),renderer.size())==RendererHash,"pinned DXVK3.1.1");
    app.renderer=LoadLibraryExW(dll.c_str(),nullptr,LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR|LOAD_LIBRARY_SEARCH_SYSTEM32);Check(app.renderer!=nullptr,"own DXVK load");
    wchar_t loaded[1024]{};Check(GetModuleFileNameW(app.renderer,loaded,1024)>0&&!_wcsicmp(dll.c_str(),Canonical(loaded).c_str()),"loaded own DXVK path");
    const auto factory=reinterpret_cast<IDirect3D9*(WINAPI*)(UINT)>(GetProcAddress(app.renderer,"Direct3DCreate9"));Check(factory!=nullptr,"Direct3DCreate9 export");
    app.d3d.value=factory(D3D_SDK_VERSION);Check(app.d3d.value!=nullptr,"D3D9 interface");
    WNDCLASSW wc{};wc.lpfnWndProc=Window;wc.hInstance=GetModuleHandleW(nullptr);wc.lpszClassName=L"S7AuthoredWaterGpuFixture";
    Check(RegisterClassW(&wc)!=0,"own class");RECT rectangle{0,0,Width,Height};AdjustWindowRect(&rectangle,WS_OVERLAPPEDWINDOW,FALSE);
    app.window=CreateWindowW(wc.lpszClassName,L"Authored water GPU fixture",WS_OVERLAPPEDWINDOW,CW_USEDEFAULT,CW_USEDEFAULT,
        rectangle.right-rectangle.left,rectangle.bottom-rectangle.top,nullptr,nullptr,wc.hInstance,nullptr);Check(app.window!=nullptr,"own hidden HWND");
    auto& p=app.parameters;p.Windowed=TRUE;p.SwapEffect=D3DSWAPEFFECT_DISCARD;p.hDeviceWindow=app.window;
    p.BackBufferWidth=Width;p.BackBufferHeight=Height;p.BackBufferCount=1;p.BackBufferFormat=D3DFMT_A8R8G8B8;p.PresentationInterval=D3DPRESENT_INTERVAL_IMMEDIATE;
    HR(app.d3d->CreateDevice(D3DADAPTER_DEFAULT,D3DDEVTYPE_HAL,app.window,D3DCREATE_HARDWARE_VERTEXPROCESSING|D3DCREATE_FPU_PRESERVE,&p,&app.device.value),"own hardware device");
    const char* vertex_source="struct O{float4 position:POSITION;float2 uv:TEXCOORD0;};O main(float4 p:TEXCOORD0){O o;o.position=float4(p.x*2-1,1-p.y*2,.5,1);o.uv=p.xy;return o;}";
    const char* original_source="float4 lighting:register(c7);float4 main(float2 uv:TEXCOORD0):COLOR0{float c=saturate(uv.x),d=saturate(uv.y);float f=1-clamp(c*c*1.4,.78,.96);float3 t=lerp(float3(.12,.26,.4),float3(.075,.2,.25),d);float nh=saturate(1-abs(uv.x-.5)*.04);return float4(lerp(t,float3(.8,.7,.4),f)+lighting.rgb*pow(nh,352),1);}";
    const char* candidate_source="float4 lighting:register(c7);float4 main(float2 uv:TEXCOORD0):COLOR0{float c=saturate(uv.x),d=saturate(uv.y);float q=1-c;float f=.02+.83*q*q*q*q*q;float z=d*d*(3-2*d);float3 t=lerp(float3(.12,.26,.4),float3(.075,.2,.25),z);float nh=saturate(1-abs(uv.x-.5)*.04);return float4(lerp(t,float3(.8,.7,.4),f)+lighting.rgb*(128.0/352.0)*pow(nh,128),1);}";
    const auto vs=Compile(vertex_source,"vs_3_0",2592,1),ps=Compile(original_source,"ps_3_0",3740,2),candidate=Compile(candidate_source,"ps_3_0",3840,3);
    const auto vertex_hash=Hex(Hash(vs.data(),vs.size())),pixel_hash=Hex(Hash(ps.data(),ps.size())),candidate_hash=Hex(Hash(candidate.data(),candidate.size()));
    Check(WaterShaderEffects::FixtureIdentities(vertex_hash.data(),pixel_hash.data(),candidate_hash.data()),"authored identities confined to fixture executable");
    HR(app.device->CreateVertexShader(reinterpret_cast<const DWORD*>(vs.data()),&app.vertex.value),"authored baseline VS driver creation");
    HR(app.device->CreatePixelShader(reinterpret_cast<const DWORD*>(ps.data()),&app.pixel.value),"authored baseline PS driver creation");
    const auto genuine=Read(app.root+L"\\genuine-water.ps.bin",3840);Check(genuine.size()==3840&&Hash(genuine.data(),genuine.size())==ProductionHash,"real recipient-generated production PS pin");
    Com<IDirect3DPixelShader9> production;HR(app.device->CreatePixelShader(reinterpret_cast<const DWORD*>(genuine.data()),&production.value),"real production PS driver creation without binding");
    UINT genuine_bytes=0;HR(production->GetFunction(nullptr,&genuine_bytes),"production PS byte count");Check(genuine_bytes==genuine.size(),"production PS preserved size");
    std::vector<unsigned char> returned(genuine_bytes);HR(production->GetFunction(returned.data(),&genuine_bytes),"production PS driver bytecode");Check(Hash(returned.data(),returned.size())==ProductionHash,"driver preserves real production PS");
    const D3DVERTEXELEMENT9 elements[]={{0,0,D3DDECLTYPE_FLOAT4,D3DDECLMETHOD_DEFAULT,D3DDECLUSAGE_TEXCOORD,0},
        {0,16,D3DDECLTYPE_FLOAT4,D3DDECLMETHOD_DEFAULT,D3DDECLUSAGE_TEXCOORD,1},
        {0,32,D3DDECLTYPE_FLOAT1,D3DDECLMETHOD_DEFAULT,D3DDECLUSAGE_TEXCOORD,2},D3DDECL_END()};
    HR(app.device->CreateVertexDeclaration(elements,&app.declaration.value),"authored public VS semantic layout");
    Texture(app,0,{.5f,.5f,1,0});Texture(app,1,{.12f,.26f,.4f,1});Texture(app,4,{.8f,0,0,1});Texture(app,6,{0,0,0,0});Texture(app,7,{.5f,0,0,1});
    HR(app.device->CreateCubeTexture(1,1,0,D3DFMT_A32B32G32R32F,D3DPOOL_MANAGED,&app.cube.value,nullptr),"authored uniform reflection cube");
    const std::array<float,4> reflection{.8f,.7f,.4f,1};
    for(unsigned i=0;i<6;++i){D3DLOCKED_RECT lock{};const auto face=static_cast<D3DCUBEMAP_FACES>(i);HR(app.cube->LockRect(face,0,&lock,nullptr,0),"cube face lock");
        Check(lock.pBits&&lock.Pitch>=16,"cube pitch");std::memcpy(lock.pBits,reflection.data(),16);HR(app.cube->UnlockRect(face,0),"cube face unlock");}
    const auto local=app.root+L"\\LocalData",materials=local+L"\\MaterialShaders",recipe=materials+L"\\river-depth-fresnel-v1-candidate";
    Check(CreateDirectoryW(local.c_str(),nullptr)&&CreateDirectoryW(materials.c_str(),nullptr)&&CreateDirectoryW(recipe.c_str(),nullptr),"fresh authored recipe hierarchy");
    std::wstring name;for(char ch:Hash(candidate.data(),candidate.size()))name+=static_cast<wchar_t>(ch);
    const auto path=recipe+L"\\"+name+L".ps.bin";Write(path,candidate.data(),candidate.size());
    app.config.render_width=Width;app.config.render_height=Height;app.config.enabled=1;
    wcscpy_s(app.config.pixel_path,path.c_str());std::memcpy(app.config.pixel_sha256,candidate_hash.data(),32);
    Check(WaterShaderEffects::Initialize((app.root+L"\\native.private.json").c_str(),Width,Height),"water admitted report context");
}
void Pipeline(App& app,DWORD width,DWORD height,bool highlights){
    HR(app.device->SetVertexDeclaration(app.declaration.value),"vertex declaration bind");
    HR(app.device->SetVertexShader(app.vertex.value),"original vertex bind");HR(app.device->SetPixelShader(app.pixel.value),"original pixel bind");
    HR(app.device->SetRenderState(D3DRS_ZENABLE,FALSE),"no fixture depth test");HR(app.device->SetRenderState(D3DRS_CULLMODE,D3DCULL_NONE),"no fixture culling");
    HR(app.device->SetRenderState(D3DRS_ALPHABLENDENABLE,FALSE),"opaque original pass");HR(app.device->SetRenderState(D3DRS_ALPHATESTENABLE,FALSE),"no alpha test");
    HR(app.device->SetRenderState(D3DRS_SRGBWRITEENABLE,FALSE),"linear original write");D3DVIEWPORT9 viewport{0,0,width,height,0,1};HR(app.device->SetViewport(&viewport),"full target viewport");
    for(DWORD i=0;i<8;++i){HR(app.device->SetTexture(i,app.textures[i].value),"authored sampler bind");
        HR(app.device->SetSamplerState(i,D3DSAMP_MINFILTER,D3DTEXF_POINT),"point min");HR(app.device->SetSamplerState(i,D3DSAMP_MAGFILTER,D3DTEXF_POINT),"point mag");
        HR(app.device->SetSamplerState(i,D3DSAMP_MIPFILTER,D3DTEXF_NONE),"no mip");HR(app.device->SetSamplerState(i,D3DSAMP_SRGBTEXTURE,0),"linear texture read");}
    HR(app.device->SetTexture(5,app.cube.value),"original cube sampler bind");
    std::array<std::array<float,4>,111> vs{};vs[12]={.02f,0,0,0};vs[13]={0,-.02f,0,0};vs[15]={-1,1,.2f,1};
    vs[16]={50,-200,80,0};vs[17]={0,0,0,1};vs[24]={0,.93f,.367f,0};vs[66]={0,0,100,100};
    vs[68]={.5f,.5f,.5f,.5f};vs[69]={1,1,1,1};vs[86]={0,0,1,0};vs[88][3]=1;vs[90][3]=1;vs[94]={-1,-1,0,0};
    for(size_t i=101;i<111;++i)vs[i]={.15f,.4f,.5f,1};
    HR(app.device->SetVertexShaderConstantF(0,vs[0].data(),static_cast<UINT>(vs.size())),"authored VS constants");
    std::array<std::array<float,4>,224> ps{};ps[0]={0,0,0,1000};ps[1]={static_cast<float>(width),static_cast<float>(height),1.0f/width,1.0f/height};
    ps[5]={0,.93f,.367f,0};ps[7]=highlights?std::array<float,4>{.5f,.5f,.5f,0}:std::array<float,4>{0,0,0,0};
    ps[68]={1.0f/width,1.0f/height,1.0f/width,1.0f/height};ps[75]={100,100,0,0};
    HR(app.device->SetPixelShaderConstantF(0,ps[0].data(),static_cast<UINT>(ps.size())),"authored PS constants");
}
struct Frame {DWORD width=0,height=0;std::vector<DWORD> pixels;WaterShaderEffects::Stats stats{};};
Frame Capture(App& app,const wchar_t* name){
    Com<IDirect3DSurface9> target,cpu;D3DSURFACE_DESC desc{};HR(app.device->GetBackBuffer(0,0,D3DBACKBUFFER_TYPE_MONO,&target.value),"actual backbuffer");HR(target->GetDesc(&desc),"actual target dimensions");
    HR(app.device->CreateOffscreenPlainSurface(desc.Width,desc.Height,desc.Format,D3DPOOL_SYSTEMMEM,&cpu.value,nullptr),"owned readback");
    HR(app.device->GetRenderTargetData(target.value,cpu.value),"actual GPU pixel readback");D3DLOCKED_RECT lock{};HR(cpu->LockRect(&lock,nullptr,D3DLOCK_READONLY),"readback lock");
    Check(lock.pBits&&lock.Pitch>=static_cast<INT>(desc.Width*4),"bounded readback pitch");Frame result;result.width=desc.Width;result.height=desc.Height;result.pixels.resize(static_cast<size_t>(desc.Width)*desc.Height);
    for(DWORD y=0;y<desc.Height;++y)std::memcpy(result.pixels.data()+static_cast<size_t>(y)*desc.Width,static_cast<const unsigned char*>(lock.pBits)+static_cast<size_t>(y)*lock.Pitch,desc.Width*4);
    HR(cpu->UnlockRect(),"readback unlock");Check(WaterShaderEffects::ReadStats(&result.stats),"GPU module scalar stats");
    if(name){BITMAPFILEHEADER file{};BITMAPINFOHEADER info{};file.bfType=0x4d42;file.bfOffBits=sizeof(file)+sizeof(info);file.bfSize=file.bfOffBits+static_cast<DWORD>(result.pixels.size()*4);
        info.biSize=sizeof(info);info.biWidth=static_cast<LONG>(desc.Width);info.biHeight=-static_cast<LONG>(desc.Height);info.biPlanes=1;info.biBitCount=32;info.biCompression=BI_RGB;
        std::vector<unsigned char> bmp(file.bfSize);std::memcpy(bmp.data(),&file,sizeof(file));std::memcpy(bmp.data()+sizeof(file),&info,sizeof(info));std::memcpy(bmp.data()+file.bfOffBits,result.pixels.data(),result.pixels.size()*4);
        Write(app.root+L"\\"+name,bmp.data(),bmp.size());}
    return result;
}
Frame Draw(App& app,bool candidate,bool fail,DWORD width,DWORD height,bool highlights,const wchar_t* name){
    Pipeline(app,width,height,highlights);HR(app.device->Clear(0,nullptr,D3DCLEAR_TARGET,0xff202020,1,0),"fixture clear");HR(app.device->BeginScene(),"fixture scene begin");
    struct Vertex {float position[4],water[4],height;};
    const Vertex vertices[]={{{0,0,0,1},{.51f,.51f,0,1.0f/255},0},{{1,0,0,1},{.51f,.51f,0,1.0f/255},0},
                             {{0,1,0,1},{.51f,.51f,0,1.0f/255},0},{{1,1,0,1},{.51f,.51f,0,1.0f/255},0}};
    WaterShaderEffects::DrawTransaction transaction{};WaterShaderEffects::BeginDraw(app.device.value,&transaction);
    Check(!transaction.unsafe_state&&transaction.active==candidate,"expected real shader activation");
    // Microsoft documents stride0 as invalid. This is a real driver HRESULT,
    // not a fabricated result supplied to the restoration routine.
    const HRESULT result=app.device->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP,2,vertices,fail?0:sizeof(Vertex));
    WaterShaderEffects::EndDraw(&transaction,result);Check(!transaction.unsafe_state,"actual GPU original shader restored");
    Com<IDirect3DPixelShader9> original;HR(app.device->GetPixelShader(&original.value),"read back restored PS binding");Check(original.value==app.pixel.value,"exact original PS pointer restored");
    if(fail)Check(FAILED(result),"actual invalid-stride draw failed");else HR(result,"authored geometry draw");
    HR(app.device->EndScene(),"fixture scene end");WaterShaderEffects::EndFrame(app.device.value);auto frame=Capture(app,name);
    HR(app.device->Present(nullptr,nullptr,nullptr,nullptr),"own fixture Present");return frame;
}
std::uint64_t Difference(const Frame& a,const Frame& b){Check(a.width==b.width&&a.height==b.height,"matching pixel dimensions");std::uint64_t sum=0;
    for(size_t i=0;i<a.pixels.size();++i)for(unsigned shift=0;shift<24;shift+=8){const int x=static_cast<int>((a.pixels[i]>>shift)&255),y=static_cast<int>((b.pixels[i]>>shift)&255);sum+=static_cast<unsigned>(x>y?x-y:y-x);}return sum;
}
void Reset(App& app,DWORD width,DWORD height){Check(WaterShaderEffects::Reset(app.device.value),"water child refs released before actual Reset");
    app.parameters.BackBufferWidth=width;app.parameters.BackBufferHeight=height;HR(app.device->Reset(&app.parameters),"actual device Reset");WaterShaderEffects::InvalidateFrame();}
}

int wmain(int argc,wchar_t** argv){try{
    Check(argc==2,"one owned case directory, no PID/export selector");App app;app.root=CasePath(argv[1]);Create(app);
    const auto off=Draw(app,false,false,Width,Height,false,L"off.bmp");
    Check(WaterShaderEffects::Configure(&app.config),"explicit generated PS enable");
    const auto on=Draw(app,true,false,Width,Height,false,L"on.bmp");
    Check(on.stats.completed_applications==1&&on.stats.completed_restorations==1,"real successful candidate draw/restoration frame proof");
    Check(Difference(off,on)>static_cast<std::uint64_t>(Width)*Height,"nontrivial GPU optics response");
    for(unsigned i=0;i<4;++i)Draw(app,true,false,Width,Height,false,nullptr);
    WaterShaderEffects::Stats warm{};Check(WaterShaderEffects::ReadStats(&warm)&&warm.shader_creations==1,"one GPU shader object across warm frames");
    const auto failed=Draw(app,true,true,Width,Height,false,nullptr);Check(failed.stats.failed_draws==1&&!failed.stats.completed_applications,"failed GPU draw does not qualify a visible frame");
    Reset(app,960,Height);const auto resized=Draw(app,false,false,960,Height,false,nullptr);Check(resized.width==960&&!resized.stats.completed_applications,"resized target refuses stale render config");
    Reset(app,Width,Height);const auto reset=Draw(app,true,false,Width,Height,false,L"after-reset.bmp");Check(Difference(on,reset)==0,"identical authored GPU response after actual Reset");
    WaterShaderEffects::Config disabled{};disabled.render_width=Width;disabled.render_height=Height;Check(WaterShaderEffects::Configure(&disabled),"explicit original control restore");
    const auto again=Draw(app,false,false,Width,Height,false,L"off-again.bmp");Check(Difference(off,again)==0,"off/on/off actual GPU rollback exact");
    const auto off_glint=Draw(app,false,false,Width,Height,true,L"off-specular.bmp");Check(WaterShaderEffects::Configure(&app.config),"specular candidate enable");
    const auto on_glint=Draw(app,true,false,Width,Height,true,L"on-specular.bmp");Check(Difference(off_glint,on_glint)>0,"real specular/optics GPU response");
    WaterShaderEffects::Stats final{};Check(WaterShaderEffects::ReadStats(&final)&&!final.faulted&&!final.unsafe_state&&!final.set_failures&&!final.restore_failures&&!final.shader_failures,"clean final GPU module state");
    char json[2048]{};std::snprintf(json,sizeof(json),"{\"format\":\"s7-authored-water-gpu-v1\",\"passed\":true,\"scene\":\"authored_geometry_shaders_textures_constants\",\"original_s7_programs_rendered\":false,\"production_ps_driver_created\":true,\"production_ps_rendered\":false,\"identity_policy\":\"fixture_only_override\",\"render\":[%lu,%lu],\"resized_width\":%lu,\"off_on_rgb_absolute_difference\":%llu,\"off_repeat_rgb_absolute_difference\":0,\"shader_creations\":%llu,\"successful_draws\":%llu,\"failed_draws\":%llu,\"restorations\":%llu,\"resets\":%llu,\"game_launched\":false,\"visible_game_quality_qualified\":false,\"readback_excluded_from_pacing\":true}\n",Width,Height,resized.width,Difference(off,on),final.shader_creations,final.successful_draws,final.failed_draws,final.restorations,final.resets);
    Write(app.root+L"\\receipt.private.json",json,std::strlen(json));std::printf("%s",json);return 0;
}catch(const std::exception& error){std::fprintf(stderr,"Water GPU fixture failed: %s\n",error.what());return 1;}}
