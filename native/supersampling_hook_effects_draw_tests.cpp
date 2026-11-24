#include "supersampling_hook_effects_draw.h"
#include <cstdio>
#include <cstdlib>
#include <string>

namespace {
unsigned checks=0;
void Require(bool condition,const char* message) {
    ++checks;if(!condition){std::fprintf(stderr,"effects draw protocol: %s\n",message);std::exit(1);}
}
struct FakeApi {
    static inline std::string order;
    static inline bool candidate=false,fail_begin=false,fail_end=false,retry_succeeds=true;
    static void Clear(){order.clear();candidate=false;fail_begin=fail_end=false;retry_succeeds=true;}
    static void Begin(IDirect3DDevice9*,MaterialShaderEffects::DrawTransaction* transaction) {
        order+="B";candidate=true;transaction->role=1;transaction->active=true;transaction->unsafe_state=fail_begin;
    }
    static void End(MaterialShaderEffects::DrawTransaction* transaction,HRESULT) {
        order+="E";transaction->unsafe_state=fail_end;
        if(!fail_end){candidate=false;transaction->active=false;}
    }
    static bool Retry(MaterialShaderEffects::DrawTransaction* transaction) {
        order+="R";
        if(retry_succeeds){candidate=false;transaction->active=false;transaction->unsafe_state=false;}
        return retry_succeeds;
    }
};
auto Draw(HRESULT code=S_OK) {
    return [code](){FakeApi::order+="D";return code;};
}
auto Observer() {
    return [](HRESULT){Require(!FakeApi::candidate,"observer saw temporary GPU state");FakeApi::order+="O";};
}
}

int main() {
    FakeApi::Clear();
    auto result=NativeEffectsDraw::Execute<FakeApi>(nullptr,true,Draw(),Observer());
    Require(FakeApi::order=="BDEO" && !result.unsafe && result.CallerResult()==S_OK,"normal transaction ordering");
    FakeApi::Clear();
    result=NativeEffectsDraw::Execute<FakeApi>(nullptr,true,Draw(D3DERR_INVALIDCALL),Observer());
    Require(FakeApi::order=="BDEO" && result.original_result==D3DERR_INVALIDCALL,"failed original did not restore");
    FakeApi::Clear();FakeApi::fail_begin=true;
    result=NativeEffectsDraw::Execute<FakeApi>(nullptr,true,Draw(),Observer());
    Require(FakeApi::order=="BRDEO" && !result.unsafe,"recoverable Begin did not retry before original");
    FakeApi::Clear();FakeApi::fail_begin=true;FakeApi::retry_succeeds=false;
    result=NativeEffectsDraw::Execute<FakeApi>(nullptr,true,Draw(),Observer());
    Require(FakeApi::order=="BR" && result.unsafe && !result.original_called && !result.observer_called,
        "unrecovered Begin still drew or observed");
    Require(result.CallerResult()==D3DERR_DEVICELOST,"unsafe Begin claimed a normal fallback");
    FakeApi::Clear();FakeApi::fail_end=true;
    result=NativeEffectsDraw::Execute<FakeApi>(nullptr,true,Draw(),Observer());
    Require(FakeApi::order=="BDERO" && !result.unsafe,"recoverable End did not restore before observer");
    FakeApi::Clear();FakeApi::fail_end=true;FakeApi::retry_succeeds=false;
    result=NativeEffectsDraw::Execute<FakeApi>(nullptr,true,Draw(),Observer());
    Require(FakeApi::order=="BDER" && result.original_called && !result.observer_called && result.unsafe,
        "unrecovered End reached original-program observer");
    Require(result.original_result==S_OK && FAILED(result.CallerResult()),"unsafe successful original was mislabeled safe");
    FakeApi::Clear();
    result=NativeEffectsDraw::Execute<FakeApi>(nullptr,false,Draw(),Observer());
    Require(FakeApi::order=="DO" && !result.unsafe,"disabled effects altered the original path");
    FakeApi::Clear();
    result=NativeEffectsDraw::Execute<FakeApi>(nullptr,true,[]()->HRESULT {FakeApi::order+="D";throw 1;},Observer());
    Require(FakeApi::order=="BDEO" && result.exception && result.original_result==D3DERR_DRIVERINTERNALERROR,
        "throwing original escaped restoration");
    std::printf("effects draw protocol: %u checks passed\n",checks);
    return 0;
}
