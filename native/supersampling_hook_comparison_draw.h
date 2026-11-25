#pragma once
#include "supersampling_hook_effects_draw.h"
#include "material_comparison.h"

// Diagnostic comparison is an outer transaction: its uniform must be restored
// after the inner material transaction and before the original-program observer.
namespace NativeComparisonDraw {
struct Api {
    static void Begin(IDirect3DDevice9* device,MaterialComparison::Transaction* transaction) noexcept {
        MaterialComparison::BeginDraw(device,transaction);
    }
    static void End(MaterialComparison::Transaction* transaction,HRESULT result) noexcept {
        MaterialComparison::EndDraw(transaction,result);
    }
    static bool Retry(MaterialComparison::Transaction* transaction) noexcept {
        return MaterialComparison::RetryRestore(transaction);
    }
};

template<class ComparisonApi=Api,class EffectApi=NativeEffectsDraw::Api,class Draw,class Observe>
NativeEffectsDraw::Result Execute(IDirect3DDevice9* device,bool comparison,bool effects,Draw&& draw,Observe&& observe) {
    MaterialComparison::Transaction outer{};
    NativeEffectsDraw::Result result;
    const auto restore=[&]() {
        if(outer.unsafe_state && (!ComparisonApi::Retry(&outer) || outer.unsafe_state)){
            result.unsafe=true;result.comparison_unsafe=true;return false;
        }
        return true;
    };
    if(comparison){ComparisonApi::Begin(device,&outer);if(!restore())return result;}
    result=NativeEffectsDraw::Execute<EffectApi>(device,effects,static_cast<Draw&&>(draw),[](HRESULT){});
    result.observer_called=false;
    // Even an unsafe inner transaction must not strand the outer PS80 change.
    if(comparison){ComparisonApi::End(&outer,result.original_result);restore();}
    if(result.unsafe)return result;
    observe(result.original_result);result.observer_called=true;
    return result;
}
}
