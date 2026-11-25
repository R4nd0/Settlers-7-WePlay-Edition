#include "native_material_draw.h"
#include <cstdio>
#include <vector>

namespace {
unsigned checks=0;std::vector<int> events;
int bad_stage=0,bad_phase=0;bool retry_works=false;
void Check(bool condition,const char* label){++checks;if(!condition){std::fprintf(stderr,"FAIL: %s\n",label);ExitProcess(1);}}
template<int Stage,class Transaction> struct Fake {
    static void Begin(IDirect3DDevice9*,Transaction* t){events.push_back(Stage*10+1);t->unsafe_state=bad_stage==Stage&&bad_phase==1;}
    static void End(Transaction* t,HRESULT){events.push_back(Stage*10+2);t->unsafe_state=bad_stage==Stage&&bad_phase==2;}
    static bool Retry(Transaction* t){events.push_back(Stage*10+3);if(retry_works)t->unsafe_state=false;return retry_works;}
};
using Comparison=Fake<1,MaterialComparison::Transaction>;
using Materials=Fake<2,MaterialShaderEffects::DrawTransaction>;
using Water=Fake<3,WaterShaderEffects::DrawTransaction>;
}
int main(){
    for(unsigned mask=0;mask<8;++mask) {
        events.clear();bad_stage=bad_phase=0;
        const auto result=NativeMaterialDraw::Execute<Comparison,Materials,Water>(nullptr,(mask&1)!=0,(mask&2)!=0,(mask&4)!=0,
            [](){events.push_back(40);return S_OK;},[](HRESULT hr){Check(hr==S_OK,"observer original HRESULT");events.push_back(50);});
        std::vector<int> expected;
        for(unsigned stage=1;stage<=3;++stage)if(mask&(1u<<(stage-1)))expected.push_back(static_cast<int>(stage*10+1));
        expected.push_back(40);
        for(int stage=3;stage>=1;--stage)if(mask&(1u<<(stage-1)))expected.push_back(stage*10+2);
        expected.push_back(50);
        Check(events==expected,"strict nesting and observer ordering");
        Check(result.original_called&&result.observer_called&&!result.unsafe&&result.CallerResult()==S_OK,"safe original result");
    }
    for(int stage=1;stage<=3;++stage)for(int phase=1;phase<=2;++phase)for(int recovery=0;recovery<2;++recovery) {
        bad_stage=stage;bad_phase=phase;retry_works=recovery!=0;events.clear();
        const auto result=NativeMaterialDraw::Execute<Comparison,Materials,Water>(nullptr,true,true,true,
            [](){events.push_back(40);return S_OK;},[](HRESULT){events.push_back(50);});
        Check(result.unsafe==!retry_works,"unrestored transaction must fail closed");
        Check(result.original_called==(phase==2||retry_works),"unrestored begin cannot draw");
        Check(result.observer_called==retry_works,"unsafe state cannot reach observer");
        Check(!result.unsafe||result.CallerResult()==D3DERR_DEVICELOST,"unsafe caller result");
        for(int outer=1;outer<stage;++outer){
            bool closed=false;for(int event:events)if(event==outer*10+2)closed=true;
            Check(closed,"inner failure still unwinds every begun outer transaction");
        }
        Check(retry_works||result.comparison_unsafe==(stage==1),"comparison fault attribution");
        Check(retry_works||result.material_unsafe==(stage==2),"material fault attribution");
        Check(retry_works||result.water_unsafe==(stage==3),"water fault attribution");
    }
    bad_stage=bad_phase=0;events.clear();
    const auto failed=NativeMaterialDraw::Execute<Comparison,Materials,Water>(nullptr,true,true,true,
        [](){events.push_back(40);return D3DERR_INVALIDCALL;},[](HRESULT hr){Check(hr==D3DERR_INVALIDCALL,"failed original preserved for observer");events.push_back(50);});
    Check(events==std::vector<int>({11,21,31,40,32,22,12,50}),"failed original still restores all transactions");
    Check(!failed.unsafe&&failed.CallerResult()==D3DERR_INVALIDCALL,"failed original HRESULT preserved");
    events.clear();
    const auto thrown=NativeMaterialDraw::Execute<Comparison,Materials,Water>(nullptr,true,true,true,
        []()->HRESULT{events.push_back(40);throw 1;},[](HRESULT hr){Check(hr==D3DERR_DRIVERINTERNALERROR,"exception converted after restoration");events.push_back(50);});
    Check(events==std::vector<int>({11,21,31,40,32,22,12,50}),"throwing original still restores all transactions");
    Check(thrown.exception&&!thrown.unsafe&&thrown.original_called,"exception attribution");
    std::printf("PASS: %u CPU nested-draw checks; no GPU/game\n",checks);
}
