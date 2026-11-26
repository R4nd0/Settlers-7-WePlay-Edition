// Authored fake COM + memory-only GDI atlas. No game, window, D3D runtime or GPU.
#include "shader_debug_overlay.h"
#include <array>
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <vector>
namespace {
unsigned checks=0;
void Check(bool okay,const char* why){++checks;if(!okay)throw std::runtime_error(why);}
struct Surface {void** table;ULONG refs=1;DWORD width=3840,height=2160;};
struct Texture {void** table;ULONG refs=0;std::vector<DWORD> pixels=std::vector<DWORD>(256*128);};
struct Device;
struct Block {void** table;ULONG refs=0;Device* device=nullptr;DWORD tag=0,frequency=0,fill=0;};
struct Device {
    void** table;std::array<Surface*,4> targets{};Surface* depth=nullptr;
    D3DVIEWPORT9 viewport{3,4,3840,2160,.2f,.9f};DWORD tag=123,frequency=D3DSTREAMSOURCE_INDEXEDDATA|3,fill=D3DFILL_WIREFRAME;
    Texture* texture=nullptr;Block* block=nullptr;bool scene=false,fail_apply=false,fail_draw=false;
    int fail_set=-1,throw_set=-1,sets=0,query_fail=-1,setup_sets=0;bool throw_query=false;unsigned draws=0;
};
ULONG STDMETHODCALLTYPE SAdd(Surface* s){return ++s->refs;}
ULONG STDMETHODCALLTYPE SRelease(Surface* s){Check(s->refs>0,"surface ref underflow");return --s->refs;}
HRESULT STDMETHODCALLTYPE Desc(Surface* s,D3DSURFACE_DESC* out){*out={};out->Width=s->width;out->Height=s->height;
    out->Format=D3DFMT_A8R8G8B8;out->Type=D3DRTYPE_SURFACE;out->Usage=D3DUSAGE_RENDERTARGET;return S_OK;}
ULONG STDMETHODCALLTYPE TRelease(Texture* s){Check(s->refs>0,"texture ref underflow");return --s->refs;}
HRESULT STDMETHODCALLTYPE LockTexture(Texture* s,UINT,D3DLOCKED_RECT* out,const RECT*,DWORD){out->Pitch=1024;out->pBits=s->pixels.data();return S_OK;}
HRESULT STDMETHODCALLTYPE UnlockTexture(Texture*,UINT){return S_OK;}
ULONG STDMETHODCALLTYPE BRelease(Block* s){Check(s->refs>0,"state block ref underflow");return --s->refs;}
HRESULT STDMETHODCALLTYPE Capture(Block* s){s->tag=s->device->tag;s->frequency=s->device->frequency;s->fill=s->device->fill;return S_OK;}
HRESULT STDMETHODCALLTYPE Apply(Block* s){s->device->tag=s->tag;s->device->frequency=s->frequency;s->device->fill=s->fill;return s->device->fail_apply?E_FAIL:S_OK;}
HRESULT STDMETHODCALLTYPE Caps(Device*,D3DCAPS9* out){*out={};out->NumSimultaneousRTs=4;return S_OK;}
HRESULT STDMETHODCALLTYPE CreateTexture(Device* d,UINT,UINT,UINT,DWORD,D3DFORMAT,D3DPOOL,IDirect3DTexture9** out,HANDLE*){
    ++d->texture->refs;*out=reinterpret_cast<IDirect3DTexture9*>(d->texture);return S_OK;}
HRESULT STDMETHODCALLTYPE CreateBlock(Device* d,D3DSTATEBLOCKTYPE,IDirect3DStateBlock9** out){
    ++d->block->refs;d->block->device=d;*out=reinterpret_cast<IDirect3DStateBlock9*>(d->block);return S_OK;}
HRESULT STDMETHODCALLTYPE Target(Device* d,DWORD index,IDirect3DSurface9** out){
    auto* s=d->targets[index];*out=reinterpret_cast<IDirect3DSurface9*>(s);if(s)SAdd(s);
    if(d->query_fail==static_cast<int>(index)){if(d->throw_query)throw 1;return E_FAIL;}
    return s?S_OK:D3DERR_NOTFOUND;
}
HRESULT STDMETHODCALLTYPE Depth(Device* d,IDirect3DSurface9** out){*out=reinterpret_cast<IDirect3DSurface9*>(d->depth);if(d->depth)SAdd(d->depth);return d->depth?S_OK:D3DERR_NOTFOUND;}
HRESULT STDMETHODCALLTYPE Viewport(Device* d,D3DVIEWPORT9* out){*out=d->viewport;return S_OK;}
HRESULT Changed(Device* d,bool tag=true){const int n=d->sets++;if(tag)++d->tag;if(n==d->throw_set)throw 1;return n==d->fail_set?E_FAIL:S_OK;}
HRESULT STDMETHODCALLTYPE SetTarget(Device* d,DWORD index,IDirect3DSurface9* target){
    auto* s=reinterpret_cast<Surface*>(target);
    if(!index&&s){for(unsigned i=1;i<4;++i)if(d->targets[i]&&d->targets[i]->width!=s->width)return E_FAIL;
        if(d->depth&&d->depth->width!=s->width)return E_FAIL;}
    d->targets[index]=s;return Changed(d,false);
}
HRESULT STDMETHODCALLTYPE SetDepth(Device* d,IDirect3DSurface9* surface){d->depth=reinterpret_cast<Surface*>(surface);return Changed(d,false);}
HRESULT STDMETHODCALLTYPE SetViewport(Device* d,const D3DVIEWPORT9* viewport){d->viewport=*viewport;return Changed(d,false);}
HRESULT STDMETHODCALLTYPE SetShader(Device* d,void*){return Changed(d);}
HRESULT STDMETHODCALLTYPE Fvf(Device* d,DWORD){return Changed(d);}
HRESULT STDMETHODCALLTYPE SetTexture(Device* d,DWORD,IDirect3DBaseTexture9*){return Changed(d);}
HRESULT STDMETHODCALLTYPE Render(Device* d,D3DRENDERSTATETYPE type,DWORD value){if(type==D3DRS_FILLMODE)d->fill=value;return Changed(d);}
HRESULT STDMETHODCALLTYPE Frequency(Device* d,UINT,UINT value){d->frequency=value;return Changed(d);}
HRESULT STDMETHODCALLTYPE Stage(Device* d,DWORD,D3DTEXTURESTAGESTATETYPE,DWORD){return Changed(d);}
HRESULT STDMETHODCALLTYPE Sampler(Device* d,DWORD,D3DSAMPLERSTATETYPE,DWORD){return Changed(d);}
HRESULT STDMETHODCALLTYPE Begin(Device* d){if(d->scene)return E_FAIL;d->scene=true;return S_OK;}
HRESULT STDMETHODCALLTYPE End(Device* d){if(!d->scene)return E_FAIL;d->scene=false;return S_OK;}
HRESULT STDMETHODCALLTYPE DrawCall(Device* d,D3DPRIMITIVETYPE,UINT count,const void*,UINT stride){
    Check(d->scene&&count>0&&stride==28&&d->frequency==1&&d->fill==D3DFILL_SOLID,"own scene, fill and non-instanced overlay");++d->draws;d->setup_sets=d->sets;
    // A draw can pump input while the resource lock is held.
    Check(ShaderDebugOverlay::QueueClick(40,100,1920,1080),"input does not use resource lock");
    return d->fail_draw?E_FAIL:S_OK;
}
}
int main(){using namespace ShaderDebugOverlay;try{
    std::array<void*,17> surface_table{};surface_table[1]=reinterpret_cast<void*>(&SAdd);surface_table[2]=reinterpret_cast<void*>(&SRelease);surface_table[12]=reinterpret_cast<void*>(&Desc);
    std::array<void*,22> texture_table{};texture_table[2]=reinterpret_cast<void*>(&TRelease);texture_table[19]=reinterpret_cast<void*>(&LockTexture);texture_table[20]=reinterpret_cast<void*>(&UnlockTexture);
    std::array<void*,6> block_table{};block_table[2]=reinterpret_cast<void*>(&BRelease);block_table[4]=reinterpret_cast<void*>(&Capture);block_table[5]=reinterpret_cast<void*>(&Apply);
    std::array<void*,119> table{};
    table[7]=reinterpret_cast<void*>(&Caps);table[23]=reinterpret_cast<void*>(&CreateTexture);table[59]=reinterpret_cast<void*>(&CreateBlock);
    table[37]=reinterpret_cast<void*>(&SetTarget);table[38]=reinterpret_cast<void*>(&Target);table[39]=reinterpret_cast<void*>(&SetDepth);table[40]=reinterpret_cast<void*>(&Depth);
    table[47]=reinterpret_cast<void*>(&SetViewport);table[48]=reinterpret_cast<void*>(&Viewport);table[92]=reinterpret_cast<void*>(&SetShader);table[107]=reinterpret_cast<void*>(&SetShader);
    table[89]=reinterpret_cast<void*>(&Fvf);table[65]=reinterpret_cast<void*>(&SetTexture);table[57]=reinterpret_cast<void*>(&Render);table[67]=reinterpret_cast<void*>(&Stage);table[69]=reinterpret_cast<void*>(&Sampler);
    table[41]=reinterpret_cast<void*>(&Begin);table[42]=reinterpret_cast<void*>(&End);table[83]=reinterpret_cast<void*>(&DrawCall);table[102]=reinterpret_cast<void*>(&Frequency);
    Surface high{surface_table.data()},mrt1{surface_table.data()},mrt2{surface_table.data()},depth{surface_table.data()},low{surface_table.data(),1,1920,1080};
    Texture texture{texture_table.data()};Block block{block_table.data()};Device d{};d.table=table.data();d.targets={&high,&mrt1,&mrt2,nullptr};d.depth=&depth;d.texture=&texture;d.block=&block;
    auto* device=reinterpret_cast<IDirect3DDevice9*>(&d);auto* output=reinterpret_cast<IDirect3DSurface9*>(&low);
    const auto targets=d.targets;const auto viewport=d.viewport;Status status{};status.applied_valid=1;
    auto Restored=[&]{Check(d.targets==targets&&d.depth==&depth&&!std::memcmp(&viewport,&d.viewport,sizeof(viewport))&&d.tag==123&&!d.scene
        &&d.frequency==(D3DSTREAMSOURCE_INDEXEDDATA|3)&&d.fill==D3DFILL_WIREFRAME,"all original state restored");
        Check(high.refs==1&&mrt1.refs==1&&mrt2.refs==1&&depth.refs==1&&low.refs==1&&block.refs==0,"all original COM refs balanced");};
    Check(!QueueClick(1,1,1920,1080)&&!HitTest(1,1,1920,1080)&&HitTest(20,200,1920,1080),"panel bounds");
    for(DWORD row=0;row<4;++row){Check(QueueClick(40,static_cast<LONG>(60+row*32+1),1920,1080),"row input");Selection selection{};
        Check(ConsumeSelection(&selection)&&selection.enabled==(row?1u:0u)&&selection.variant==(row?row-1:0u),"four exact choices");Check(!ConsumeSelection(&selection),"consume once");}
    Check(Status{}.profile==Profile::Water&&Selection{}.profile==Profile::Water,"legacy defaults remain Water");
    for(DWORD family=1;family<=5;++family){const auto profile=static_cast<Profile>(family);
        for(DWORD row=0;row<4;++row){
            const bool available=(profile==Profile::Terrain||profile==Profile::Shadow)?row<=1:profile==Profile::Ambient?row<=2:true;
            Check(HitTest(40,static_cast<LONG>(60+row*32+1),1920,1080,profile),"disabled rows remain inside panel hitbox");
            Check(QueueClick(40,static_cast<LONG>(60+row*32+1),1920,1080,profile)==available,"only supported family choices queue");
            Selection selection{};
            if(available)Check(ConsumeSelection(&selection)&&selection.profile==profile&&selection.enabled==(row?1u:0u)
                &&selection.variant==(row?row-1:0u),"exact family tag and normalized choice");
            else Check(!ConsumeSelection(&selection),"disabled row cannot enqueue fallback choice");
        }
    }
    Check(!QueueClick(40,70,1920,1080,static_cast<Profile>(99))&&!HitTest(40,70,1920,1080,static_cast<Profile>(99)),"unknown family refuses input");
    SetLastError(0x7357);Check(ShaderDebugOverlay::Draw(device,output,1920,1080,status),"actual renderer through authored COM");Check(GetLastError()==0x7357,"LastError preserved");Restored();Check(texture.refs==1,"font cached");
    // Every partial setter failure and post-mutation C++ exception must restore.
    const int setup_sets=d.setup_sets;
    for(int n=0;n<setup_sets;++n){d.sets=0;d.fail_set=n;ShaderDebugOverlay::Draw(device,output,1920,1080,status);Check(!HasUnsafe(),"failed setter recovered");Restored();d.fail_set=-1;}
    d.sets=0;d.throw_set=6;Check(!ShaderDebugOverlay::Draw(device,output,1920,1080,status),"setter throws after mutation");d.throw_set=-1;Check(!HasUnsafe(),"throw restored");Restored();
    d.fail_draw=true;Check(!ShaderDebugOverlay::Draw(device,output,1920,1080,status),"original overlay draw failure");d.fail_draw=false;Restored();
    d.fail_apply=true;Check(!ShaderDebugOverlay::Draw(device,output,1920,1080,status)&&HasUnsafe(),"failed restore retained");
    Stats stats{};Check(ReadStats(&stats)&&stats.pending_restore&&stats.retained_original_refs>0,"durable backup exposed");
    Check(!Reset(device)&&!Shutdown()&&!ShaderDebugOverlay::Draw(device,output,1920,1080,status),"unsafe refuses cleanup/new draw");
    d.fail_apply=false;Check(RetryRestore(device)&&!HasUnsafe(),"explicit owner retry");Restored();
    d.query_fail=1;Check(!ShaderDebugOverlay::Draw(device,output,1920,1080,status),"failed getter after outref");Restored();
    d.throw_query=true;Check(!ShaderDebugOverlay::Draw(device,output,1920,1080,status),"throw getter after outref");Restored();d.query_fail=-1;d.throw_query=false;
    for(DWORD family=2;family<=5;++family){status.profile=static_cast<Profile>(family);status.applied_enabled=1;
        status.applied_variant=family==5?1u:0u;status.pending=1;status.pending_enabled=1;status.pending_variant=status.applied_variant;
        Check(ShaderDebugOverlay::Draw(device,output,1920,1080,status),"all supported profile labels render through same safe pipeline");Restored();}
    status.profile=Profile::Shadow;status.applied_variant=1;status.pending_variant=0;
    Check(!ShaderDebugOverlay::Draw(device,output,1920,1080,status),"unavailable shadow variant cannot claim applied");Restored();
    Selection discard{};for(unsigned i=0;i<6&&ConsumeSelection(&discard);++i){}
    Preset preset{};std::uint64_t revision=0;Check(ReadPreset(&preset,&revision)&&revision>0,"read default combined preset");
    Check(preset.values[0].enabled==1&&!preset.values[0].variant,"combined defaults accepted Water B");
    for(unsigned i=1;i<5;++i)Check(!preset.values[i].enabled&&!preset.values[i].variant,"other families default Original");
    const DWORD rows[]={3,2,1,1,2};
    for(DWORD i=0;i<5;++i){const auto profile=static_cast<Profile>(i+1);Preset before{};std::uint64_t before_revision=0;
        Check(ReadPreset(&before,&before_revision)&&QueueTab(profile)&&SelectedTab()==profile,"tab selected UI only");
        Preset after{};std::uint64_t after_revision=0;Check(ReadPreset(&after,&after_revision)&&before_revision==after_revision
            &&!std::memcmp(&before,&after,sizeof(Preset)),"tab does not mutate feature preset or revision");
        Check(QueueClick(40,static_cast<LONG>(92+rows[i]*32+1),1920,1080,Profile::All),"combined row click queues family");
    }
    DWORD changed=0;Check(ConsumePreset(&preset,&changed,&revision)&&changed==31,"atomic snapshot retains all five rapid choices");
    for(DWORD i=0;i<5;++i)Check(preset.values[i].profile==static_cast<Profile>(i+1)&&preset.values[i].enabled==1
        &&preset.values[i].variant==rows[i]-1,"chosen family values survive tab changes");
    Check(!ConsumePreset(&preset,&changed),"combined dirty mask consumed once");
    const auto retained=preset;const auto retained_revision=revision;
    for(unsigned invalid=0;invalid<4;++invalid){auto bad=retained;
        if(invalid==0)bad.values[3].variant=1;if(invalid==1)bad.values[4].variant=2;
        if(invalid==2)bad.values[2].profile=Profile::Leaf;if(invalid==3){bad.values[0].enabled=0;bad.values[0].variant=2;}
        Check(!SetPreset(&bad,true),"entire invalid preset rejected before any mutation");
        Preset observed{};std::uint64_t observed_revision=0;Check(ReadPreset(&observed,&observed_revision)&&observed_revision==retained_revision
            &&!std::memcmp(&observed,&retained,sizeof(Preset)),"invalid persistence input retains complete prior state");
    }
    Check(!QueueTab(Profile::All)&&!QueueTab(static_cast<Profile>(99)),"tabs accept only real families");
    Check(QueueTab(Profile::Shadow)&&!QueueClick(40,92+2*32+1,1920,1080,Profile::All),"combined disabled shadow row cannot queue");
    Check(!ConsumePreset(&preset,&changed),"disabled row and tab alone create no feature event");
    Preset defaults{};Check(SetPreset(&defaults,false)&&!ConsumePreset(&preset,&changed),"restore model without applying shaders");
    Check(QueueApplyAll(),"apply all preview event");
    for(DWORD i=0;i<5;++i){Selection event{};Check(ConsumeSelection(&event)&&event.profile==static_cast<Profile>(i+1)
        &&event.enabled==(i?0u:1u)&&!event.variant,"per-family queues retain independent selected values");}
    Check(!ConsumeSelection(&discard),"all five apply events consumed");
    MultiStatus multi{};for(auto& item:multi.families)item.applied_valid=1;multi.families[0].applied_enabled=1;
    for(DWORD i=1;i<=5;++i){Check(QueueTab(static_cast<Profile>(i))&&DrawAll(device,output,1920,1080,multi),"all tabs render with independent Core status");Restored();}
    Check(HitTest(430,300,1920,1080,Profile::All)&&!HitTest(430,300,1920,1080,Profile::Water),"combined compact panel bounds");
    Check(QueueClick(25+3*80,53,1920,1080,Profile::All)&&SelectedTab()==Profile::Shadow,"physical tab click changes only selected tab");
    Check(!ConsumePreset(&preset,&changed),"physical tab click creates no shader event");
    Check(QueueClick(40,240,1920,1080,Profile::All)&&ConsumePreset(&preset,&changed)&&changed==31,"physical ApplyAll button snapshots current preset");
    Check(Reset(device)&&texture.refs==0,"external Release/Reset clears parent-retaining font");Check(Shutdown(),"quiescent shutdown");
    Check(ReadPreset(&preset)&&!std::memcmp(&preset,&defaults,sizeof(Preset)),"shutdown retains final choice model for last report");
    std::printf("{\"authored_cpu_checks\":%u,\"passed\":true,\"gpu_or_game\":false}\n",checks);return 0;
}catch(const std::exception& error){std::fprintf(stderr,"overlay fixture: %s\n",error.what());return 1;}catch(...){std::fprintf(stderr,"overlay fixture: exception\n");return 1;}}
