// SPDX-License-Identifier: MIT
// Authored fake COM only: three programs in one device/resource epoch.
#define S7_MATERIAL_V2_ENTRY RetainedV2FixtureMain
#include "material_shader_effects_v2_fixture.cpp"
#include "material_shader_effects_contract_v4.h"
int wmain(int argc,wchar_t** argv){using namespace MaterialShaderEffects;try{
    Verify(argc==2,"new palette fixture folder");const std::wstring folder=argv[1];Verify(CreateDirectoryW(folder.c_str(),nullptr)!=0,"new palette folder");
    std::array<void*,5> shaders{};shaders[1]=reinterpret_cast<void*>(&ShaderAddRef);shaders[2]=reinterpret_cast<void*>(&ShaderRelease);shaders[4]=reinterpret_cast<void*>(&V2Function);
    std::array<void*,17> surfaces{};surfaces[1]=reinterpret_cast<void*>(&SurfaceAddRef);surfaces[2]=reinterpret_cast<void*>(&SurfaceRelease);surfaces[12]=reinterpret_cast<void*>(&Description);
    Surface surface{surfaces.data(),1};std::array<void*,134> table{};
    table[93]=reinterpret_cast<void*>(&V2GetVertex);table[108]=reinterpret_cast<void*>(&V2GetPixel);table[92]=reinterpret_cast<void*>(&V2SetVertex);table[107]=reinterpret_cast<void*>(&V2SetPixel);
    table[95]=reinterpret_cast<void*>(&GetVS);table[110]=reinterpret_cast<void*>(&GetPS);table[94]=reinterpret_cast<void*>(&SetVS);table[109]=reinterpret_cast<void*>(&SetPS);
    table[38]=reinterpret_cast<void*>(&Target);table[48]=reinterpret_cast<void*>(&Viewport);table[58]=reinterpret_cast<void*>(&RenderState);table[106]=reinterpret_cast<void*>(&V2CreatePixel);
    V2Device d{};d.table=table.data();d.surface=&surface;d.shader_table=shaders.data();auto* device=reinterpret_cast<IDirect3DDevice9*>(static_cast<Device*>(&d));
    Shader vertex{shaders.data(),1,false,Code(true,100)},pixel{shaders.data(),1,false,Code(false,200)};
    const auto vh=Hash(vertex.code.data(),20),ph=Hash(pixel.code.data(),20);Verify(FixtureCompatibility(3,vh.data(),ph.data(),20,20,nullptr),"authored Leaf pair");
    MaterialShaderConfigV4::Config config{};config.flags=Leaf;config.render_width=1920;config.render_height=1080;
    wcscpy_s(config.report_path,(folder+L"/material-effects.private.json").c_str());
    std::array<std::vector<unsigned char>,3> codes{};std::array<std::array<unsigned char,32>,3> hashes{};
    Verify(CreateDirectoryW((folder+L"/LocalData").c_str(),nullptr)!=0,"local folder");Verify(CreateDirectoryW((folder+L"/LocalData/MaterialShaders").c_str(),nullptr)!=0,"program folder");
    for(unsigned i=0;i<3;++i){codes[i]=Code(false,500+i);hashes[i]=Hash(codes[i].data(),20);Verify(FixtureProgramCompatibility(6,i,hashes[i].data(),20),"fixture-only content override");
        const auto parent=folder+L"/LocalData/MaterialShaders/"+NativeEffectsContractV4::Recipes[i];Verify(CreateDirectoryW(parent.c_str(),nullptr)!=0,"fixed recipe folder");
        wcscpy_s(config.program_paths[i],(parent+L"/"+NativeEffectsContractV4::Digests[i]+L".ps.bin").c_str());Write(config.program_paths[i],codes[i]);}
    NativeGraphicsConfig::Version2 native{};native.base.size=sizeof(native);native.base.version=2;native.flags=NativeGraphicsConfig::Vegetation;
    native.base.render_width=native.base.output_width=1920;native.base.render_height=native.base.output_height=1080;wcscpy_s(native.base.report_path,(folder+L"/native.private.json").c_str());
    auto off=config;off.flags=0;off.selected_variant=2;MaterialShaderConfigV4::Config initial{};
    Verify(NativeEffectsContractV4::PrepareInitial(off,native,NativeEffectsContractV4::Layout::PrivateCase,initial)==NativeEffectsContractV4::Error::None&&initial.flags==Leaf,"full host preflight including OFF selection");
    Verify(NativeEffectsContractV4::SamePinnedConfig(config,off),"only flags and selection mutable");
    for(unsigned i=0;i<3;++i){auto bad=config;bad.program_paths[i][30]^=1;Verify(!NativeEffectsContractV4::SamePinnedConfig(config,bad),"all program paths immutable");}
    auto bad=config;bad.selected_variant=3;Verify(!InitializeV4(&bad),"invalid selection refused");bad=config;bad.flags=Leaf|Terrain;Verify(!InitializeV4(&bad),"mixed material refused");
    auto corrupt=codes[2];corrupt[8]^=1;OverwriteFixture(config.program_paths[2],corrupt);Verify(!InitializeV4(&config),"last bad program prevents atomic initialization");
    Verify(WritableFixture(config.program_paths[0])&&WritableFixture(config.program_paths[1]),"failed last load drops earlier leases");OverwriteFixture(config.program_paths[2],codes[2]);
    Verify(InitializeV4(&config),"all3 CPU buffers admitted");auto stats=Snapshot();Verify(stats.variant_sha256==hashes&&!stats.shader_creations&&stats.selected_variant==0,"actual fixture hashes exposed without GPU");
    for(unsigned i=0;i<3;++i)Verify(!WritableFixture(config.program_paths[i]),"immutable program leases");
    Verify(SetEnabled(0)&&SetVariant(2)&&Snapshot().selected_variant==2,"switch while OFF");Verify(!SetVariant(3)&&!Snapshot().faulted,"invalid switch harmless");Verify(SetEnabled(Leaf),"enable after selected OFF");
    SetVertex(&d,reinterpret_cast<IDirect3DVertexShader9*>(&vertex));FixtureSetPixel(&d,reinterpret_cast<IDirect3DPixelShader9*>(&pixel));
    auto Draw=[&](unsigned index,HRESULT result){Verify(SetVariant(index),"safe selection");DrawTransaction txn{};BeginDraw(device,&txn);Verify(txn.active&&txn.selected_variant==index,"selected actual leaf transaction");
        Verify(d.vertex==&vertex&&d.pixel!=&pixel&&d.pixel->code==codes[index],"only selected PS with original VS");Verify(!SetVariant((index+1)%3)&&!Snapshot().faulted,"active transaction switch refused");
        EndDraw(&txn,result);Verify(!txn.unsafe_state&&d.vertex==&vertex&&d.pixel==&pixel,"original pair restored");EndFrame(device);};
    Draw(0,S_OK);Verify(Snapshot().shader_creations==3&&Snapshot().retained_programs==3&&live_created==3,"all3 created atomically before first bind");
    for(unsigned repeat=0;repeat<3;++repeat)for(unsigned i=0;i<3;++i){Draw(i,i==2?E_FAIL:S_OK);Verify(SetEnabled(0)&&Snapshot().retained_programs==3&&SetEnabled(Leaf),"OFF switching retains bounded programs");}
    stats=Snapshot();Verify(stats.shader_creations==3,"warm switches create no programs");for(unsigned i=0;i<3;++i){Verify(stats.variant_creations[i]==1&&stats.variant_applications[i]==stats.variant_restorations[i],"per-program creations and restore balance");Verify(stats.variant_applications[i]==(i?3u:4u),"per-program application provenance");}
    Verify(stats.variant_failed_draws[2]==3&&stats.variant_successful_draws[0]==4,"successful/failed original attribution");
    Reset(device);Verify(!live_created&&!Snapshot().retained_programs,"mandatory lifecycle releases all3");Draw(1,S_OK);Verify(Snapshot().shader_creations==6,"reset recreates exact all3 once");
    d.fail_pixel=true;DrawTransaction failed{};BeginDraw(device,&failed);Verify(!failed.active&&!failed.unsafe_state&&d.pixel==&pixel&&d.vertex==&vertex,"partial setter rollback");
    DrawTransaction unsafe{};BeginDraw(device,&unsafe);Verify(unsafe.active,"prepare failed restore");d.fail_restore_pixel=true;EndDraw(&unsafe,S_OK);Verify(unsafe.unsafe_state&&!SetVariant(0),"unsafe blocks switch");d.fail_restore_pixel=false;Verify(RetryRestore(&unsafe)&&d.pixel==&pixel,"durable restore retry");
    Reset(device);Shutdown();SetVertex(&d,nullptr);FixtureSetPixel(&d,nullptr);Verify(!live_created&&vertex.refs==1&&pixel.refs==1&&surface.refs==1,"all refs balanced");
    std::printf("Palette CPU: %u checks PASS; Stats %zu bytes; no GPU/game\n",v2_checks,sizeof(Stats));return 0;
}catch(const std::exception& error){std::fprintf(stderr,"palette CPU failed: %s\n",error.what());return 1;}}
