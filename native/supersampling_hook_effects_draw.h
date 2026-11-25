#pragma once
#include "material_shader_effects.h"

// The integrating hook owns the serial device lock and internal/TLS scope.
// This protocol is staged separately from the live observer checkpoint.
namespace NativeEffectsDraw {
struct Api {
    static void Begin(IDirect3DDevice9* device,MaterialShaderEffects::DrawTransaction* transaction) noexcept {
        MaterialShaderEffects::BeginDraw(device,transaction);
    }
    static void End(MaterialShaderEffects::DrawTransaction* transaction,HRESULT result) noexcept {
        MaterialShaderEffects::EndDraw(transaction,result);
    }
    static bool Retry(MaterialShaderEffects::DrawTransaction* transaction) noexcept {
        return MaterialShaderEffects::RetryRestore(transaction);
    }
};
struct Result {
    HRESULT original_result=D3DERR_DEVICELOST;
    bool original_called=false,observer_called=false,unsafe=false,exception=false;
    bool material_unsafe=false,comparison_unsafe=false;
    DWORD role=0;
    HRESULT CallerResult() const {return unsafe?D3DERR_DEVICELOST:original_result;}
};

template<class EffectApi=Api,class Draw,class Observe>
Result Execute(IDirect3DDevice9* device,bool enable_effects,Draw&& draw,Observe&& observe) {
    Result result;
    MaterialShaderEffects::DrawTransaction transaction{};
    const auto restore=[&]() {
        if(transaction.unsafe_state && (!EffectApi::Retry(&transaction) || transaction.unsafe_state)) {
            result.unsafe=true;result.material_unsafe=true;return false;
        }
        return true;
    };
    if(enable_effects) {
        EffectApi::Begin(device,&transaction);result.role=transaction.role;
        if(!restore())return result; // Never draw with an unrecovered Begin failure.
    }
    result.original_called=true;
    try {result.original_result=draw();}
    catch(...) {result.original_result=D3DERR_DRIVERINTERNALERROR;result.exception=true;}
    // Restore on original success, original HRESULT failure and C++ exception.
    // The transaction never escapes this serial callback or borrows cache refs
    // across Present/Reset/Release.
    if(enable_effects) {
        EffectApi::End(&transaction,result.original_result);
        if(!restore())return result;
    }
    observe(result.original_result);result.observer_called=true;
    return result;
}
}
