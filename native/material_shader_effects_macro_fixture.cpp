// SPDX-License-Identifier: MIT
// Authored fake COM only. No GPU, original programs, device, or game.
#define S7_MATERIAL_V2_ENTRY RetainedV2FixtureMain
#include "material_shader_effects_v2_fixture.cpp"
#include "terrain_macro_guard.h"
#include <limits>
namespace {
struct Texture {void** table=nullptr;ULONG refs=1;D3DSURFACE_DESC desc{};UINT levels=0;D3DRESOURCETYPE type=D3DRTYPE_TEXTURE;bool throw_desc=false;};
ULONG STDMETHODCALLTYPE TextureAddRef(Texture* t){return ++t->refs;}
ULONG STDMETHODCALLTYPE TextureRelease(Texture* t){Check(t->refs>1,"texture reference underflow");return --t->refs;}
D3DRESOURCETYPE STDMETHODCALLTYPE TextureType(Texture* t){return t->type;}
DWORD STDMETHODCALLTYPE TextureLevels(Texture* t){return t->levels;}
HRESULT STDMETHODCALLTYPE TextureDesc(Texture* t,UINT index,D3DSURFACE_DESC* desc){
    if(index)return E_FAIL;*desc=t->desc;if(t->throw_desc){t->throw_desc=false;throw std::runtime_error("texture desc after output");}return S_OK;
}
struct MacroDevice:V2Device {std::array<Texture*,3> textures{};bool fail_texture=false,throw_texture=false;unsigned texture_queries=0;};
HRESULT STDMETHODCALLTYPE GetTexture(MacroDevice* d,DWORD sampler,IDirect3DBaseTexture9** out){
    ++d->texture_queries;*out=nullptr;if(sampler<4||sampler>6)return E_FAIL;
    Texture* t=d->textures[sampler-4];if(t){TextureAddRef(t);*out=reinterpret_cast<IDirect3DBaseTexture9*>(t);}
    if(d->throw_texture){d->throw_texture=false;throw std::runtime_error("texture getter after owned output");}
    if(d->fail_texture){d->fail_texture=false;return E_FAIL;}return S_OK;
}
std::wstring Name(const std::vector<unsigned char>& bytes,bool vertex){
    const auto hash=Hash(bytes.data(),bytes.size());constexpr wchar_t digits[]=L"0123456789abcdef";std::wstring name;
    for(auto value:hash){name+=digits[value>>4];name+=digits[value&15];}return name+(vertex?L".vs.bin":L".ps.bin");
}
}
int wmain(int argc,wchar_t** argv){using namespace MaterialShaderEffects;try{
    Verify(argc==2,"new private macro fixture path");const std::wstring folder=argv[1];Verify(CreateDirectoryW(folder.c_str(),nullptr)!=0,"new macro fixture directory");
    std::array<void*,5> shader_table{};shader_table[1]=reinterpret_cast<void*>(&ShaderAddRef);shader_table[2]=reinterpret_cast<void*>(&ShaderRelease);shader_table[4]=reinterpret_cast<void*>(&V2Function);
    std::array<void*,17> surface_table{};surface_table[1]=reinterpret_cast<void*>(&SurfaceAddRef);surface_table[2]=reinterpret_cast<void*>(&SurfaceRelease);surface_table[12]=reinterpret_cast<void*>(&Description);
    Surface surface{surface_table.data(),1};std::array<void*,134> table{};
    table[93]=reinterpret_cast<void*>(&V2GetVertex);table[108]=reinterpret_cast<void*>(&V2GetPixel);table[92]=reinterpret_cast<void*>(&V2SetVertex);table[107]=reinterpret_cast<void*>(&V2SetPixel);
    table[95]=reinterpret_cast<void*>(&GetVS);table[110]=reinterpret_cast<void*>(&GetPS);table[94]=reinterpret_cast<void*>(&SetVS);table[109]=reinterpret_cast<void*>(&SetPS);
    table[38]=reinterpret_cast<void*>(&Target);table[48]=reinterpret_cast<void*>(&Viewport);table[58]=reinterpret_cast<void*>(&RenderState);table[91]=reinterpret_cast<void*>(&CreateVertex);table[106]=reinterpret_cast<void*>(&V2CreatePixel);table[64]=reinterpret_cast<void*>(&GetTexture);
    MacroDevice d{};d.table=table.data();d.surface=&surface;d.shader_table=shader_table.data();d.blend=1;
    auto* device=reinterpret_cast<IDirect3DDevice9*>(static_cast<Device*>(&d));
    std::array<void*,22> texture_table{};texture_table[1]=reinterpret_cast<void*>(&TextureAddRef);texture_table[2]=reinterpret_cast<void*>(&TextureRelease);
    texture_table[10]=reinterpret_cast<void*>(&TextureType);texture_table[13]=reinterpret_cast<void*>(&TextureLevels);texture_table[17]=reinterpret_cast<void*>(&TextureDesc);
    std::array<Texture,3> textures{};for(unsigned i=0;i<3;++i){auto& t=textures[i];t.table=texture_table.data();t.desc.Width=t.desc.Height=i==2?256u:512u;t.desc.Format=i==2?D3DFMT_L8:D3DFMT_DXT5;t.levels=i==2?9u:10u;d.textures[i]=&t;}
    std::array<Shader,4> originals{};
    for(unsigned i=0;i<2;++i){originals[i*2]={shader_table.data(),1,false,Code(true,100+i)};originals[i*2+1]={shader_table.data(),1,false,Code(false,200+i)};
        const auto v=Hash(originals[i*2].code.data(),20),p=Hash(originals[i*2+1].code.data(),20);Verify(FixtureCompatibility(i+1,v.data(),p.data(),20,20,nullptr),"authored original pair");}
    std::array<std::wstring,5> paths{},old_paths{};
    for(unsigned slot=0;slot<5;++slot){const bool vertex=slot==0||slot>=3;const auto code=Code(vertex,400+slot);const auto hash=Hash(code.data(),code.size());
        Verify(FixtureProgramCompatibility(3,slot,hash.data(),20),"macro fixture program identity");
        std::wstring parent=folder;
        if(slot>=3){parent+=slot==3?L"/terrain-macro-relief-BumpLayerShader-v3":L"/terrain-macro-relief-ParallaxLayerShader-v3";Verify(CreateDirectoryW(parent.c_str(),nullptr)!=0,"macro recipe directory");}
        paths[slot]=parent+L"/"+Name(code,vertex);Write(paths[slot],code);
        const auto old=Code(vertex,slot>=3?600+slot:400+slot);const auto old_hash=Hash(old.data(),old.size());
        Verify(FixtureProgramCompatibility(2,slot,old_hash.data(),20),"retained V2 program identity");old_paths[slot]=folder+L"/"+Name(old,vertex);
        if(old_paths[slot]!=paths[slot])Write(old_paths[slot],old);
    }
    MaterialShaderConfigV2::Config config{};config.flags=Terrain;config.render_width=1920;config.render_height=1080;config.terrain_detail_scale=config.terrain_relief_scale=1;
    wcscpy_s(config.bump_pixel_path,paths[1].c_str());wcscpy_s(config.parallax_pixel_path,paths[2].c_str());wcscpy_s(config.bump_vertex_path,paths[3].c_str());wcscpy_s(config.parallax_vertex_path,paths[4].c_str());
    const auto report=folder+L"/macro.private.json";wcscpy_s(config.report_path,report.c_str());
    auto mixed=config;wcscpy_s(mixed.bump_vertex_path,old_paths[3].c_str());Verify(!InitializeV2(&mixed),"mixed historical/macro vertex set refused atomically");
    mixed=config;wcscpy_s(mixed.parallax_vertex_path,old_paths[4].c_str());Verify(!InitializeV2(&mixed),"opposite mixed vertex set refused atomically");
    auto gain=config;gain.terrain_detail_scale=1.2f;Verify(!InitializeV2(&gain),"macro does not silently retain old colour gain");
    Verify(InitializeV2(&config),"initialize complete macro set on unchanged wire2");Verify(Read(report).find("\"terrain_recipe_variant\":3")!=std::string::npos,"explicit private recipe variant");
    d.vs[66]={56,64,3200,3200};d.vs[67]={512,9,8,0};d.ps[33]={15,.5f,.02f,.2f};
    auto Bind=[&](unsigned role){SetVertex(&d,reinterpret_cast<IDirect3DVertexShader9*>(&originals[role*2]));FixtureSetPixel(&d,reinterpret_cast<IDirect3DPixelShader9*>(&originals[role*2+1]));};
    auto Balanced=[&](){for(auto& t:textures)Verify(t.refs==1,"texture query references balanced");};
    for(unsigned role=0;role<2;++role){Bind(role);const auto vs66=d.vs[66],vs67=d.vs[67],ps33=d.ps[33];const unsigned queries=d.texture_queries;
        DrawTransaction draw{};SetLastError(0x3757abcd);BeginDraw(device,&draw);Verify(GetLastError()==0x3757abcd,"macro getters preserve LastError");Verify(draw.active&&draw.touched_vertex&&draw.touched_pixel,"guarded exact pair replaces both stages");
        Verify(d.texture_queries-queries==(role?3u:2u),"Bump queries only used diffuse/normal; Parallax includes height");Balanced();EndDraw(&draw,role?E_FAIL:S_OK);
        Verify(!draw.unsafe_state&&d.vertex==&originals[role*2]&&d.pixel==&originals[role*2+1],"original VS/PS restored including failed draw");Verify(d.vs[66]==vs66&&d.vs[67]==vs67&&d.ps[33]==ps33,"all constants bit-preserved");EndFrame(device);}
    Verify(Snapshot().retained_programs==4&&Snapshot().shader_creations==4,"bounded four terrain programs warm");
    for(unsigned frame=0;frame<8;++frame){Bind(0);DrawTransaction draw{};BeginDraw(device,&draw);Verify(draw.active,"warm macro draw");EndDraw(&draw,S_OK);EndFrame(device);}
    Verify(Snapshot().shader_creations==4,"no warm program recreation");
    Bind(0);const auto creates=Snapshot().shader_creations;
    for(unsigned bad=0;bad<6;++bad){auto region=d.vs[66],detail=d.vs[67];const auto desc=textures[0].desc;const auto levels=textures[1].levels;
        if(bad==0)d.vs[66][2]=6400;if(bad==1)d.vs[67][2]=4;if(bad==2)d.vs[67][1]=std::numeric_limits<float>::infinity();
        if(bad==3)textures[0].desc.Width=256;if(bad==4)textures[1].levels=9;if(bad==5)d.fail_texture=true;
        DrawTransaction draw{};BeginDraw(device,&draw);Verify(!draw.active&&!draw.unsafe_state&&d.vertex==&originals[0]&&d.pixel==&originals[1],"unknown macro input falls back before mutation");
        Verify(Snapshot().shader_creations==creates,"bad input cannot create shaders");Balanced();d.vs[66]=region;d.vs[67]=detail;textures[0].desc=desc;textures[1].levels=levels;}
    d.vs[66][0]=std::numeric_limits<float>::quiet_NaN();d.vs[67][3]=std::numeric_limits<float>::quiet_NaN();
    DrawTransaction unused{};BeginDraw(device,&unused);Verify(unused.active,"unused components are not misinterpreted");EndDraw(&unused,S_OK);d.vs[66][0]=56;d.vs[67][3]=0;
    d.fail_pixel=true;DrawTransaction partial{};BeginDraw(device,&partial);Verify(!partial.active&&!partial.unsafe_state&&d.vertex==&originals[0]&&d.pixel==&originals[1],"macro partial setter rollback");Balanced();
    Verify(SetEnabled(0)&&!live_created,"macro safe-boundary disable releases all generated programs");
    const unsigned disabled_queries=d.texture_queries;DrawTransaction disabled{};BeginDraw(device,&disabled);Verify(!disabled.active&&d.texture_queries==disabled_queries,"disabled macro has no input queries");
    Verify(SetEnabled(Terrain),"macro re-enable retains its pinned variant");DrawTransaction again{};BeginDraw(device,&again);Verify(again.active,"macro lazily recreates after toggle");EndDraw(&again,S_OK);
    Reset(device);Shutdown();Verify(!live_created,"reset/shutdown drops generated programs");
    for(unsigned kind=0;kind<2;++kind){Verify(InitializeV2(&config),"initialize for getter exception");Bind(0);if(kind==0)d.throw_texture=true;else textures[0].throw_desc=true;
        DrawTransaction draw{};BeginDraw(device,&draw);Verify(!draw.active&&!draw.unsafe_state&&d.vertex==&originals[0]&&d.pixel==&originals[1],"throwing texture getter never mutates GPU");Balanced();Verify(Snapshot().exceptions==1&&Snapshot().faulted,"getter exception reported fail-closed");Shutdown();}
    SetVertex(&d,nullptr);FixtureSetPixel(&d,nullptr);for(auto& s:originals)Verify(s.refs==1,"original shader refs balanced");Balanced();Verify(surface.refs==1&&!live_created,"all fake references balanced");
    std::printf("Macro terrain CPU transactions: %u checks passed; no GPU/game started\n",v2_checks);return 0;
}catch(const std::exception& error){std::fprintf(stderr,"macro fixture failed: %s\n",error.what());return 1;}}
