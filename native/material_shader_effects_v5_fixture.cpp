// SPDX-License-Identifier: MIT
// Authored COM only: unchanged Leaf palette + Macro terrain together.
#define S7_MATERIAL_V2_ENTRY RetainedV2FixtureMain
#include "material_shader_effects_v2_fixture.cpp"
#include "material_shader_effects_contract_v5.h"
#include <filesystem>
namespace {
struct CombinedTexture {void**table=nullptr;ULONG refs=1;D3DSURFACE_DESC desc{};UINT levels=0;};
ULONG STDMETHODCALLTYPE TAdd(CombinedTexture*t){return ++t->refs;}
ULONG STDMETHODCALLTYPE TRelease(CombinedTexture*t){Verify(t->refs>1,"texture ref balance");return --t->refs;}
D3DRESOURCETYPE STDMETHODCALLTYPE TType(CombinedTexture*){return D3DRTYPE_TEXTURE;}
DWORD STDMETHODCALLTYPE TLevels(CombinedTexture*t){return t->levels;}
HRESULT STDMETHODCALLTYPE TDesc(CombinedTexture*t,UINT level,D3DSURFACE_DESC*out){if(level)return E_FAIL;*out=t->desc;return S_OK;}
struct CombinedDevice:V2Device {std::array<CombinedTexture*,3> textures{};};
HRESULT STDMETHODCALLTYPE Texture(CombinedDevice*d,DWORD sampler,IDirect3DBaseTexture9**out){*out=nullptr;if(sampler<4||sampler>6)return E_FAIL;
    auto*t=d->textures[sampler-4];TAdd(t);*out=reinterpret_cast<IDirect3DBaseTexture9*>(t);return S_OK;}
}
int wmain(int argc,wchar_t**argv){using namespace MaterialShaderEffects;try{
    Verify(argc==2,"new combined fixture path");const std::wstring folder=argv[1];Verify(CreateDirectoryW(folder.c_str(),nullptr)!=0,"new case");
    std::array<void*,5> st{};st[1]=reinterpret_cast<void*>(&ShaderAddRef);st[2]=reinterpret_cast<void*>(&ShaderRelease);st[4]=reinterpret_cast<void*>(&V2Function);
    std::array<void*,17> rt{};rt[1]=reinterpret_cast<void*>(&SurfaceAddRef);rt[2]=reinterpret_cast<void*>(&SurfaceRelease);rt[12]=reinterpret_cast<void*>(&Description);
    Surface surface{rt.data(),1};std::array<void*,134> table{};
    table[93]=reinterpret_cast<void*>(&V2GetVertex);table[108]=reinterpret_cast<void*>(&V2GetPixel);table[92]=reinterpret_cast<void*>(&V2SetVertex);table[107]=reinterpret_cast<void*>(&V2SetPixel);
    table[95]=reinterpret_cast<void*>(&GetVS);table[110]=reinterpret_cast<void*>(&GetPS);table[94]=reinterpret_cast<void*>(&SetVS);table[109]=reinterpret_cast<void*>(&SetPS);
    table[38]=reinterpret_cast<void*>(&Target);table[48]=reinterpret_cast<void*>(&Viewport);table[58]=reinterpret_cast<void*>(&RenderState);table[91]=reinterpret_cast<void*>(&CreateVertex);table[106]=reinterpret_cast<void*>(&V2CreatePixel);table[64]=reinterpret_cast<void*>(&Texture);
    CombinedDevice d{};d.table=table.data();d.surface=&surface;d.shader_table=st.data();auto*device=reinterpret_cast<IDirect3DDevice9*>(static_cast<Device*>(&d));
    std::array<void*,22> tt{};tt[1]=reinterpret_cast<void*>(&TAdd);tt[2]=reinterpret_cast<void*>(&TRelease);tt[10]=reinterpret_cast<void*>(&TType);tt[13]=reinterpret_cast<void*>(&TLevels);tt[17]=reinterpret_cast<void*>(&TDesc);
    std::array<CombinedTexture,3> textures{};for(unsigned i=0;i<3;++i){auto&t=textures[i];t.table=tt.data();t.desc.Width=t.desc.Height=i==2?256:512;t.desc.Format=i==2?D3DFMT_L8:D3DFMT_DXT5;t.levels=i==2?9:10;d.textures[i]=&t;}
    std::array<Shader,6> originals{};for(unsigned i=0;i<3;++i){originals[i*2]={st.data(),1,false,Code(true,100+i)};originals[i*2+1]={st.data(),1,false,Code(false,200+i)};
        const auto v=Hash(originals[i*2].code.data(),20),p=Hash(originals[i*2+1].code.data(),20);Verify(FixtureCompatibility(i+1,v.data(),p.data(),20,20,nullptr),"authored exact role pair");}
    const auto report=folder+L"/native.private.json";NativeEffectsContractV5::Resolved paths{};
    Verify(NativeEffectsContractV5::Resolve(report.c_str(),MaterialShaderConfigV5::Layout::PrivateCase,paths,false)==NativeEffectsContractV5::Error::None,"derive seven fixed paths without wire filenames");
    std::array<std::vector<unsigned char>,7> codes{};std::array<std::array<unsigned char,32>,7> hashes{};
    for(unsigned i=0;i<7;++i){codes[i]=Code(i==3||i==4,400+i);hashes[i]=Hash(codes[i].data(),20);Verify(FixtureProgramCompatibility(7,i,hashes[i].data(),20),"synthetic seven-code pins");
        std::filesystem::create_directories(std::filesystem::path(paths.program_paths[i]).parent_path());Write(paths.program_paths[i],codes[i]);}
    MaterialShaderConfigV5::Config config{};config.render_width=1920;config.render_height=1080;
    NativeGraphicsConfig::Version2 native{};native.base.size=sizeof(native);native.base.version=2;native.flags=NativeGraphicsConfig::Terrain|NativeGraphicsConfig::Vegetation;
    native.base.render_width=native.base.output_width=1920;native.base.render_height=native.base.output_height=1080;wcscpy_s(native.base.report_path,report.c_str());
    Verify(NativeEffectsContractV5::Validate(config,native,MaterialShaderConfigV5::Layout::PrivateCase)==NativeEffectsContractV5::Error::None,"host admits zero initial flags with both declared families");
    auto changed=config;changed.flags=6;changed.selected_variant=2;Verify(NativeEffectsContractV5::SamePinnedConfig(config,changed),"only tupleflags/index mutable");changed.render_width=1280;Verify(!NativeEffectsContractV5::SamePinnedConfig(config,changed),"dimensions pinned");
    changed=config;changed.flags=1;Verify(!InitializeV5(&changed,report.c_str()),"legacy River never enters combined module");
    auto corrupt=codes[6];corrupt[8]^=1;OverwriteFixture(paths.program_paths[6],corrupt);Verify(!InitializeV5(&config,report.c_str()),"bad last program rejects all seven atomically");
    for(unsigned i=0;i<6;++i)Verify(WritableFixture(paths.program_paths[i]),"failed initialization releases prior leases");OverwriteFixture(paths.program_paths[6],codes[6]);
    Verify(InitializeV5(&config,report.c_str()),"preload seven while both families OFF");auto stats=Snapshot();Verify(stats.config_version==5&&stats.initialized_flags==6&&!stats.enabled_flags&&stats.selected_variant==0&&!stats.shader_creations&&stats.combined_program_sha256==hashes,"complete actual loaded identities while OFF");
    Verify(stats.variant_sha256==std::array<std::array<unsigned char,32>,3>{{hashes[0],hashes[5],hashes[6]}},"Leaf identity slot mapping");
    for(unsigned i=0;i<7;++i)Verify(!WritableFixture(paths.program_paths[i]),"all seven files immutable");
    d.vs[66]={56,64,3200,3200};d.vs[67]={512,9,8,0};d.ps[33]={15,.5f,.02f,.2f};
    auto Bind=[&](unsigned role){SetVertex(&d,reinterpret_cast<IDirect3DVertexShader9*>(&originals[role*2]));FixtureSetPixel(&d,reinterpret_cast<IDirect3DPixelShader9*>(&originals[role*2+1]));d.blend=role==2?0:1;};
    auto Draw=[&](unsigned role,bool enabled,HRESULT result=S_OK){Bind(role);const auto constants=d.ps[33];DrawTransaction draw{};BeginDraw(device,&draw);Verify(draw.active==enabled&&!draw.unsafe_state,"only enabled family changes draw");
        if(enabled){Verify(!ConfigureV5(&config),"no configuration inside active transaction");if(role==2)Verify(d.vertex==&originals[4]&&d.pixel->code==codes[config.selected_variant?config.selected_variant+4:0],"selected Leaf PS only, original VS");
            else Verify(d.vertex->code==codes[role?4:3]&&d.pixel->code==codes[role?2:1],"paired macro terrain stages");EndDraw(&draw,result);}
        Verify(!draw.unsafe_state&&d.vertex==&originals[role*2]&&d.pixel==&originals[role*2+1]&&d.ps[33]==constants,"all original stages/constants restored");EndFrame(device);};
    Draw(2,false);config.flags=6;config.selected_variant=1;Verify(ConfigureV5(&config)&&Snapshot().enable_changes==1,"atomic combined tuple one epoch");Draw(2,true);
    Verify(Snapshot().retained_programs==3&&Snapshot().shader_creations==3,"Leaf branch eagerly creates three");Draw(0,true);Verify(Snapshot().retained_programs==7&&Snapshot().shader_creations==7,"Terrain branch eagerly creates four without dropping Leaf");Draw(1,true,E_FAIL);
    config.selected_variant=2;Verify(ConfigureV5(&config),"change Leaf while Terrain remains enabled");Draw(0,true);Draw(2,true);
    config.flags=4;Verify(ConfigureV5(&config),"Terrain OFF leaves Leaf active");Draw(0,false);Draw(2,true);
    config.flags=2;Verify(ConfigureV5(&config),"Leaf OFF leaves Terrain active");Draw(2,false);Draw(1,true);
    config.flags=0;Verify(ConfigureV5(&config),"both OFF");Draw(0,false);Draw(2,false);stats=Snapshot();Verify(stats.shader_creations==7&&stats.retained_programs==7&&stats.peak_retained_programs==7,"group toggles retain bounded warm seven programs");
    for(auto n:stats.combined_program_creations)Verify(n==1,"one creation per fixed program");Reset(device);
    Verify(!live_created&&!Snapshot().retained_programs,"mandatory Reset releases seven programs");config.flags=6;Verify(ConfigureV5(&config),"reenable after reset");Draw(0,true);Draw(2,true);Verify(Snapshot().shader_creations==14,"new device epoch recreates exactly seven");
    Bind(0);d.fail_pixel=true;DrawTransaction partial{};BeginDraw(device,&partial);Verify(!partial.active&&!partial.unsafe_state&&d.vertex==&originals[0]&&d.pixel==&originals[1],"partial paired setter rollback");
    DrawTransaction unsafe{};BeginDraw(device,&unsafe);Verify(unsafe.active,"prepare durable backup");d.fail_restore_pixel=true;EndDraw(&unsafe,S_OK);Verify(unsafe.unsafe_state&&!ConfigureV5(&config),"unsafe blocks all family switches");d.fail_restore_pixel=false;Verify(RetryRestore(&unsafe),"durable retry restores original");
    Reset(device);Shutdown();SetVertex(&d,nullptr);FixtureSetPixel(&d,nullptr);for(auto&s:originals)Verify(s.refs==1,"original refs balanced");for(auto&t:textures)Verify(t.refs==1,"texture refs balanced");Verify(surface.refs==1&&!live_created,"all owned COM refs closed");
    std::printf("CombinedV5 CPU: %u checks PASS; Stats %zu; no GPU/game\n",v2_checks,sizeof(Stats));return 0;
}catch(const std::exception&e){std::fprintf(stderr,"CombinedV5 CPU failed: %s\n",e.what());return 1;}}
