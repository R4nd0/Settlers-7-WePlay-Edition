#include "on12_motion_pose.h"
#include <cstdio>
#include <thread>
#include <d3dcompiler.h>
namespace {
unsigned checks=0;bool Check(bool value,unsigned line){++checks;if(!value)std::fprintf(stderr,"motion pose check failed at %u\n",line);return value;}
#define REQUIRE(x) do{if(!Check(!!(x),__LINE__))return 20;}while(false)
}
int main(){
    using namespace On12Graphics;MotionPoseProbe probe;probe.Initialize();MotionPoseCommand command;command.sequence=1;command.frames=2;
    REQUIRE(probe.Arm(command));probe.Upload(68,reinterpret_cast<const float*>(0x123400),4,0x567800);probe.Upload(80,reinterpret_cast<const float*>(0x987600),48,0x567900);probe.Begin(10);
    MotionPoseRecord key;key.kind=2;key.primitives=3;
    unsigned reads=0;auto read=[&](MotionPoseRecord& record){++reads;record.constants[68*4]=static_cast<float>(reads);record.constants[80*4]=.5f;return true;};
    for(UINT n=0;n<128;++n)REQUIRE(probe.Observe(key,read));
    REQUIRE(!probe.Observe(key,read)&&reads==128&&probe.End()&&probe.Captured()==1);
    REQUIRE(probe.Records()[0].source==0x123400&&probe.Records()[0].caller==0x567800&&probe.Records()[0].world_hash!=probe.Records()[1].world_hash);
    REQUIRE(probe.Records()[0].bone_source==0x987600&&probe.Records()[0].bone_caller==0x567900);
    probe.Begin(11);probe.Upload(69,reinterpret_cast<const float*>(0x222200),1,0x567800);
    probe.Upload(128,reinterpret_cast<const float*>(0x987900),30,0x567900);
    REQUIRE(probe.Observe(key,read)&&probe.Records().back().source==0&&probe.Records().back().bone_source==0x987600);
    probe.Upload(140,reinterpret_cast<const float*>(0x111100),10,0x567900);
    REQUIRE(probe.Observe(key,read)&&probe.Records().back().bone_source==0&&probe.End()&&!probe.Active());
    const auto header=probe.Header();REQUIRE(header.frames==2&&header.records==130&&header.record_bytes==4588&&header.requested==2);
    REQUIRE(!probe.Arm(command));command.sequence=2;command.frames=5;REQUIRE(!probe.Arm(command));command.frames=2;REQUIRE(probe.Arm(command));
    bool foreign=true;std::thread other([&]{foreign=probe.Arm(MotionPoseCommand{16,1,3,2});});other.join();REQUIRE(!foreign);
    probe.Begin(12);std::thread mutation([&]{probe.ForeignUpdate();});mutation.join();REQUIRE(!probe.CanObserve()&&!probe.Observe(key,read));
    probe.Stop();REQUIRE(!probe.Active());
    const char* shader="float4x4 g_VSMainViewProjectionMatrix:register(c12);float4x4 g_WorldMatrix:register(c68);float4 g_SkinningMatrixArray[150]:register(c80);float4 VS(float4 p:POSITION0):POSITION0{return mul(g_VSMainViewProjectionMatrix,mul(g_WorldMatrix,p+g_SkinningMatrixArray[(int)p.w]));}";
    Microsoft::WRL::ComPtr<ID3DBlob> code,error;
    REQUIRE(SUCCEEDED(D3DCompile(shader,std::strlen(shader),"authored-skin-pose",nullptr,nullptr,"VS","vs_3_0",D3DCOMPILE_OPTIMIZATION_LEVEL3,0,&code,&error)));
    REQUIRE(SkinningPoseProgram(code->GetBufferPointer(),code->GetBufferSize())&&!SkinningPoseProgram(code->GetBufferPointer(),code->GetBufferSize()-4));
    std::printf("{\"passed\":true,\"checks\":%u,\"cpu_only\":true,\"gpu_device_created\":false,\"identity_verified\":false,\"bounded_records_per_frame\":128,\"record_bytes\":4588}\n",checks);return 0;
}
