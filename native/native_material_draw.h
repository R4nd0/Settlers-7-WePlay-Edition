#pragma once
#include "supersampling_hook_comparison_draw.h"
#include "water_shader_effects.h"

// One serial/TLS scope owns all three transactions. The comparison reads the
// original River binding first, then material and water changes are unwound in
// reverse order before the original-program observer runs.
namespace NativeMaterialDraw {
struct WaterApi {
    static void Begin(IDirect3DDevice9* device,WaterShaderEffects::DrawTransaction* transaction) noexcept {
        WaterShaderEffects::BeginDraw(device,transaction);
    }
    static void End(WaterShaderEffects::DrawTransaction* transaction,HRESULT result) noexcept {
        WaterShaderEffects::EndDraw(transaction,result);
    }
    static bool Retry(WaterShaderEffects::DrawTransaction* transaction) noexcept {
        return WaterShaderEffects::RetryRestore(transaction);
    }
};
struct Result:NativeEffectsDraw::Result {bool water_unsafe=false;};

template<class ComparisonApi=NativeComparisonDraw::Api,class MaterialApi=NativeEffectsDraw::Api,
         class RiverApi=WaterApi,class Draw,class Observe>
Result Execute(IDirect3DDevice9* device,bool comparison,bool materials,bool water,Draw&& draw,Observe&& observe) {
    MaterialComparison::Transaction comparison_transaction{};
    MaterialShaderEffects::DrawTransaction material_transaction{};
    WaterShaderEffects::DrawTransaction water_transaction{};
    Result result;
    bool comparison_begun=false,material_begun=false,water_begun=false;
    const auto comparison_safe=[&]() {
        if(comparison_transaction.unsafe_state&&(!ComparisonApi::Retry(&comparison_transaction)||comparison_transaction.unsafe_state)) {
            result.unsafe=result.comparison_unsafe=true;return false;
        }
        return true;
    };
    const auto material_safe=[&]() {
        if(material_transaction.unsafe_state&&(!MaterialApi::Retry(&material_transaction)||material_transaction.unsafe_state)) {
            result.unsafe=result.material_unsafe=true;return false;
        }
        return true;
    };
    const auto water_safe=[&]() {
        if(water_transaction.unsafe_state&&(!RiverApi::Retry(&water_transaction)||water_transaction.unsafe_state)) {
            result.unsafe=result.water_unsafe=true;return false;
        }
        return true;
    };
    const auto unwind=[&]() {
        if(water_begun){RiverApi::End(&water_transaction,result.original_result);water_safe();}
        if(material_begun){MaterialApi::End(&material_transaction,result.original_result);material_safe();}
        if(comparison_begun){ComparisonApi::End(&comparison_transaction,result.original_result);comparison_safe();}
    };
    if(comparison){comparison_begun=true;ComparisonApi::Begin(device,&comparison_transaction);
        if(!comparison_safe()){unwind();return result;}}
    if(materials){material_begun=true;MaterialApi::Begin(device,&material_transaction);result.role=material_transaction.role;
        if(!material_safe()){unwind();return result;}}
    if(water){water_begun=true;RiverApi::Begin(device,&water_transaction);
        if(!water_safe()){unwind();return result;}}
    result.original_called=true;
    try{result.original_result=draw();}
    catch(...){result.original_result=D3DERR_DRIVERINTERNALERROR;result.exception=true;}
    unwind();
    if(!result.unsafe){observe(result.original_result);result.observer_called=true;}
    return result;
}
}
