#pragma once
#include "on12_native_temporal.h"
#include "on12_temporal.h"
#include <d3d9.h>
#include <d3d9on12.h>
#include <d3d12.h>
#include <wrl/client.h>
#include "water_on12_identity.h"

namespace On12Graphics {
// Frontend policy: packet updates occur only at a scene/role/jitter transition.
// No shader or legacy constant binding is changed by this policy.
class NativeJitterScope {
public:
    using Call=DWORD(WINAPI*)(void*);
    HRESULT Initialize(std::uint64_t key,Call control,Call query){
        if(key_||!key||!control||!query)return E_INVALIDARG;
        On12NativeTemporal::Query q;q.device12=key;
        WaterOn12Identity::Guard guard;
        if(query(&q)!=1||q.supported!=1||q.owner!=GetCurrentThreadId())return E_NOINTERFACE;
        key_=key;control_=control;query_=query;owner_=GetCurrentThreadId();sequence_=q.sequence;known_=true;last_=q.enabled!=0;
        // An old active scope is never inherited by a newly attached frontend.
        return Send(false);
    }
    HRESULT Begin(std::uint64_t frame,UINT width,UINT height,bool enabled){
        if(!key_)return enabled?E_NOINTERFACE:S_OK;
        if(GetCurrentThreadId()!=owner_)return RPC_E_WRONG_THREAD;
        if(!frame||frame<=frame_||!width||!height||width>4096||height>4096)return E_INVALIDARG;
        const auto jitter=Temporal::Jitter(frame);frame_=frame;width_=width;height_=height;jx_=jitter[0];jy_=jitter[1];active_=enabled;ui_=false;return Reconcile();
    }
    HRESULT Geometry(bool value){if(Foreign())return RPC_E_WRONG_THREAD;if(geometry_==value&&known_)return S_OK;geometry_=value;return Reconcile();}
    HRESULT Ui(){if(Foreign())return RPC_E_WRONG_THREAD;ui_=true;return Reconcile();}
    HRESULT Suspend(){if(Foreign())return RPC_E_WRONG_THREAD;suspended_=true;return Reconcile();}
    HRESULT Resume(){if(Foreign())return RPC_E_WRONG_THREAD;suspended_=false;return Reconcile();}
    HRESULT Stop(){if(Foreign())return RPC_E_WRONG_THREAD;active_=false;ui_=true;return Reconcile();}
    HRESULT Snapshot(On12NativeTemporal::Query& q)const{
        if(!key_)return E_NOINTERFACE;if(GetCurrentThreadId()!=owner_)return RPC_E_WRONG_THREAD;
        q={};q.device12=key_;WaterOn12Identity::Guard guard;return query_(&q)==1?S_OK:E_FAIL;
    }
    bool Ready()const{return key_!=0;}
    bool Enabled()const{return active_;}
    DWORD LastStatus()const{return last_status_;}
    UINT64 Frame()const{return frame_;}
    std::array<float,2> Jitter()const{return{jx_,jy_};}
private:
    bool Foreign()const{return key_&&GetCurrentThreadId()!=owner_;}
    HRESULT Reconcile(){return Send(active_&&!ui_&&!suspended_&&geometry_);}
    HRESULT Send(bool enabled){
        if(!key_)return enabled?E_NOINTERFACE:S_OK;
        if(GetCurrentThreadId()!=owner_)return RPC_E_WRONG_THREAD;
        if(known_&&last_==enabled&&(!enabled||(sent_width_==width_&&sent_height_==height_&&sent_x_==jx_&&sent_y_==jy_)))return S_OK;
        if(sequence_==MAXDWORD)return HRESULT_FROM_WIN32(ERROR_ARITHMETIC_OVERFLOW);
        On12NativeTemporal::Control c;c.device12=key_;c.sequence=sequence_+1;
        if(enabled){c.flags=1;c.width=width_;c.height=height_;c.jitter_x=jx_;c.jitter_y=jy_;}
        WaterOn12Identity::Guard guard;
        last_status_=control_(&c);if(last_status_!=On12NativeTemporal::Accepted){known_=false;return E_FAIL;}
        sequence_=c.sequence;known_=true;last_=enabled;sent_width_=width_;sent_height_=height_;sent_x_=jx_;sent_y_=jy_;return S_OK;
    }
    Call control_=nullptr,query_=nullptr;std::uint64_t key_=0,frame_=0;DWORD owner_=0,sequence_=0,last_status_=0;
    UINT width_=0,height_=0,sent_width_=0,sent_height_=0;float jx_=0,jy_=0,sent_x_=0,sent_y_=0;
    bool active_=false,geometry_=false,ui_=true,suspended_=false,known_=false,last_=false;
};
class NativeTemporalLink {
public:
    HRESULT Initialize(IDirect3DDevice9* native,HMODULE runtime){
        if(!native||!runtime||device_)return E_INVALIDARG;
        const auto control=reinterpret_cast<NativeJitterScope::Call>(GetProcAddress(runtime,"WePlayOn12TemporalControl"));
        const auto query=reinterpret_cast<NativeJitterScope::Call>(GetProcAddress(runtime,"WePlayOn12TemporalQuery"));
        if(!control||!query||!WaterOn12Identity::Belongs(reinterpret_cast<void*>(control),runtime)||!WaterOn12Identity::Belongs(reinterpret_cast<void*>(query),runtime))return E_NOINTERFACE;
        Microsoft::WRL::ComPtr<IDirect3DDevice9On12> bridge;Microsoft::WRL::ComPtr<ID3D12Device> device;
        auto hr=native->QueryInterface(IID_PPV_ARGS(&bridge));if(FAILED(hr))return hr;hr=bridge->GetD3D12Device(IID_PPV_ARGS(&device));if(FAILED(hr))return hr;
        hr=scope_.Initialize(reinterpret_cast<std::uintptr_t>(device.Get()),control,query);if(SUCCEEDED(hr))device_=device;return hr;
    }
    NativeJitterScope& Scope(){return scope_;}
    HRESULT Shutdown(){const auto hr=scope_.Stop();if(SUCCEEDED(hr))device_.Reset();return hr;}
private:
    Microsoft::WRL::ComPtr<ID3D12Device> device_;NativeJitterScope scope_;
};
}
