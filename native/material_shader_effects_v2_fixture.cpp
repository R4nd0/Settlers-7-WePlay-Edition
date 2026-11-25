// SPDX-License-Identifier: MIT
// Authored fake COM only. Includes the retained V1 regression fixture unchanged.
#define wmain LegacyFixtureMain
#include "material_shader_effects_fixture.cpp"
#undef wmain
#include "material_shader_config_v2.h"

namespace {
unsigned v2_checks=0;
void Verify(bool value,const char* message){++v2_checks;Check(value,message);}
struct V2Device:Device {
    bool fail_vertex=false,fail_pixel=false,throw_pixel=false;
    bool fail_restore_vertex=false,fail_restore_pixel=false,fail_create_pixel=false;
    bool throw_vertex_get=false,throw_target_get=false;
    unsigned throw_pixel_get=0;
};
unsigned function_body_reads=0;bool throw_function_body=false,throw_surface_description=false;
HRESULT STDMETHODCALLTYPE V2Function(Shader* shader,void* data,UINT* bytes){
    const HRESULT result=Function(shader,data,bytes);
    if(data){++function_body_reads;if(throw_function_body){throw_function_body=false;throw std::runtime_error("authored GetFunction exception");}}return result;
}
HRESULT STDMETHODCALLTYPE V2GetVertex(Device* base,IDirect3DVertexShader9** output){
    const HRESULT result=GetVertex(base,output);auto* d=static_cast<V2Device*>(base);
    if(d->throw_vertex_get){d->throw_vertex_get=false;throw std::runtime_error("authored VS getter after owned output");}return result;
}
HRESULT STDMETHODCALLTYPE V2GetPixel(Device* base,IDirect3DPixelShader9** output){
    const HRESULT result=FixtureGetPixel(base,output);auto* d=static_cast<V2Device*>(base);
    if(d->throw_pixel_get&&!--d->throw_pixel_get)throw std::runtime_error("authored PS getter after owned output");return result;
}
HRESULT STDMETHODCALLTYPE V2Target(Device* base,DWORD index,IDirect3DSurface9** output){
    const HRESULT result=Target(base,index,output);auto* d=static_cast<V2Device*>(base);
    if(d->throw_target_get){d->throw_target_get=false;throw std::runtime_error("authored RT getter after owned output");}return result;
}
HRESULT STDMETHODCALLTYPE V2Description(Surface* surface,D3DSURFACE_DESC* output){
    const HRESULT result=Description(surface,output);
    if(throw_surface_description){throw_surface_description=false;throw std::runtime_error("authored surface description exception");}return result;
}
struct WorkerDraw {IDirect3DDevice9* device;bool active=false;};
DWORD WINAPI IneligibleWorker(void* pointer){auto& work=*static_cast<WorkerDraw*>(pointer);DrawTransaction draw{};BeginDraw(work.device,&draw);work.active=draw.active;if(draw.active)EndDraw(&draw,S_OK);return 0;}
HRESULT STDMETHODCALLTYPE V2SetVertex(Device* base,IDirect3DVertexShader9* shader){
    auto* d=static_cast<V2Device*>(base);auto* object=reinterpret_cast<Shader*>(shader);
    if(object&&!object->dynamic&&d->fail_restore_vertex)return E_FAIL;
    const HRESULT result=SetVertex(base,shader);
    if(object&&object->dynamic&&d->fail_vertex){d->fail_vertex=false;return E_FAIL;}return result;
}
HRESULT STDMETHODCALLTYPE V2SetPixel(Device* base,IDirect3DPixelShader9* shader){
    auto* d=static_cast<V2Device*>(base);auto* object=reinterpret_cast<Shader*>(shader);
    if(object&&!object->dynamic&&d->fail_restore_pixel)return E_FAIL;
    const HRESULT result=FixtureSetPixel(base,shader);
    if(object&&object->dynamic&&d->throw_pixel){d->throw_pixel=false;throw std::runtime_error("authored V2 pixel setter exception");}
    if(object&&object->dynamic&&d->fail_pixel){d->fail_pixel=false;return E_FAIL;}return result;
}
HRESULT STDMETHODCALLTYPE V2CreatePixel(Device* base,const DWORD* code,IDirect3DPixelShader9** output){
    const HRESULT result=CreatePixel(base,code,output);auto* d=static_cast<V2Device*>(base);
    if(d->fail_create_pixel){d->fail_create_pixel=false;return E_FAIL;}return result;
}
void OverwriteFixture(const std::wstring& path,const std::vector<unsigned char>& bytes){
    HANDLE file=CreateFileW(path.c_str(),GENERIC_WRITE,0,nullptr,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);Verify(file!=INVALID_HANDLE_VALUE,"open authored fixture for controlled tamper");
    DWORD wrote=0;const bool valid=WriteFile(file,bytes.data(),static_cast<DWORD>(bytes.size()),&wrote,nullptr)&&wrote==bytes.size();CloseHandle(file);Verify(valid,"write authored fixture only");
}
bool WritableFixture(const std::wstring& path){HANDLE file=CreateFileW(path.c_str(),GENERIC_WRITE,0,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);if(file==INVALID_HANDLE_VALUE)return false;CloseHandle(file);return true;}
}
#ifndef S7_MATERIAL_V2_ENTRY
#define S7_MATERIAL_V2_ENTRY wmain
#endif
int S7_MATERIAL_V2_ENTRY(int argc,wchar_t** argv){using namespace MaterialShaderEffects;try{
    Verify(argc==2,"use a new private fixture path");
    Verify(LegacyFixtureMain(argc,argv)==0,"unchanged V1 regression fixture");
    const std::wstring folder=std::wstring(argv[1])+L"-v2";
    Verify(CreateDirectoryW(folder.c_str(),nullptr)!=0,"new authored V2 fixture directory");
    std::array<void*,5> shader_table{};shader_table[1]=reinterpret_cast<void*>(&ShaderAddRef);shader_table[2]=reinterpret_cast<void*>(&ShaderRelease);shader_table[4]=reinterpret_cast<void*>(&V2Function);
    std::array<void*,17> surface_table{};surface_table[1]=reinterpret_cast<void*>(&SurfaceAddRef);surface_table[2]=reinterpret_cast<void*>(&SurfaceRelease);surface_table[12]=reinterpret_cast<void*>(&V2Description);
    Surface surface{surface_table.data(),1};std::array<void*,134> table{};
    table[93]=reinterpret_cast<void*>(&V2GetVertex);table[108]=reinterpret_cast<void*>(&V2GetPixel);table[92]=reinterpret_cast<void*>(&V2SetVertex);table[107]=reinterpret_cast<void*>(&V2SetPixel);
    table[95]=reinterpret_cast<void*>(&GetVS);table[110]=reinterpret_cast<void*>(&GetPS);table[94]=reinterpret_cast<void*>(&SetVS);table[109]=reinterpret_cast<void*>(&SetPS);
    table[38]=reinterpret_cast<void*>(&V2Target);table[48]=reinterpret_cast<void*>(&Viewport);table[58]=reinterpret_cast<void*>(&RenderState);table[91]=reinterpret_cast<void*>(&CreateVertex);table[106]=reinterpret_cast<void*>(&V2CreatePixel);
    V2Device d{};d.table=table.data();d.surface=&surface;d.shader_table=shader_table.data();auto* device=reinterpret_cast<IDirect3DDevice9*>(static_cast<Device*>(&d));
    std::array<Shader,8> originals{};
    for(unsigned role=0;role<4;++role){
        originals[role*2]={shader_table.data(),1,false,Code(true,100+role)};originals[role*2+1]={shader_table.data(),1,false,Code(false,200+role)};
        const auto v=Hash(originals[role*2].code.data(),20),p=Hash(originals[role*2+1].code.data(),20);
        Verify(FixtureCompatibility(role,v.data(),p.data(),20,20,nullptr),"authored original pair pins");
    }
    std::array<std::wstring,5> paths{};std::array<std::vector<unsigned char>,5> programs{};
    for(unsigned slot=0;slot<5;++slot){
        const bool vertex=slot==0||slot>=3;programs[slot]=Code(vertex,400+slot);const auto hash=Hash(programs[slot].data(),20);
        constexpr wchar_t digits[]=L"0123456789abcdef";std::wstring name;for(unsigned char value:hash){name+=digits[value>>4];name+=digits[value&15];}
        paths[slot]=folder+L"/"+name+(vertex?L".vs.bin":L".ps.bin");Write(paths[slot],programs[slot]);
        Verify(FixtureProgramCompatibility(2,slot,hash.data(),20),"authored V2 generated pin only in fixture build");
    }
    MaterialShaderConfigV2::Config config{};config.flags=AllowedFlags;config.render_width=1920;config.render_height=1080;
    wcscpy_s(config.leaf_vertex_path,paths[0].c_str());wcscpy_s(config.bump_pixel_path,paths[1].c_str());wcscpy_s(config.parallax_pixel_path,paths[2].c_str());
    wcscpy_s(config.bump_vertex_path,paths[3].c_str());wcscpy_s(config.parallax_vertex_path,paths[4].c_str());wcscpy_s(config.report_path,(folder+L"/effects.private.json").c_str());
    Verify(!Initialize(reinterpret_cast<const Config*>(&config)),"V1 refuses V2 allocation/version");
    Config legacy{};Verify(!InitializeV2(reinterpret_cast<const MaterialShaderConfigV2::Config*>(&legacy)),"V2 refuses legacy header before full copy");
    auto invalid=config;invalid.version=1;Verify(!InitializeV2(&invalid),"wrong version refused");
    invalid=config;invalid.bump_vertex_path[0]=0;Verify(!InitializeV2(&invalid),"missing paired VS refused");
    const std::wstring unlisted=folder+L"/unlisted.vs.bin";Write(unlisted,programs[0]);invalid=config;wcscpy_s(invalid.leaf_vertex_path,unlisted.c_str());
    Verify(!InitializeV2(&invalid),"arbitrary filename refused even for matching shader content");
    const std::wstring nested=folder+L"/nested";Verify(CreateDirectoryW(nested.c_str(),nullptr)!=0,"new private nested report directory");invalid=config;wcscpy_s(invalid.report_path,(nested+L"/effects.private.json").c_str());
    Verify(!InitializeV2(&invalid),"shader paths outside trusted report root refused");
    const std::wstring report_directory=folder+L"/directory.private.json";Verify(CreateDirectoryW(report_directory.c_str(),nullptr)!=0,"authored report-directory negative fixture");invalid=config;wcscpy_s(invalid.report_path,report_directory.c_str());
    Verify(!InitializeV2(&invalid),"report directory refused before program pinning");
    auto unknown=programs[4];unknown[8]^=1;OverwriteFixture(paths[4],unknown);
    Verify(!InitializeV2(&config)&&!Snapshot().initialized_flags,"unknown generated program hash refused");
    Verify(WritableFixture(paths[0])&&WritableFixture(paths[1])&&WritableFixture(paths[2])&&WritableFixture(paths[3]),"failed late load closes all earlier program handles");OverwriteFixture(paths[4],programs[4]);
    SYSTEM_INFO system{};GetSystemInfo(&system);const size_t page=system.dwPageSize;
    auto* pages=static_cast<unsigned char*>(VirtualAlloc(nullptr,page*3,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));Verify(pages!=nullptr,"legacy-sized guarded allocation");DWORD protection=0;
    Verify(VirtualProtect(pages+page*2,page,PAGE_NOACCESS,&protection)!=0,"legacy trailing guard page");
    auto* old_buffer=pages+page*2-sizeof(legacy);std::memcpy(old_buffer,&legacy,sizeof(legacy));
    Verify(!InitializeV2(reinterpret_cast<const MaterialShaderConfigV2::Config*>(old_buffer)),"V2 rejects old-sized allocation without reading trailing guard");Verify(VirtualFree(pages,0,MEM_RELEASE)!=0,"legacy guard allocation released");
    SetLastError(0x37572001);Verify(InitializeV2(&config),"explicit V2 initialized");Verify(GetLastError()==0x37572001,"V2 init preserves LastError");Verify(Snapshot().config_version==2,"V2 receipt identifies recipe version");
    auto Bind=[&](unsigned role){SetVertex(&d,reinterpret_cast<IDirect3DVertexShader9*>(&originals[role*2]));FixtureSetPixel(&d,reinterpret_cast<IDirect3DPixelShader9*>(&originals[role*2+1]));d.blend=role==1||role==2?1u:0u;
        d.vs[68]={.8333333f,.84f,.8266667f,.8333333f};d.vs[69]={.6333333f,.66f,.64f,.66f};d.ps[33]={role==2?15.0f:1.0f,.5f,.02f,role==2?.2f:.25f};};
    auto Draw=[&](unsigned role,HRESULT result=S_OK){Bind(role);const auto fog=d.vs[68],reflection=d.vs[69],detail=d.ps[33];DrawTransaction draw{};
        SetLastError(0x37572002);BeginDraw(device,&draw);Verify(GetLastError()==0x37572002,"V2 begin preserves LastError");Verify(draw.active,"known exact pair activates V2");
        if(role==1||role==2)Verify(d.vertex!=&originals[role*2]&&d.pixel!=&originals[role*2+1]&&draw.touched_vertex&&draw.touched_pixel,"terrain replaces both VS and PS");
        if(role==3)Verify(d.vertex!=&originals[role*2]&&d.pixel==&originals[role*2+1],"leaf changes only VS");
        if(role==0)Verify(d.vertex==&originals[0]&&d.pixel==&originals[1],"legacy river policy changes no shader object");
        EndDraw(&draw,result);Verify(!draw.unsafe_state&&!draw.active,"draw-local V2 restoration proven");
        Verify(d.vertex==&originals[role*2]&&d.pixel==&originals[role*2+1]&&d.vs[68]==fog&&d.vs[69]==reflection&&d.ps[33]==detail,"both original stages and all components restored");};
    Shader worker_vertex{shader_table.data(),1,false,Code(true,9000)},worker_pixel{shader_table.data(),1,false,Code(false,9001)};
    worker_vertex.code.insert(worker_vertex.code.end()-4,4,0);worker_pixel.code.insert(worker_pixel.code.end()-4,4,0);
    SetVertex(&d,reinterpret_cast<IDirect3DVertexShader9*>(&worker_vertex));FixtureSetPixel(&d,reinterpret_cast<IDirect3DPixelShader9*>(&worker_pixel));
    const auto bodies_before_worker=function_body_reads;WorkerDraw work{device};HANDLE thread=CreateThread(nullptr,0,IneligibleWorker,&work,0,nullptr);
    Verify(thread!=nullptr&&WaitForSingleObject(thread,10000)==WAIT_OBJECT_0,"first unrelated worker draw completed");CloseHandle(thread);
    Verify(!work.active&&!Snapshot().cached_refs&&!Snapshot().active_original_refs&&function_body_reads==bodies_before_worker,"wrong-size first worker draws neither hash code nor claim/cache ownership");
    for(unsigned role=0;role<4;++role)Draw(role,role==2?E_FAIL:S_OK);
    Verify(worker_vertex.refs==1&&worker_pixel.refs==1,"unrelated worker query refs balanced; legitimate main-thread owner accepted");
    EndFrame(device);Verify(live_created==5&&Snapshot().retained_programs==5&&Snapshot().peak_retained_programs==5,"five generated objects retained, original frame cache cleared");
    Verify(!Snapshot().cached_refs&&Snapshot().shader_creations==5,"one initial creation per generated program");
    for(unsigned frame=0;frame<8;++frame){for(unsigned role=1;role<4;++role)Draw(role);EndFrame(device);Verify(Snapshot().shader_creations==5&&live_created==5,"eight warm V2 frames create no new objects");}
    for(auto count:Snapshot().program_creations)Verify(count==1,"per-program creation evidence");
    // 257 wrong hashes with the exact eligible sizes must never starve a valid
    // material that arrives later in the same frame.
    std::vector<Shader> wrong(514);
    for(unsigned i=0;i<257;++i){wrong[i*2]={shader_table.data(),1,false,Code(true,7000+i)};wrong[i*2+1]={shader_table.data(),1,false,Code(false,8000+i)};
        SetVertex(&d,reinterpret_cast<IDirect3DVertexShader9*>(&wrong[i*2]));FixtureSetPixel(&d,reinterpret_cast<IDirect3DPixelShader9*>(&wrong[i*2+1]));DrawTransaction draw{};BeginDraw(device,&draw);
        Verify(!draw.active&&!draw.unsafe_state&&!Snapshot().cached_refs&&!Snapshot().active_original_refs,"same-size wrong identities are not cached");}
    Draw(1);EndFrame(device);for(const auto& shader:wrong)Verify(shader.refs==1,"negative identity query refs balanced");
    // Saturate with positive identities, then drop all external references to a
    // new pair. The transaction's own refs must keep originals alive while the
    // device temporarily binds generated shaders.
    std::vector<Shader> positives(256);
    for(unsigned i=0;i<128;++i){positives[i*2]={shader_table.data(),1,false,Code(true,101)};positives[i*2+1]={shader_table.data(),1,false,Code(false,201)};
        Bind(1);SetVertex(&d,reinterpret_cast<IDirect3DVertexShader9*>(&positives[i*2]));FixtureSetPixel(&d,reinterpret_cast<IDirect3DPixelShader9*>(&positives[i*2+1]));DrawTransaction draw{};
        BeginDraw(device,&draw);Verify(draw.active,"positive cache fill draw");EndDraw(&draw,S_OK);}
    Verify(Snapshot().cached_refs==256,"positive frame cache has fixed capacity256");
    const auto retained_before=live_created;
    auto* fresh_vertex=new Shader{shader_table.data(),1,true,Code(true,101)};auto* fresh_pixel=new Shader{shader_table.data(),1,true,Code(false,201)};live_created+=2;
    SetVertex(&d,reinterpret_cast<IDirect3DVertexShader9*>(fresh_vertex));FixtureSetPixel(&d,reinterpret_cast<IDirect3DPixelShader9*>(fresh_pixel));ShaderRelease(fresh_vertex);ShaderRelease(fresh_pixel);
    DrawTransaction spill{};BeginDraw(device,&spill);
    Verify(spill.active&&Snapshot().cached_refs==256&&Snapshot().active_original_refs==2&&live_created==retained_before+2,"full positive cache still matches and owns uncached originals across replacement");
    EndDraw(&spill,S_OK);Verify(!spill.unsafe_state&&!Snapshot().active_original_refs&&d.vertex==fresh_vertex&&d.pixel==fresh_pixel&&fresh_vertex->refs==1&&fresh_pixel->refs==1,"uncached originals restored before active refs released");
    EndFrame(device);Bind(1);Verify(live_created==retained_before,"uncached originals die only after device intentionally changes original binding");
    for(const auto& shader:positives)Verify(shader.refs==1,"positive frame refs balanced");
    Bind(1);d.fail_vertex=true;DrawTransaction failed{};BeginDraw(device,&failed);Verify(!failed.active&&!failed.unsafe_state&&d.vertex==&originals[2]&&d.pixel==&originals[3],"failed VS setter after mutation restores original before draw");
    d.fail_pixel=true;BeginDraw(device,&failed);Verify(!failed.active&&!failed.unsafe_state&&d.vertex==&originals[2]&&d.pixel==&originals[3],"failed PS setter after VS and PS mutation restores both");
    const auto detail=d.ps[33];d.fail_set=1;BeginDraw(device,&failed);Verify(!failed.active&&!failed.unsafe_state&&d.ps[33]==detail&&d.vertex==&originals[2]&&d.pixel==&originals[3],"failed material constant restores both shader stages too");
    Reset(device);Verify(!live_created&&!Snapshot().retained_programs,"Reset/external Release clears all five device programs");Draw(1);Verify(Snapshot().shader_creations==7,"paired terrain recreated lazily after Reset");
    EndFrame(device);Verify(SetEnabled(0)&&!live_created&&!Snapshot().retained_programs,"disable drops all retained programs");
    DrawTransaction off{};BeginDraw(device,&off);Verify(!off.active,"disabled V2 preserves originals");Verify(SetEnabled(Terrain|Leaf),"subset reenable retains pinned CPU programs");Draw(2);Draw(3);EndFrame(device);
    Shutdown();Verify(!live_created&&!Snapshot().initialized_flags,"V2 Shutdown permits fresh initialization");Verify(InitializeV2(&config),"V2 reinitializes after full shutdown");
    Bind(1);d.throw_pixel=true;DrawTransaction exceptional{};BeginDraw(device,&exceptional);
    Verify(!exceptional.active&&!exceptional.unsafe_state&&Snapshot().faulted&&d.vertex==&originals[2]&&d.pixel==&originals[3],"exception after both binds rolls back both stages");
    Reset(device);Shutdown();Verify(InitializeV2(&config),"fresh V2 after expected exception fault");
    Bind(2);DrawTransaction unsafe{};BeginDraw(device,&unsafe);Verify(unsafe.active,"prepare paired restoration failure");d.fail_restore_vertex=true;d.fail_restore_pixel=true;EndDraw(&unsafe,S_OK);
    Verify(unsafe.unsafe_state&&Snapshot().faulted,"paired restore failure explicitly unsafe");const auto refs=Snapshot().cached_refs,retained=Snapshot().retained_programs;
    EndFrame(device);Reset(device);Shutdown();Verify(Snapshot().cached_refs==refs&&Snapshot().retained_programs==retained&&Snapshot().initialized_flags,"unsafe lifecycle refuses dropping borrowed originals and replacement refs");
    Verify(!SetEnabled(0),"unsafe state cannot be hidden by a disable");d.fail_restore_vertex=false;d.fail_restore_pixel=false;
    Verify(RetryRestore(&unsafe)&&!unsafe.unsafe_state&&d.vertex==&originals[4]&&d.pixel==&originals[5],"immediate same-callback retry restores both original stages");
    Reset(device);Shutdown();Verify(!live_created,"unsafe retry recovery then cleanup balances replacement refs");
    Verify(InitializeV2(&config),"prepare creation failure");Bind(2);d.fail_create_pixel=true;DrawTransaction create_failed{};BeginDraw(device,&create_failed);
    Verify(!create_failed.active&&!create_failed.unsafe_state&&Snapshot().shader_failures==1&&Snapshot().faulted,"failed PS creation faults without partial binding");
    Verify(d.vertex==&originals[4]&&d.pixel==&originals[5]&&live_created==1,"failed returned PS released, prepared VS remains bounded and unbound");
    Reset(device);Shutdown();
    Verify(InitializeV2(&config),"prepare nested-transaction ownership refusal");Bind(1);DrawTransaction owner{};BeginDraw(device,&owner);Verify(owner.active,"own transaction active");
    auto copied=owner;EndDraw(&copied,S_OK);Verify(copied.unsafe_state&&Snapshot().active_transactions==1,"copied transaction cannot consume real owner's backup");
    const auto* saved_vertex=owner.original_vertex;const auto* saved_pixel=owner.original_pixel;
    BeginDraw(device,&owner);Verify(owner.active&&owner.unsafe_state&&owner.original_vertex==saved_vertex&&owner.original_pixel==saved_pixel,"nested Begin cannot erase outstanding recovery state");
    Verify(RetryRestore(&owner)&&!owner.unsafe_state&&!owner.active&&d.vertex==&originals[2]&&d.pixel==&originals[3],"original owner still recovers both stages after illegal nested begin");
    Verify(!RetryRestore(&owner),"completed transaction cannot restore stale borrowed references later");Reset(device);Shutdown();
    for(unsigned failure=0;failure<5;++failure){Verify(InitializeV2(&config),"fresh getter exception case");Bind(1);
        const auto vertex_refs=originals[2].refs,pixel_refs=originals[3].refs,surface_refs=surface.refs;
        if(failure==0)d.throw_vertex_get=true;else if(failure==1)d.throw_pixel_get=1;
        else if(failure==2)throw_function_body=true;else if(failure==3)d.throw_target_get=true;else throw_surface_description=true;
        DrawTransaction query{};BeginDraw(device,&query);
        Verify(!query.active&&!query.unsafe_state&&Snapshot().faulted&&!Snapshot().active_original_refs,"throwing initial query never mutates GPU state or retains active originals");
        Verify(originals[2].refs==vertex_refs&&originals[3].refs==pixel_refs&&surface.refs==surface_refs,"getter/GetFunction/surface exception after output releases every acquired reference");
        Reset(device);Shutdown();}
    for(unsigned failure=0;failure<2;++failure){Verify(InitializeV2(&config),"fresh restoration readback exception case");Bind(1);
        const auto vertex_refs=originals[2].refs,pixel_refs=originals[3].refs;DrawTransaction query{};BeginDraw(device,&query);Verify(query.active,"readback exception starts from active paired draw");
        if(failure==0)d.throw_vertex_get=true;else d.throw_pixel_get=1;EndDraw(&query,S_OK);
        Verify(query.unsafe_state&&Snapshot().active_original_refs==2&&originals[2].refs==vertex_refs+2&&originals[3].refs==pixel_refs+2,"throwing readback releases temporary output ref while retaining recovery originals");
        Verify(RetryRestore(&query)&&!Snapshot().active_original_refs,"readback retry proves restoration before releasing recovery ownership");
        EndFrame(device);Verify(originals[2].refs==vertex_refs&&originals[3].refs==pixel_refs,"readback exception and retry leave no COM leak");Reset(device);Shutdown();}
    Config again{};again.flags=AllowedFlags;again.render_width=1920;again.render_height=1080;
    const std::wstring legacy_folder=argv[1];wcscpy_s(again.leaf_vertex_path,(legacy_folder+L"/leaf.bin").c_str());
    wcscpy_s(again.bump_pixel_path,(legacy_folder+L"/bump.bin").c_str());wcscpy_s(again.parallax_pixel_path,(legacy_folder+L"/parallax.bin").c_str());
    wcscpy_s(again.report_path,(folder+L"/legacy-return.private.json").c_str());
    Verify(Initialize(&again)&&Snapshot().config_version==1,"legacy V1 reinitializes after V2 without recipe upgrade");
    Bind(1);DrawTransaction old_draw{};BeginDraw(device,&old_draw);
    Verify(old_draw.active&&!old_draw.touched_vertex&&old_draw.touched_pixel&&d.vertex==&originals[2],"V1 terrain still preserves original VS");
    EndDraw(&old_draw,S_OK);Verify(!old_draw.unsafe_state&&d.vertex==&originals[2]&&d.pixel==&originals[3],"V1 draw restores after V2 transition");
    EndFrame(device);Verify(Snapshot().retained_programs==1&&Snapshot().program_creations[3]==0&&Snapshot().program_creations[4]==0,"V1 does not retain V2 terrain VS programs");
    Reset(device);Shutdown();SetVertex(&d,nullptr);FixtureSetPixel(&d,nullptr);
    for(const auto& shader:originals)Verify(shader.refs==1,"V2 original COM references balanced");Verify(surface.refs==1&&!live_created,"all V2 COM references balanced");
    std::printf("V1 regression + V2 CPU transactions: %u V2 checks passed; no GPU/game started\n",v2_checks);return 0;
}catch(const std::exception& error){std::fprintf(stderr,"material V2 fixture failed: %s\n",error.what());return 1;}}
