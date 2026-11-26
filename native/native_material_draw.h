#pragma once
#include "supersampling_hook_comparison_draw.h"
#include "water_shader_effects.h"
#include "shadow_filter_effects.h"
#include <utility>

// The host serial/TLS scope spans all four transactions and the original draw.
// Restore shadow, water, material, comparison before the original observer.
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
struct ShadowApi {
    static void Begin(IDirect3DDevice9* device,ShadowFilterEffects::DrawTransaction* transaction) noexcept {
        ShadowFilterEffects::BeginDraw(device,transaction);
    }
    static void End(ShadowFilterEffects::DrawTransaction* transaction,HRESULT result) noexcept {
        ShadowFilterEffects::EndDraw(transaction,result);
    }
    static bool Retry(ShadowFilterEffects::DrawTransaction* transaction) noexcept {
        return ShadowFilterEffects::RetryRestore(transaction);
    }
};
namespace Detail {
// Preserve existing header-only fake-COM callers without new shadow linkage.
struct DisabledShadowApi {
    static void Begin(IDirect3DDevice9*,ShadowFilterEffects::DrawTransaction*) noexcept {}
    static void End(ShadowFilterEffects::DrawTransaction*,HRESULT) noexcept {}
    static bool Retry(ShadowFilterEffects::DrawTransaction*) noexcept {return false;}
};
}
struct Result:NativeEffectsDraw::Result {bool water_unsafe=false,shadow_unsafe=false,ambient_unsafe=false;};

template<class ComparisonApi=NativeComparisonDraw::Api,class MaterialApi=NativeEffectsDraw::Api,
         class RiverApi=WaterApi,class FilterApi=ShadowApi,class Draw,class Observe>
Result ExecuteFour(IDirect3DDevice9* device,bool comparison,bool materials,bool water,bool shadow,
                   Draw&& draw,Observe&& observe) {
    MaterialComparison::Transaction comparison_transaction{};
    MaterialShaderEffects::DrawTransaction material_transaction{};
    WaterShaderEffects::DrawTransaction water_transaction{};
    ShadowFilterEffects::DrawTransaction shadow_transaction{};
    Result result;
    bool comparison_begun=false,material_begun=false,water_begun=false,shadow_begun=false;
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
    const auto shadow_safe=[&]() {
        if(shadow_transaction.unsafe_state&&(!FilterApi::Retry(&shadow_transaction)||shadow_transaction.unsafe_state)) {
            result.unsafe=result.shadow_unsafe=true;return false;
        }
        return true;
    };
    const auto unwind=[&]() {
        // Never short-circuit outer cleanup after an inner restoration failure.
        if(shadow_begun){FilterApi::End(&shadow_transaction,result.original_result);shadow_safe();}
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
    if(shadow){shadow_begun=true;FilterApi::Begin(device,&shadow_transaction);
        if(!shadow_safe()){unwind();return result;}}
    result.original_called=true;
    try{result.original_result=draw();}
    catch(...){result.original_result=D3DERR_DRIVERINTERNALERROR;result.exception=true;}
    unwind();
    if(!result.unsafe){observe(result.original_result);result.observer_called=true;}
    return result;
}

template<class ComparisonApi=NativeComparisonDraw::Api,class MaterialApi=NativeEffectsDraw::Api,
         class RiverApi=WaterApi,class Draw,class Observe>
Result Execute(IDirect3DDevice9* device,bool comparison,bool materials,bool water,Draw&& draw,Observe&& observe) {
    return ExecuteFour<ComparisonApi,MaterialApi,RiverApi,Detail::DisabledShadowApi>
        (device,comparison,materials,water,false,std::forward<Draw>(draw),std::forward<Observe>(observe));
}
// Additive combined-diagnostic route. AO is the outer fifth scope; the legacy
// four-stage route and its mutual-exclusion admission remain unchanged.
template<class AmbientApi,class ComparisonApi=NativeComparisonDraw::Api,class MaterialApi=NativeEffectsDraw::Api,
         class RiverApi=WaterApi,class FilterApi=ShadowApi,class Draw,class Observe>
Result ExecuteFive(IDirect3DDevice9* device,bool comparison,bool materials,bool water,bool shadow,bool ambient,
                   Draw&& draw,Observe&& observe) {
    ShadowFilterEffects::DrawTransaction ambient_transaction{};
    Result result;bool begun=false;
    const auto safe=[&](){
        if(ambient_transaction.unsafe_state&&(!AmbientApi::Retry(&ambient_transaction)||ambient_transaction.unsafe_state)) {
            result.unsafe=result.ambient_unsafe=true;return false;
        }
        return true;
    };
    if(ambient){begun=true;AmbientApi::Begin(device,&ambient_transaction);
        if(!safe()){AmbientApi::End(&ambient_transaction,result.original_result);safe();return result;}}
    result=ExecuteFour<ComparisonApi,MaterialApi,RiverApi,FilterApi>(device,comparison,materials,water,shadow,
        std::forward<Draw>(draw),[](HRESULT){});
    result.observer_called=false;
    if(begun){AmbientApi::End(&ambient_transaction,result.original_result);safe();}
    if(result.original_called&&!result.unsafe){observe(result.original_result);result.observer_called=true;}
    return result;
}
}
