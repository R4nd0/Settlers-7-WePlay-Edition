// Synthetic x86 getter/ABI checks. No game, module identity or GPU is exercised.
#include <windows.h>
#include <cfenv>
#include <cmath>
#include <cstdio>
#include <limits>
#include <intrin.h>
#include "graphics_observation.h"

namespace {
int object=1,context=2,fault=0;
void* __cdecl Main(){return fault==1?nullptr:&context;}
void* __fastcall FakeCamera(void*,void*){return fault==2?nullptr:&object;}
void __fastcall World(void*,void*,void* output) {
    if(fault==5){*static_cast<volatile int*>(nullptr)=1;return;}
    auto* values=static_cast<float*>(output);
    for(int i=0;i<(fault==9?15:16);++i)values[i]=fault==3?0.f:((i%5)==0?1.f:0.f);
    if(fault==4)values[16]=0; // Corrupt only the caller's adjacent canary.
}
void __fastcall Fov(void*,void*,float* x,float* y){SetLastError(0x6501432);*x=fault==6?std::numeric_limits<float>::quiet_NaN():60.f;*y=40.f;}
void __fastcall Clip(void*,void*,float* near_value,float* far_value){*near_value=1.f;*far_value=fault==7?.5f:1000.f;}
void __fastcall FakeSize(void*,void*,int* width,int* height){*width=fault==8?0:1920;*height=1080;}
}

int main() {
    using namespace GraphicsObservation;
    unsigned failures=0,checks=0;
    const auto check=[&](bool okay,const char* name){++checks;if(!okay){++failures;std::fprintf(stderr,"failed: %s\n",name);}};
    SetLastError(0x7a53b12);
    check(!Initialize() && GetLastError()==0x7a53b12,"unknown-host Initialize preserves LastError");
    Snapshot value{};
    check(!Read(value) && value.status==NotInitialized && value.available==0,"uninitialized reader");
    // Fastcall mock ECX/EDX parameters reproduce the actual x86 thiscall ECX
    // object and stack-reference convention; the unused EDX is intentional.
#pragma warning(push)
#pragma warning(disable:4191)
    FixtureGetters getters{Main,reinterpret_cast<GraphicsObservation::Camera>(FakeCamera),
        reinterpret_cast<Matrix>(World),reinterpret_cast<Floats>(Fov),reinterpret_cast<Floats>(Clip),
        reinterpret_cast<GraphicsObservation::Size>(FakeSize)};
#pragma warning(pop)
    InitializeFixture(getters);
    SetLastError(0x7139ad2);
    check(Read(value),"actual getter calling conventions");
    check(GetLastError()==0x7139ad2,"diagnostic getter calls preserve LastError");
    check(value.size==sizeof(Snapshot) && value.version==1,"snapshot layout");
    check(value.available==(CameraReady|ContextSizeReady),"unmapped measurements remain unavailable");
    check(value.context_width==1920 && value.context_height==1080,"context size references");
    check(value.camera_world[0]==1 && value.camera_world[15]==1 && value.camera_world[1]==0,"64-byte camera matrix");
    check(value.fov_native[0]==60 && value.fov_native[1]==40 && value.clip_native[1]==1000,"FOV and clip references");
    const int old_round=std::fegetround();const unsigned old_mxcsr=_mm_getcsr();
    std::fesetround(FE_DOWNWARD);const unsigned chosen_mxcsr=_mm_getcsr();
    check(Read(value) && std::fegetround()==FE_DOWNWARD && _mm_getcsr()==chosen_mxcsr,"FPU/SSE restoration");
    std::fesetround(old_round);_mm_setcsr(old_mxcsr);
    for(fault=1;fault<=8;++fault) {
        SetLastError(0x6e56a43);
        check(!Read(value) && value.available==0,"invalid getter output rejected");
        check(GetLastError()==0x6e56a43,"unavailable/fault getter preserves LastError");
        check(std::isfinite(value.fov_native[0]) && value.camera_world[0]==0,"partial invalid measurements not published");
    }
    fault=9;check(Read(value) && value.camera_world[15]==1,"original partial-matrix initialization convention");
    fault=0;check(Read(value),"valid reader recovers after unavailable state");
    std::printf("graphics_observation CPU fixtures: %u checks, %u failures; no game tested\n",checks,failures);
    return failures?1:0;
}
