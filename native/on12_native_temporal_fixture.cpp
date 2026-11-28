#include "on12_native_temporal.h"
#include "on12_scene_projection.h"
#include <d3dcompiler.h>
#include <wrl/client.h>
#include <cmath>
#include <cstdio>
#include <limits>
#include <thread>
#include <vector>

using namespace On12NativeTemporal;
namespace {unsigned checks=0,dirty=0;bool Check(bool good,unsigned line){++checks;if(!good)std::fprintf(stderr,"native jitter check failed at %u\n",line);return good;}
#define REQUIRE(x) do{if(!Check(!!(x),__LINE__))return 20;}while(false)
void Dirty(void* cookie){if(cookie==&dirty)++dirty;}
}
int wmain(int argc,wchar_t** argv){
    if(argc==3&&!wcscmp(argv[1],L"--classify")){
        FILE* file=nullptr;if(_wfopen_s(&file,argv[2],L"rb")||!file)return 2;
        struct Close{FILE* f;~Close(){std::fclose(f);}} close{file};
        char magic[8]{};DWORD count=0;
        if(std::fread(magic,1,8,file)!=8||std::memcmp(magic,"S7VSCLS1",8)||std::fread(&count,4,1,file)!=1||!count||count>2048)return 2;
        std::printf("{\"cpu_only\":true,\"classifications\":[");
        for(DWORD i=0;i<count;++i){DWORD size=0;if(std::fread(&size,4,1,file)!=1||size<16||size>65536||size%4)return 2;
            std::vector<unsigned char> code(size);if(std::fread(code.data(),1,size,file)!=size)return 2;
            if(i)std::putchar(',');std::fputs(On12Graphics::SceneProjectionShader(code.data(),code.size())?"true":"false",stdout);}
        if(std::fgetc(file)!=EOF)return 2;std::puts("]}");return 0;
    }
    if(argc!=1)return 2;
    void* key=reinterpret_cast<void*>(static_cast<std::uintptr_t>(0x123400));
    State state;REQUIRE(state.Register(key,&dirty,Dirty));
    Control c;c.device12=reinterpret_cast<std::uintptr_t>(key);c.sequence=1;c.flags=1;c.width=1280;c.height=720;c.jitter_x=0.25f;c.jitter_y=-0.375f;
    REQUIRE(!Configure(c)&&!dirty);state.ObserveOwner();REQUIRE(Configure(c)&&dirty==1&&state.Enabled());
    float x=1.0f/1280,y=-1.0f/720;REQUIRE(state.Adjust(true,1280,720,x,y));
    REQUIRE(std::fabs(x-1.5f/1280)<1e-9f&&std::fabs(y+0.25f/720)<1e-9f);
    x=1.0f/1280;y=-1.0f/720;const float original_x=x,original_y=y;
    Control non_scene;non_scene.device12=c.device12;non_scene.sequence=2;REQUIRE(Configure(non_scene)&&dirty==2);
    REQUIRE(!state.Adjust(false,1280,720,x,y)&&x==original_x&&y==original_y); // Explicit UI/postprocessing scope.
    c.sequence=3;REQUIRE(Configure(c)&&dirty==3);
    REQUIRE(!state.Adjust(true,512,512,x,y)&&x==original_x&&y==original_y); // Shadow viewport.
    REQUIRE(!Configure(c)&&dirty==3);Query q;q.device12=c.device12;REQUIRE(Snapshot(q)&&q.sequence==3&&q.shifted_resolves==1&&q.configurations==3);
    REQUIRE(state.Adjust(false,1280,720,x,y)); // Trusted app scope still works if driver metadata was stripped.
    bool foreign=false;std::thread other([&]{auto test=c;test.sequence=4;foreign=Configure(test);float a=1,b=-1;if(state.Adjust(true,1280,720,a,b))foreign=true;});other.join();REQUIRE(!foreign&&dirty==3);
    for(unsigned test=0;test<5;++test){auto bad=c;bad.sequence=4;if(test==0)bad.jitter_x=0.6f;if(test==1)bad.jitter_y=(std::numeric_limits<float>::quiet_NaN)();if(test==2)bad.width=0;if(test==3)bad.flags=2;if(test==4)bad.device12=0;
        REQUIRE(!Configure(bad)&&dirty==3);}
    {State ambiguous;REQUIRE(ambiguous.Register(key,&dirty,Dirty));ambiguous.ObserveOwner();auto next=c;next.sequence=4;REQUIRE(!Configure(next)&&dirty==3);}
    Control off;off.device12=c.device12;off.sequence=4;REQUIRE(Configure(off)&&dirty==4&&!state.Enabled());
    x=original_x;y=original_y;REQUIRE(!state.Adjust(true,1280,720,x,y)&&x==original_x&&y==original_y);
    c.sequence=5;REQUIRE(Configure(c)&&dirty==5);REQUIRE(!state.Register(key,&dirty,Dirty));
    auto obsolete=c;obsolete.version=1;obsolete.sequence=6;REQUIRE(!Configure(obsolete));
    const char* source="float4x4 g_VSMainViewProjectionMatrix:register(c12);float4 VS(float4 p:POSITION0):POSITION0{return mul(g_VSMainViewProjectionMatrix,p);}";
    Microsoft::WRL::ComPtr<ID3DBlob> program,error;
    REQUIRE(SUCCEEDED(D3DCompile(source,std::strlen(source),"authored-native-jitter",nullptr,nullptr,"VS","vs_3_0",D3DCOMPILE_OPTIMIZATION_LEVEL3,0,&program,&error)));
    REQUIRE(On12Graphics::SceneProjectionShader(program->GetBufferPointer(),program->GetBufferSize()));
    REQUIRE(!On12Graphics::SceneProjectionShader(program->GetBufferPointer(),program->GetBufferSize()-4));
    REQUIRE(!On12Graphics::SceneProjectionShader(nullptr,0));
    std::printf("{\"passed\":true,\"checks\":%u,\"cpu_only\":true,\"gpu_device_created\":false,\"control_version\":2,\"control_bytes\":40,\"query_bytes\":96}\n",checks);return 0;
}
