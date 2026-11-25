#include "supersampling_hook_comparison_draw.h"
#include <cstdio>
#include <cstdlib>
#include <string>

namespace {
std::string order;bool comparison=false,effects=false,unsafe_outer=false,unsafe_inner=false;
unsigned checks=0;
void Require(bool condition,const char* message){++checks;if(!condition){std::fprintf(stderr,"nested transaction: %s\n",message);std::exit(1);}}
struct Compare {
    static void Begin(IDirect3DDevice9*,MaterialComparison::Transaction* t){order+="C";comparison=true;t->active=true;}
    static void End(MaterialComparison::Transaction* t,HRESULT){order+="c";t->unsafe_state=unsafe_outer;if(!unsafe_outer)comparison=false;}
    static bool Retry(MaterialComparison::Transaction*){order+="r";return false;}
};
struct Effect {
    static void Begin(IDirect3DDevice9*,MaterialShaderEffects::DrawTransaction* t){order+="E";effects=true;t->active=true;}
    static void End(MaterialShaderEffects::DrawTransaction* t,HRESULT){order+="e";t->unsafe_state=unsafe_inner;if(!unsafe_inner)effects=false;}
    static bool Retry(MaterialShaderEffects::DrawTransaction*){order+="R";return false;}
};
auto Draw(HRESULT code=S_OK){return [code](){Require(comparison&&effects,"draw missed nested state");order+="D";return code;};}
auto Observe(){return [](HRESULT){Require(!comparison&&!effects,"observer saw temporary state");order+="O";};}
void Clear(){order.clear();comparison=effects=unsafe_outer=unsafe_inner=false;}
}

int main(){
    Clear();auto result=NativeComparisonDraw::Execute<Compare,Effect>(nullptr,true,true,Draw(),Observe());
    Require(order=="CEDecO"&&!result.unsafe,"nested restore order");
    Clear();result=NativeComparisonDraw::Execute<Compare,Effect>(nullptr,true,true,Draw(D3DERR_INVALIDCALL),Observe());
    Require(order=="CEDecO"&&result.original_result==D3DERR_INVALIDCALL,"failed draw missed outer restore");
    Clear();unsafe_inner=true;result=NativeComparisonDraw::Execute<Compare,Effect>(nullptr,true,true,Draw(),Observe());
    Require(order=="CEDeRc"&&result.unsafe&&!result.observer_called&&!comparison,"unsafe inner stranded outer state or observed");
    Clear();unsafe_outer=true;result=NativeComparisonDraw::Execute<Compare,Effect>(nullptr,true,true,Draw(),Observe());
    Require(order=="CEDecr"&&result.unsafe&&!result.observer_called,"unsafe outer was observed");
    Clear();result=NativeComparisonDraw::Execute<Compare,Effect>(nullptr,false,false,[](){order+="D";return S_OK;},Observe());
    Require(order=="DO"&&!result.unsafe,"disabled comparison/effects changed original path");
    std::printf("nested comparison/effects: %u checks passed\n",checks);return 0;
}
