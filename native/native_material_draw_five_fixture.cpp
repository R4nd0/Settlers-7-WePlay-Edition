#include "native_material_draw.h"
#include <algorithm>
#include <array>
#include <cstdio>
#include <float.h>
#include <intrin.h>
#include <vector>

namespace {
unsigned checks=0;std::vector<int> events;
std::array<bool,6> begin_bad{},end_bad{},retry_recovers{},retry_lies{};
bool end_clears=false;
unsigned short original_post_control=0;
unsigned original_post_mxcsr=0;
DWORD original_post_error=0;
void Check(bool condition,const char* label){++checks;if(!condition){std::fprintf(stderr,"FAIL: %s\n",label);ExitProcess(1);}}
struct FpErrorScope {
    alignas(16) unsigned char fp[512];DWORD error=GetLastError();
    FpErrorScope(){_fxsave(fp);}
    ~FpErrorScope(){_fxrstor(fp);SetLastError(error);}
};
unsigned short Control(){unsigned short value=0;__asm {fnstcw value}return value;}
void ChangeFp(unsigned rounding,DWORD error){unsigned ignored=0;_controlfp_s(&ignored,rounding,_MCW_RC);SetLastError(error);}
void Reset(){events.clear();begin_bad={};end_bad={};retry_recovers={};retry_lies={};end_clears=false;}
template<int Stage,class Transaction> struct Fake {
    static void Begin(IDirect3DDevice9*,Transaction* transaction){
        FpErrorScope preserve;ChangeFp(_RC_DOWN,8100+Stage);events.push_back(Stage*10+1);
        transaction->unsafe_state=begin_bad[Stage];
        if constexpr(Stage==2)transaction->role=4;
    }
    static void End(Transaction* transaction,HRESULT){
        FpErrorScope preserve;ChangeFp(_RC_UP,8200+Stage);events.push_back(Stage*10+2);
        if(end_clears)transaction->unsafe_state=false;
        else transaction->unsafe_state=transaction->unsafe_state||end_bad[Stage];
    }
    static bool Retry(Transaction* transaction){
        FpErrorScope preserve;ChangeFp(_RC_CHOP,8300+Stage);events.push_back(Stage*10+3);
        if(retry_recovers[Stage])transaction->unsafe_state=false;
        return retry_recovers[Stage]||retry_lies[Stage];
    }
};
using Comparison=Fake<1,MaterialComparison::Transaction>;
using Materials=Fake<2,MaterialShaderEffects::DrawTransaction>;
using Water=Fake<3,WaterShaderEffects::DrawTransaction>;
using Shadow=Fake<4,ShadowFilterEffects::DrawTransaction>;
using Ambient=Fake<5,ShadowFilterEffects::DrawTransaction>;
auto Run(unsigned mask,HRESULT hr=S_OK,bool throws=false){
    return NativeMaterialDraw::ExecuteFive<Ambient,Comparison,Materials,Water,Shadow>(nullptr,
        (mask&1)!=0,(mask&2)!=0,(mask&4)!=0,(mask&8)!=0,(mask&16)!=0,
        [=]()->HRESULT{
            events.push_back(70);ChangeFp(_RC_UP,0x7357);original_post_control=Control();
            original_post_mxcsr=_mm_getcsr();original_post_error=GetLastError();
            if(throws)throw 1;return hr;
        },[=](HRESULT result){
            FpErrorScope preserve;
            Check(result==(throws?D3DERR_DRIVERINTERNALERROR:hr),"observer retains original HRESULT");
            Check(Control()==original_post_control&&_mm_getcsr()==original_post_mxcsr
                &&GetLastError()==original_post_error,"observer sees post-original FP/LastError");
            ChangeFp(_RC_DOWN,0xbad);events.push_back(80);
        });
}
bool Event(int event){return std::find(events.begin(),events.end(),event)!=events.end();}
void Attribution(const NativeMaterialDraw::Result& result,int stage){
    Check(result.comparison_unsafe==(stage==1),"comparison unsafe attribution");
    Check(result.material_unsafe==(stage==2),"material unsafe attribution");
    Check(result.water_unsafe==(stage==3),"water unsafe attribution");
    Check(result.shadow_unsafe==(stage==4),"shadow unsafe attribution");
    Check(result.ambient_unsafe==(stage==5),"ambient unsafe attribution");
}
}
int main(){
    FpErrorScope restore_test_environment;
    for(unsigned mask=0;mask<32;++mask)for(int mode=0;mode<3;++mode){
        Reset();const HRESULT hr=mode==1?D3DERR_INVALIDCALL:S_OK;
        const auto result=Run(mask,hr,mode==2);
        std::vector<int> expected;
        if(mask&16)expected.push_back(51);
        for(int stage=1;stage<=4;++stage)if(mask&(1u<<(stage-1)))expected.push_back(stage*10+1);
        expected.push_back(70);
        for(int stage=4;stage>=1;--stage)if(mask&(1u<<(stage-1)))expected.push_back(stage*10+2);
        if(mask&16)expected.push_back(52);
        expected.push_back(80);
        Check(events==expected,"all32 masks nest and restore before observer");
        Check(result.original_called&&result.observer_called&&!result.unsafe,"safe original and observer called once");
        Check(result.CallerResult()==(mode==2?D3DERR_DRIVERINTERNALERROR:hr),"caller original HRESULT preserved");
        Check(result.exception==(mode==2),"original C++ exception recorded");
        Check(result.role==((mask&2)?4u:0u),"material role survives added shadow stage");
        Check(Control()==original_post_control&&_mm_getcsr()==original_post_mxcsr
            &&GetLastError()==original_post_error,"post-original FP/LastError survives all callbacks");
        Attribution(result,0);
    }
    for(int stage=1;stage<=5;++stage)for(int phase=1;phase<=2;++phase)for(int recovery=0;recovery<3;++recovery){
        Reset();(phase==1?begin_bad:end_bad)[stage]=true;
        retry_recovers[stage]=recovery==1;retry_lies[stage]=recovery==2;
        const auto result=Run(31);
        const bool safe=recovery==1;
        Check(result.unsafe==!safe,"failed or lying retry stays unsafe");
        Check(result.original_called==(phase==2||safe),"unsafe Begin cannot issue original draw");
        Check(result.observer_called==safe,"unsafe callback blocks original observer");
        Check(result.CallerResult()==(safe?S_OK:D3DERR_DEVICELOST),"unsafe caller result device-lost");
        Check(Event(stage*10+3),"unsafe stage attempts immediate retry");
        if(stage!=5)Check(Event(52),"outer AO always unwinds despite inner failure");
        if(stage!=5)for(int outer=1;outer<stage;++outer)Check(Event(outer*10+2),"inner outer transactions unwind despite failure");
        for(int inner=1;inner<=4;++inner)if(stage==5||inner>stage)Check(Event(inner*10+1)==(phase==2||safe),"failed Begin stops later stages");
        Attribution(result,safe?0:stage);
    }
    for(int stage=1;stage<=5;++stage){
        Reset();begin_bad[stage]=true;end_clears=true;
        const auto result=Run(31);
        Check(result.unsafe&&!result.original_called&&!result.observer_called,"unsafe sticky after later End recovery");
        Check(result.CallerResult()==D3DERR_DEVICELOST,"sticky unsafe cannot claim original fallback");
        Attribution(result,stage);
    }
    Reset();end_bad[1]=end_bad[2]=end_bad[3]=end_bad[4]=end_bad[5]=true;
    const auto combined=Run(31,D3DERR_INVALIDCALL);
    Check(combined.unsafe&&combined.comparison_unsafe&&combined.material_unsafe&&combined.water_unsafe&&combined.shadow_unsafe&&combined.ambient_unsafe,
        "independent multiple unsafe attributions survive unwind");
    Check(Event(42)&&Event(32)&&Event(22)&&Event(12)&&Event(52)&&!Event(80),"all four Ends still run after failed original and multiple restore faults");
    Reset();end_bad[4]=true;
    const auto thrown=Run(31,S_OK,true);
    Check(thrown.exception&&thrown.shadow_unsafe&&thrown.original_result==D3DERR_DRIVERINTERNALERROR,
        "shadow unsafe preserves throwing original diagnostic result");
    Check(Event(42)&&Event(32)&&Event(22)&&Event(12)&&Event(52)&&!Event(80),"throwing original plus failed shadow still unwinds all outer stages");
    Reset();
    const auto legacy=NativeMaterialDraw::Execute<Comparison,Materials,Water>(nullptr,true,true,true,
        []()->HRESULT{events.push_back(70);return S_OK;},[](HRESULT){events.push_back(80);});
    Check(events==std::vector<int>({11,21,31,70,32,22,12,80}),"old API delegates without starting shadow");
    Check(legacy.original_called&&legacy.observer_called&&!legacy.unsafe&&!legacy.shadow_unsafe,"old caller result remains safe");
    std::printf("PASS: %u CPU five-stage protocol checks; no GPU/game\n",checks);
}
