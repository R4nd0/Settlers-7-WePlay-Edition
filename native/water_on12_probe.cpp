// Exact River input capture; explicit flag 1 enables two finite POW caps only
// on the proven On12 device. Original draw once, original VS restored afterward.
#include "water_on12_identity.h"
#include "water_on12_pow_cap.h"
#include <d3d9.h>
#include <d3d12.h>
#include <d3d9on12.h>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <sstream>
#include <vector>

namespace {
using namespace WaterOn12Identity;
constexpr char GetterName[]="?GetD3DDevice@VVideo@@SAPAUIDirect3DDevice9@@XZ";
constexpr char Vertex[]="f190d66ee1ae43637341a76e5b644cc63bf3c2a5b5c94ea9286764e13f1c16c0";
constexpr char Pixel[]="dd65ba0b9d79c7c4d9ad9904404388230556b194eedb8cbdfbd4fa99f44eee5b";
struct Config {DWORD size,version,flags;wchar_t report[512];};
static_assert(sizeof(Config)==1036,"Water observer ABI changed");
using Getter=IDirect3DDevice9* (__cdecl*)();
using DrawIndexed=HRESULT (STDMETHODCALLTYPE*)(IDirect3DDevice9*,D3DPRIMITIVETYPE,INT,UINT,UINT,UINT,UINT);
using Present=HRESULT (STDMETHODCALLTYPE*)(IDirect3DDevice9*,const RECT*,const RECT*,HWND,const RGNDATA*);
template<class T> struct Ref {T* value=nullptr;~Ref(){if(value)value->Release();}};
struct Slot {void** address;void* original;void* replacement;};
Getter original_getter=nullptr;DrawIndexed original_draw=nullptr;Present original_present=nullptr;
HMODULE wrapper=nullptr,apphelp=nullptr;IDirect3DDevice9* admitted=nullptr;DWORD owner=0;
thread_local bool internal=false;
struct Scope {bool previous=internal;Scope(){internal=true;}~Scope(){internal=previous;}};
Config config{};std::wstring directory;bool snapshot=false;DWORD failure=0;
bool failure_reported=false;
std::uint64_t draws=0,matches=0,failed_draws=0,presents=0;HRESULT on12_hr=E_NOINTERFACE;
// One device-owned program for this pinned process lifetime. Do not Release a
// COM object from a CRT global destructor after renderer DLL teardown.
IDirect3DVertexShader9* corrected=nullptr;
std::uint64_t caps_applied=0,caps_restored=0;

bool Shape(const Config& c) {
    const size_t n=wcsnlen_s(c.report,512);constexpr wchar_t suffix[]=L"\\water-probe.private.json";
    if(c.size!=sizeof(c)||c.version!=1||c.flags>1||n>=512||n<wcslen(suffix)+3||c.report[1]!=L':'||c.report[2]!=L'\\'
        ||_wcsicmp(c.report+n-wcslen(suffix),suffix))return false;
    for(size_t i=3;i<n;++i)if(c.report[i]<32||c.report[i]==L':'||c.report[i]==L'/')return false;
    wchar_t full[512]{};const DWORD count=GetFullPathNameW(c.report,512,full,nullptr);
    return count&&count<512&&!_wcsicmp(c.report,full);
}
bool WriteNew(const wchar_t* path,const void* data,DWORD bytes) {
    HANDLE file=CreateFileW(path,GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);
    if(file==INVALID_HANDLE_VALUE)return false;
    DWORD written=0;const bool okay=WriteFile(file,data,bytes,&written,nullptr)&&written==bytes&&FlushFileBuffers(file);
    CloseHandle(file);return okay;
}
bool Artifact(const wchar_t* name,const void* bytes,DWORD count,std::string& hash) {
    return Hash(bytes,count,hash)&&WriteNew((directory+L"\\"+name).c_str(),bytes,count);
}
void Live() {
    std::ostringstream text;text<<"{\"format\":\"s7-on12-water-live-v1\",\"attached\":"<<(admitted?"true":"false")
        <<",\"draws\":"<<draws<<",\"exact_pair_matches\":"<<matches<<",\"failed_original_draws\":"<<failed_draws
        <<",\"presents\":"<<presents<<",\"snapshot_done\":"<<(snapshot?"true":"false")
        <<",\"failure\":"<<failure<<",\"on12_query_hr\":"<<static_cast<long>(on12_hr)
        <<",\"pow_cap_requested\":"<<(config.flags?"true":"false")<<",\"caps_applied\":"<<caps_applied<<",\"caps_restored\":"<<caps_restored<<"}";
    const auto raw=text.str();const auto temporary=directory+L"\\water-probe-live.private.tmp";
    HANDLE file=CreateFileW(temporary.c_str(),GENERIC_WRITE,0,nullptr,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);
    if(file!=INVALID_HANDLE_VALUE){DWORD count=0;const bool okay=WriteFile(file,raw.data(),static_cast<DWORD>(raw.size()),&count,nullptr)&&count==raw.size();CloseHandle(file);
        if(okay)MoveFileExW(temporary.c_str(),(directory+L"\\water-probe-live.private.json").c_str(),MOVEFILE_REPLACE_EXISTING);}
}
bool Patch(const Slot& s,bool forward) {
    DWORD previous=0,unused=0;if(!VirtualProtect(s.address,sizeof(void*),PAGE_READWRITE,&previous))return false;
    void* before=forward?s.original:s.replacement;void* after=forward?s.replacement:s.original;
    const bool changed=InterlockedCompareExchangePointer(reinterpret_cast<void* volatile*>(s.address),after,before)==before;
    const bool restored=VirtualProtect(s.address,sizeof(void*),previous,&unused)!=FALSE;
    if(changed&&!restored){InterlockedCompareExchangePointer(reinterpret_cast<void* volatile*>(s.address),before,after);VirtualProtect(s.address,sizeof(void*),previous,&unused);}
    return changed&&restored;
}
bool Import(HMODULE module,std::vector<Slot>& slots) {
    auto* image=reinterpret_cast<unsigned char*>(module);const auto* dos=reinterpret_cast<IMAGE_DOS_HEADER*>(image);
    if(!Range(dos,sizeof(*dos))||dos->e_magic!=IMAGE_DOS_SIGNATURE||dos->e_lfanew<64||dos->e_lfanew>4096)return false;
    const auto* nt=reinterpret_cast<IMAGE_NT_HEADERS32*>(image+dos->e_lfanew);
    if(!Range(nt,sizeof(*nt))||nt->Signature!=IMAGE_NT_SIGNATURE||nt->OptionalHeader.Magic!=IMAGE_NT_OPTIONAL_HDR32_MAGIC)return false;
    const DWORD size=nt->OptionalHeader.SizeOfImage;
    auto valid=[&](DWORD rva,size_t bytes){return rva<size&&bytes<=size-rva&&Range(image+rva,bytes);};
    const auto d=nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];if(!valid(d.VirtualAddress,d.Size))return false;
    auto* entries=reinterpret_cast<IMAGE_IMPORT_DESCRIPTOR*>(image+d.VirtualAddress);
    for(size_t n=0;n<d.Size/sizeof(*entries)&&entries[n].Name;++n) {
        const auto& e=entries[n];if(!valid(e.Name,12))return false;
        if(_stricmp(reinterpret_cast<char*>(image+e.Name),"vbase90.dll"))continue;
        if(!e.OriginalFirstThunk)return false;
        for(DWORD i=0;;++i) {
            const DWORD offset=i*sizeof(IMAGE_THUNK_DATA32);
            if(!valid(e.OriginalFirstThunk+offset,4)||!valid(e.FirstThunk+offset,4))return false;
            const DWORD thunk=reinterpret_cast<IMAGE_THUNK_DATA32*>(image+e.OriginalFirstThunk+offset)->u1.AddressOfData;
            if(!thunk)break;if(IMAGE_SNAP_BY_ORDINAL32(thunk))continue;
            if(!valid(thunk,sizeof(WORD)+sizeof(GetterName)))return false;
            const auto* name=reinterpret_cast<IMAGE_IMPORT_BY_NAME*>(image+thunk);
            if(std::memcmp(name->Name,GetterName,sizeof(GetterName)))continue;
            auto** slot=reinterpret_cast<void**>(image+e.FirstThunk+offset);
            if(*slot!=reinterpret_cast<void*>(original_getter))return false;
            slots.push_back({slot,*slot,nullptr});
        }
    }
    return true;
}

std::string Texture(IDirect3DDevice9* device,DWORD sampler,DWORD& budget) {
    std::ostringstream out;out<<"{\"sampler\":"<<sampler;Ref<IDirect3DBaseTexture9> base;
    const HRESULT got=device->GetTexture(sampler,&base.value);out<<",\"get_hr\":"<<static_cast<long>(got);
    if(FAILED(got)||!base.value){out<<",\"bound\":false}";return out.str();}
    const auto type=base.value->GetType();out<<",\"bound\":true,\"type\":"<<type;
    if(type==D3DRTYPE_TEXTURE) {
        auto* texture=static_cast<IDirect3DTexture9*>(base.value);D3DSURFACE_DESC desc{};
        if(SUCCEEDED(texture->GetLevelDesc(0,&desc))) {
            out<<",\"width\":"<<desc.Width<<",\"height\":"<<desc.Height<<",\"format\":"<<desc.Format
                <<",\"usage\":"<<desc.Usage<<",\"pool\":"<<desc.Pool;
            const DWORD bpp=desc.Format==D3DFMT_A32B32G32R32F?16:desc.Format==D3DFMT_A16B16G16R16F?8:
                (desc.Format==D3DFMT_A8R8G8B8||desc.Format==D3DFMT_X8R8G8B8||desc.Format==D3DFMT_R32F)?4:0;
            if(bpp&&desc.Width&&desc.Height&&desc.Width<=4096&&desc.Height<=4096
                &&static_cast<std::uint64_t>(desc.Width)*desc.Height*bpp<=budget) {
                Ref<IDirect3DSurface9> source,staging;D3DLOCKED_RECT locked{};
                HRESULT copied=texture->GetSurfaceLevel(0,&source.value);
                if(SUCCEEDED(copied))copied=device->CreateOffscreenPlainSurface(desc.Width,desc.Height,desc.Format,D3DPOOL_SYSTEMMEM,&staging.value,nullptr);
                if(SUCCEEDED(copied))copied=device->GetRenderTargetData(source.value,staging.value);
                bool locked_staging=false;HRESULT lock=copied;
                if(SUCCEEDED(copied)){lock=staging.value->LockRect(&locked,nullptr,D3DLOCK_READONLY);locked_staging=SUCCEEDED(lock);}
                if(FAILED(lock))lock=texture->LockRect(0,&locked,nullptr,D3DLOCK_READONLY);
                if(SUCCEEDED(lock)) {
                    const DWORD row=desc.Width*bpp,bytes=row*desc.Height;
                    if(locked.Pitch>=static_cast<INT>(row)&&locked.pBits) {
                        std::vector<unsigned char> raw(bytes);
                        for(DWORD y=0;y<desc.Height;++y)std::memcpy(raw.data()+static_cast<size_t>(y)*row,static_cast<unsigned char*>(locked.pBits)+static_cast<size_t>(y)*locked.Pitch,row);
                        std::string hash;const auto name=L"water-sampler-"+std::to_wstring(sampler)+L".private.bin";
                        if(Artifact(name.c_str(),raw.data(),bytes,hash)){out<<",\"captured_bytes\":"<<bytes<<",\"sha256\":\""<<hash<<"\"";budget-=bytes;}
                    }
                    if(locked_staging)staging.value->UnlockRect();else texture->UnlockRect(0);
                }
                out<<",\"readback_hr\":"<<static_cast<long>(copied)<<",\"lock_hr\":"<<static_cast<long>(lock);
            }
        }
    }
    out<<",\"sampler_states\":[";
    for(DWORD state=1;state<=13;++state){DWORD value=0;const HRESULT hr=device->GetSamplerState(sampler,static_cast<D3DSAMPLERSTATETYPE>(state),&value);if(state>1)out<<",";out<<"["<<static_cast<long>(hr)<<","<<value<<"]";}
    out<<"]}";return out.str();
}

void Snapshot(IDirect3DDevice9* device,D3DPRIMITIVETYPE primitive,INT base,UINT minimum,UINT vertices,UINT start,UINT count) {
    if(snapshot||failure||!count)return;Ref<IDirect3DPixelShader9> ps;
    UINT pixel_bytes=0;if(FAILED(device->GetPixelShader(&ps.value))||!ps.value||FAILED(ps.value->GetFunction(nullptr,&pixel_bytes))||pixel_bytes!=3740)return;
    std::array<unsigned char,3740> pixel{};std::string hash;
    if(FAILED(ps.value->GetFunction(pixel.data(),&pixel_bytes))||pixel_bytes!=pixel.size()||!Hash(pixel.data(),pixel_bytes,hash)||hash!=Pixel)return;
    Ref<IDirect3DVertexShader9> vs;UINT vertex_bytes=0;
    if(FAILED(device->GetVertexShader(&vs.value))||!vs.value||FAILED(vs.value->GetFunction(nullptr,&vertex_bytes))||vertex_bytes!=2592)return;
    std::array<unsigned char,2592> vertex{};
    if(FAILED(vs.value->GetFunction(vertex.data(),&vertex_bytes))||vertex_bytes!=vertex.size()||!Hash(vertex.data(),vertex_bytes,hash)||hash!=Vertex)return;
    ++matches;
    struct Constants {DWORD size,version,vs_count,ps_count;float vs[256*4],ps[224*4];} constants{};
    constants.size=sizeof(constants);constants.version=1;constants.vs_count=256;constants.ps_count=224;
    const HRESULT vs_hr=device->GetVertexShaderConstantF(0,constants.vs,256),ps_hr=device->GetPixelShaderConstantF(0,constants.ps,224);
    if(FAILED(vs_hr)||FAILED(ps_hr)){failure=301;return;}
    std::string constants_hash,vs_hash,ps_hash;
    if(!Artifact(L"water-constants.private.bin",&constants,sizeof(constants),constants_hash)
        ||!Artifact(L"water-vertex.private.bin",vertex.data(),vertex_bytes,vs_hash)
        ||!Artifact(L"water-pixel.private.bin",pixel.data(),pixel_bytes,ps_hash)){failure=302;return;}
    unsigned vs_bad=0,ps_bad=0;for(float value:constants.vs)if(!std::isfinite(value))++vs_bad;for(float value:constants.ps)if(!std::isfinite(value))++ps_bad;
    std::ostringstream out;out<<"{\"format\":\"s7-on12-water-inputs-v1\",\"input_capture_uses_only_getters\":true,\"pow_cap_requested\":"<<(config.flags?"true":"false")<<",\"draw_replayed\":false,\"draw_skipped\":false,"
        <<"\"vertex_sha256\":\""<<vs_hash<<"\",\"pixel_sha256\":\""<<ps_hash<<"\",\"constants_sha256\":\""<<constants_hash
        <<"\",\"vs_nonfinite\":"<<vs_bad<<",\"ps_nonfinite\":"<<ps_bad<<",\"draw\":["<<primitive<<","<<base<<","<<minimum<<","<<vertices<<","<<start<<","<<count<<"],\"samplers\":[";
    DWORD budget=48*1024*1024;const DWORD samplers[]{0,1,4,5,6,7};
    for(size_t i=0;i<6;++i){if(i)out<<",";out<<Texture(device,samplers[i],budget);}
    out<<"]}";const auto raw=out.str();
    if(!WriteNew(config.report,raw.data(),static_cast<DWORD>(raw.size()))){failure=303;return;}
    snapshot=true;
}

HRESULT DrawWithCap(IDirect3DDevice9* d,D3DPRIMITIVETYPE p,INT b,UINT m,UINT v,UINT s,UINT c) {
    Ref<IDirect3DPixelShader9> ps;Ref<IDirect3DVertexShader9> vs;
    bool target=config.flags==1&&on12_hr==S_OK&&!failure;
    try {if(target) {
        UINT size=0;std::array<unsigned char,3740> pixel{};std::string hash;
        target=SUCCEEDED(d->GetPixelShader(&ps.value))&&ps.value&&SUCCEEDED(ps.value->GetFunction(nullptr,&size))&&size==pixel.size();
        if(target)target=SUCCEEDED(ps.value->GetFunction(pixel.data(),&size))&&size==pixel.size()&&Hash(pixel.data(),size,hash)&&hash==Pixel;
        if(target) {
            std::vector<std::uint32_t> source(648);size=0;
            target=SUCCEEDED(d->GetVertexShader(&vs.value))&&vs.value&&SUCCEEDED(vs.value->GetFunction(nullptr,&size))&&size==2592;
            if(target)target=SUCCEEDED(vs.value->GetFunction(source.data(),&size))&&size==2592&&Hash(source.data(),size,hash)&&hash==Vertex;
            if(target) {
                std::array<float,32> scattering{};
                if(FAILED(d->GetVertexShaderConstantF(87,scattering.data(),8)))target=false;
                for(float value:scattering)if(value!=0.f)target=false;
            }
            if(target&&!corrected) {
                std::vector<std::uint32_t> program;
                if(!WaterOn12PowCap::Generate(source,program)||FAILED(d->CreateVertexShader(reinterpret_cast<const DWORD*>(program.data()),&corrected))){failure=305;target=false;}
                else {std::string generated;Artifact(L"water-capped-vertex.private.bin",program.data(),static_cast<DWORD>(program.size()*4),generated);}
            }
        }
    }}catch(...){failure=308;target=false;}
    bool bound=false;
    if(target&&corrected){bound=SUCCEEDED(d->SetVertexShader(corrected));if(!bound)failure=306;else ++caps_applied;}
    const HRESULT hr=original_draw(d,p,b,m,v,s,c);
    if(bound){if(SUCCEEDED(d->SetVertexShader(vs.value)))++caps_restored;else failure=307;}
    return hr;
}
HRESULT STDMETHODCALLTYPE HookDraw(IDirect3DDevice9* d,D3DPRIMITIVETYPE p,INT b,UINT m,UINT v,UINT s,UINT c) {
    if(!internal&&d==admitted&&GetCurrentThreadId()==owner) {
        Guard guard;Scope scope;++draws;
        try{Snapshot(d,p,b,m,v,s,c);}catch(...){failure=304;}
        // Allocation/hash exceptions are handled before any temporary binding.
        const HRESULT hr=DrawWithCap(d,p,b,m,v,s,c);
        if(FAILED(hr))++failed_draws;return hr;
    }
    return original_draw(d,p,b,m,v,s,c);
}
HRESULT STDMETHODCALLTYPE HookPresent(IDirect3DDevice9* d,const RECT* a,const RECT* b,HWND w,const RGNDATA* r) {
    if(!internal&&d==admitted&&GetCurrentThreadId()==owner){Guard guard;Scope scope;++presents;if(presents==1||presents%60==0)Live();}
    return original_present(d,a,b,w,r);
}
void Attach(IDirect3DDevice9* device) {
    if(!device||admitted||failure)return;
    if(!Range(device,sizeof(void*)))return;auto** table=*reinterpret_cast<void***>(device);
    if(!Range(table,119*sizeof(void*))||!Belongs(table,wrapper)||!Belongs(table[82],wrapper)||!Belongs(table[17],wrapper)){failure=201;return;}
    for(size_t slot=0;slot<119;++slot) {
        MEMORY_BASIC_INFORMATION page{};
        if(!table[slot]||VirtualQuery(table[slot],&page,sizeof(page))!=sizeof(page)||page.State!=MEM_COMMIT||page.Type!=MEM_IMAGE
            ||(page.Protect&(PAGE_NOACCESS|PAGE_GUARD))
            ||!(page.Protect&(PAGE_EXECUTE|PAGE_EXECUTE_READ|PAGE_EXECUTE_READWRITE|PAGE_EXECUTE_WRITECOPY))
            ||(page.AllocationBase!=wrapper&&!(apphelp&&page.AllocationBase==apphelp&&(slot==0||slot==2||slot==13||slot==16)))){failure=205;return;}
    }
    Slot slots[]={{&table[82],table[82],reinterpret_cast<void*>(HookDraw)},{&table[17],table[17],reinterpret_cast<void*>(HookPresent)}};
    original_draw=reinterpret_cast<DrawIndexed>(table[82]);original_present=reinterpret_cast<Present>(table[17]);
    if(!Patch(slots[0],true)){failure=202;return;}
    if(!Patch(slots[1],true)){Patch(slots[0],false);failure=203;return;}
    admitted=device;owner=GetCurrentThreadId();Ref<IDirect3DDevice9On12> on12;
    on12_hr=device->QueryInterface(__uuidof(IDirect3DDevice9On12),reinterpret_cast<void**>(&on12.value));
    if(config.flags&&on12_hr!=S_OK)failure=206;Live();
}
IDirect3DDevice9* __cdecl HookGetter() {
    IDirect3DDevice9* device=original_getter();
    if(!internal){Guard guard;Scope scope;try{Attach(device);if(failure&&!failure_reported){Live();failure_reported=true;}}catch(...){failure=204;}}
    return device;
}
}

extern "C" DWORD WINAPI WePlayWaterOn12ProbeInitialize(void* opaque) {
    using namespace WaterOn12Identity;Guard guard;
    try {Config c{};SIZE_T read=0;
    if(!opaque||!ReadProcessMemory(GetCurrentProcess(),opaque,&c,sizeof(c),&read)||read!=sizeof(c)||!Shape(c))return 0;
    if(original_getter||!Module(GetModuleHandleW(nullptr),Game)||!Module(GetModuleHandleW(L"vbase90.dll"),Base)
        ||!Module(GetModuleHandleW(L"vision90.dll"),Vision)||!Parents(c.report)||GetFileAttributesW(c.report)!=INVALID_FILE_ATTRIBUTES)return 101;
    wchar_t image[512]{};if(!GetModuleFileNameW(nullptr,image,512))return 102;
    const auto at=std::wstring(image).find_last_of(L'\\');if(at==std::wstring::npos)return 102;
    const auto bin=std::wstring(image).substr(0,at);wrapper=GetModuleHandleW((bin+L"\\dxwrapper.dll").c_str());
    if(!Module(GetModuleHandleW((bin+L"\\d3d9.dll").c_str()),Stub)||!Module(wrapper,Wrapper))return 103;
    apphelp=GetModuleHandleW(L"apphelp.dll");
    if(apphelp&&!Module(apphelp,"34655b97cbcaa5b93733da1d9118e0663735c442eb0016671a98389f3cde863e"))return 109;
    config=c;directory=std::wstring(c.report).substr(0,std::wstring(c.report).find_last_of(L'\\'));
    original_getter=reinterpret_cast<Getter>(GetProcAddress(GetModuleHandleW(L"vbase90.dll"),GetterName));if(!original_getter)return 104;
    std::vector<Slot> slots;
    if(!Import(GetModuleHandleW(nullptr),slots)||!Import(GetModuleHandleW(L"vision90.dll"),slots)||slots.empty())return 105;
    HMODULE self=nullptr;if(!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_PIN,reinterpret_cast<const wchar_t*>(WePlayWaterOn12ProbeInitialize),&self))return 106;
    size_t applied=0;for(auto& slot:slots){slot.replacement=reinterpret_cast<void*>(HookGetter);if(!Patch(slot,true)){for(size_t i=applied;i>0;--i)Patch(slots[i-1],false);return 107;}++applied;}
    Live();return 1;
    }catch(...){return 108;}
}

#ifdef S7_WATER_ON12_PROBE_FIXTURE
int main(){Config c{};c.size=sizeof(c);c.version=1;wcscpy_s(c.report,L"C:\\work\\case\\water-probe.private.json");unsigned checks=0;
    auto check=[&](bool okay){++checks;if(!okay)std::exit(1);};check(Shape(c));c.flags=1;check(Shape(c));c.flags=2;check(!Shape(c));c.flags=0;c.version=2;check(!Shape(c));c.version=1;
    c.size--;check(!Shape(c));c.size++;wcscpy_s(c.report,L"C:\\work\\case\\..\\case\\water-probe.private.json");check(!Shape(c));
    std::string hash;check(Hash("abc",3,hash)&&hash=="ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    SYSTEM_INFO system{};GetSystemInfo(&system);const SIZE_T page=system.dwPageSize;
    auto* memory=static_cast<unsigned char*>(VirtualAlloc(nullptr,page*2,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE));check(memory!=nullptr);
    DWORD previous=0;check(VirtualProtect(memory+page,page,PAGE_READONLY,&previous)&&Range(memory+page-4,16));
    check(VirtualProtect(memory+page,page,PAGE_NOACCESS,&previous)&&!Range(memory+page-4,16));
    check(!Range(reinterpret_cast<const void*>(0xfffffff0),32));VirtualFree(memory,0,MEM_RELEASE);
    check(!WePlayWaterOn12ProbeInitialize(nullptr));check(!admitted);
    std::vector<std::uint32_t> empty(648),generated;check(!WaterOn12PowCap::Generate(empty,generated));
    empty.front()=0xfffe0300;empty.back()=0xffff;
    const std::uint32_t first[]={0x03000020,0x80080002,0x80ff0000,0xa0ff0077},second[]={0x03000020,0x80010005,0x80aa0000,0xa0ff0077};
    std::memcpy(empty.data()+469,first,sizeof(first));std::memcpy(empty.data()+486,second,sizeof(second));
    const std::uint32_t products[]={0x03000005,0x80040000,0x80ff0000,0x80000005,0x03000005,0x80080000,0x80ff0002,0x80ff0000};
    std::memcpy(empty.data()+490,products,sizeof(products));
    check(WaterOn12PowCap::Generate(empty,generated)&&generated.size()==662);
    check(generated[240]==0x05000051&&generated[242]==0x7f7fffff&&generated[504]==0x0300000a&&generated[508]==0x0300000a);
    empty[469]^=1;check(!WaterOn12PowCap::Generate(empty,generated));
    std::printf("{\"cpu_checks\":%u,\"gpu_accessed\":false}\n",checks);return 0;}
#endif
