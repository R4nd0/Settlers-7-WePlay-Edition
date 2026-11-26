// Authored fake COM only; no game or GPU.
#define wmain RetainedWaterMain
#include "water_shader_effects_fixture.cpp"
#undef wmain
#include "water_shader_palette_contract.h"
int wmain(int argc,wchar_t**argv){using namespace WaterShaderEffects;try{
    Check(argc==2,"new palette case");const std::wstring root=argv[1];Check(CreateDirectoryW(root.c_str(),nullptr)!=0,"new palette root");
    std::array<void*,5> st{};st[1]=reinterpret_cast<void*>(&Add);st[2]=reinterpret_cast<void*>(&Release);st[4]=reinterpret_cast<void*>(&Function);
    std::array<void*,17> rt{};rt[1]=reinterpret_cast<void*>(&SurfaceAdd);rt[2]=reinterpret_cast<void*>(&SurfaceRelease);rt[12]=reinterpret_cast<void*>(&Desc);
    std::array<void*,134> table{};table[93]=reinterpret_cast<void*>(&Vertex);table[108]=reinterpret_cast<void*>(&Pixel);table[107]=reinterpret_cast<void*>(&Set);table[106]=reinterpret_cast<void*>(&Create);
    table[38]=reinterpret_cast<void*>(&Target);table[48]=reinterpret_cast<void*>(&Viewport);table[58]=reinterpret_cast<void*>(&RenderState);
    Shader vertex{st.data(),1,Code(0xfffe0300,2592,1)},pixel{st.data(),1,Code(0xffff0300,3740,2)};
    std::array<std::vector<unsigned char>,3> programs{};std::array<std::array<unsigned char,32>,3> hashes{};
    for(unsigned i=0;i<3;++i){programs[i]=Code(0xffff0300,3840,10+i);hashes[i]=Hash(programs[i]);Check(FixturePaletteIdentity(i,hashes[i].data()),"fixture palette pin");}
    const auto vh=Hash(vertex.code),ph=Hash(pixel.code);Check(FixtureIdentities(vh.data(),ph.data(),hashes[0].data()),"fixture original pair");
    const auto local=root+L"\\LocalData",materials=local+L"\\MaterialShaders";
    Check(CreateDirectoryW(local.c_str(),nullptr)&&CreateDirectoryW(materials.c_str(),nullptr),"private hierarchy");
    ConfigPalette config{};config.render_width=1920;config.render_height=1080;
    for(unsigned i=0;i<3;++i){const auto recipe=materials+L"\\"+WaterShaderPaletteContract::Recipes[i];Check(CreateDirectoryW(recipe.c_str(),nullptr)!=0,"fixed recipe");
        std::wstring digest;for(unsigned j=0;j<64;++j)digest+=static_cast<wchar_t>(WaterShaderPaletteContract::Digests[i][j]);
        wcscpy_s(config.program_paths[i],(recipe+L"\\"+digest+L".ps.bin").c_str());Write(config.program_paths[i],programs[i]);}
    Check(Initialize((root+L"\\native.private.json").c_str(),1920,1080),"initialize trusted module");
    auto bad=config;bad.selected_variant=3;Check(!ConfigurePalette(&bad),"selection bound");bad=config;bad.program_paths[2][30]^=1;Check(!ConfigurePalette(&bad)&&!Snapshot().configured,"all-three preflight");
    config.selected_variant=2;SetLastError(0x37570004);Check(ConfigurePalette(&config)&&GetLastError()==0x37570004,"OFF preloads all3 with LastError preserved");
    auto stats=Snapshot();Check(stats.config_version==2&&stats.selected_variant==2&&stats.variant==4&&!stats.enabled&&!stats.shader_creations&&stats.variant_sha256==hashes,"actual hashes and selection while OFF");
    for(unsigned i=0;i<3;++i){HANDLE file=CreateFileW(config.program_paths[i],GENERIC_WRITE,0,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);Check(file==INVALID_HANDLE_VALUE,"all3 program files leased");}
    Config legacy{};legacy.render_width=1920;legacy.render_height=1080;Check(!Configure(&legacy),"no implicit palette to legacy crossing");
    bad=config;bad.program_paths[0][0]^=1;Check(!ConfigurePalette(&bad),"all pinned paths immutable");
    Surface surface{rt.data()};Device d{};d.table=table.data();d.vertex=&vertex;d.pixel=&pixel;d.original=&pixel;d.surface=&surface;d.shader_table=st.data();Add(&vertex);Add(&pixel);
    auto* device=reinterpret_cast<IDirect3DDevice9*>(&d);
    auto Draw=[&](unsigned index,HRESULT result){config.enabled=1;config.selected_variant=index;Check(ConfigurePalette(&config),"safe selected palette configure");
        DrawTransaction draw{};SetLastError(0x37570005);BeginDraw(device,&draw);Check(GetLastError()==0x37570005&&draw.active&&draw.selected_variant==index,"selected actual transaction");
        Check(d.vertex==&vertex&&d.pixel->code==programs[index],"original VS and selected PS code");Check(!ConfigurePalette(&config),"no active transaction selection change");
        EndDraw(&draw,result);Check(!draw.unsafe_state&&d.pixel==&pixel,"original PS restored");EndFrame(device);
        const auto s=Snapshot();Check(s.completed_drawn_variant==(SUCCEEDED(result)?index:MAXDWORD)&&s.completed_successful_draws==(SUCCEEDED(result)?1u:0u)&&s.completed_failed_draws==(FAILED(result)?1u:0u),"same-frame successful/failed selection proof");};
    Draw(0,S_OK);Check(Snapshot().shader_creations==3&&Snapshot().retained_programs==3&&d.refs==4,"all3 created before first application");
    for(unsigned n=0;n<3;++n)for(unsigned i=0;i<3;++i){Draw(i,i==2?E_FAIL:S_OK);config.enabled=0;Check(ConfigurePalette(&config)&&Snapshot().retained_programs==3,"OFF retains palette programs");EndFrame(device);Check(Snapshot().completed_drawn_variant==MAXDWORD&&!Snapshot().completed_applications,"OFF carries no replacement proof");}
    stats=Snapshot();Check(stats.shader_creations==3,"all warm switches zero creation delta");for(unsigned i=0;i<3;++i){Check(stats.variant_creations[i]==1&&stats.variant_applications[i]==stats.variant_restorations[i],"pervariant balance");Check(stats.variant_applications[i]==(i?3u:4u),"pervariant exact applications");}
    Check(stats.variant_failed_draws[2]==3&&stats.variant_successful_draws[0]==4,"failed originals attributed by selected program");
    Draw(1,S_OK);const auto completed=Snapshot().completed_frame;Check(Reset(device)&&Snapshot().completed_frame==completed&&!Snapshot().retained_programs&&d.refs==1,"routine release drops COM refs and preserves copied proof");
    InvalidateFrame();Check(!Snapshot().completed_frame&&Snapshot().completed_drawn_variant==MAXDWORD,"actual reset clears proof");Draw(2,S_OK);Check(Snapshot().shader_creations==6,"next resource epoch creates all3 once");
    d.fail_apply=true;DrawTransaction partial{};BeginDraw(device,&partial);Check(!partial.active&&!partial.unsafe_state&&d.pixel==&pixel,"partial setter restores before original fallback");
    config.enabled=1;Check(ConfigurePalette(&config),"clear controlled failed application fault");DrawTransaction unsafe{};BeginDraw(device,&unsafe);Check(unsafe.active,"prepare durable restoration failure");d.fail_restore=true;EndDraw(&unsafe,S_OK);
    Check(unsafe.unsafe_state&&!ConfigurePalette(&config)&&!Reset(device),"unsafe state blocks reconfiguration/cleanup");d.fail_restore=false;Check(RetryRestore(&unsafe)&&d.pixel==&pixel,"same-callback retry recovers original");
    Check(Reset(device)&&Shutdown(),"safe full close");Bind(d,nullptr);Release(d.vertex);d.vertex=nullptr;Check(vertex.refs==1&&pixel.refs==1&&surface.refs==1&&d.refs==1,"all refs balanced");
    std::printf("WaterPalette CPU: %u checks PASS; Stats %zu bytes; no GPU/game\n",checks.load(),sizeof(Stats));return 0;
}catch(const std::exception&e){std::fprintf(stderr,"WaterPalette CPU failed: %s\n",e.what());return 1;}}
