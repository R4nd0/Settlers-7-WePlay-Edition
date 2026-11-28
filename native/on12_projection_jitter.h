#pragma once
#include "on12_temporal.h"
#include "water_on12_identity.h"
#include <d3d9.h>
#include <array>
#include <atomic>

namespace On12Graphics {
struct ProjectionJitterStats {
    std::uint64_t applied=0,restored=0,translated_sets=0,logical_gets=0,readbacks=0,verified_frame=0;
    UINT verified_banks=0;HRESULT readback_result=E_PENDING;
};
// Ordered API-level projection updates. Only the current projection bank is
// translated; applications continue to read their original logical constants.
class ProjectionJitter {
public:
    using Set=HRESULT(STDMETHODCALLTYPE*)(IDirect3DDevice9*,UINT,const float*,UINT);
    using Get=HRESULT(STDMETHODCALLTYPE*)(IDirect3DDevice9*,UINT,float*,UINT);
    HRESULT Initialize(IDirect3DDevice9* device,Set set,Get get){if(device_||!device||!set||!get)return E_INVALIDARG;device_=device;set_=set;get_=get;owner_=GetCurrentThreadId();return S_OK;}
    HRESULT Begin(std::uint64_t frame,UINT w,UINT h,bool enabled){
        if(Foreign())return RPC_E_WRONG_THREAD;if(!device_||!frame||frame<=frame_||!w||!h||w>4096||h>4096)return E_INVALIDARG;
        auto hr=Restore();if(FAILED(hr))return hr;frame_=frame;width_=w;height_=h;jitter_=Temporal::Jitter(frame);enabled_=enabled;armed_.store(enabled,std::memory_order_release);
        for(auto& bank:banks_)bank.known=false;stats_.verified_banks=0;stats_.readback_result=E_PENDING;ui_=false;return S_OK;
    }
    HRESULT BeforeDraw(UINT projection,UINT physical_width,UINT physical_height){
        if(Foreign())return RPC_E_WRONG_THREAD;if(!enabled_&&applied_<0)return S_OK;
        if(foreign_.load(std::memory_order_acquire))return E_UNEXPECTED;
        const int index=projection==12?0:projection==64?1:-1;
        if(!enabled_||ui_||suspended_||physical_width!=width_||physical_height!=height_||index<0)return Restore();
        if(applied_==index)return S_OK;
        auto hr=Restore();if(FAILED(hr))return hr;auto& bank=banks_[index];
        if(!bank.known){hr=get_(device_,Slot(index),bank.logical.data(),4);if(FAILED(hr))return hr;bank.known=true;}
        auto physical=bank.logical;for(UINT c=0;c<4;++c){physical[c*4]+=2*jitter_[0]/width_*physical[c*4+3];physical[c*4+1]-=2*jitter_[1]/height_*physical[c*4+3];}
        applied_=index;hr=set_(device_,Slot(index),physical.data(),4);
        if(FAILED(hr)){const auto restored=Restore();return FAILED(restored)?restored:hr;}++stats_.applied;
        // Explicit short diagnostics only: verify each bank once per frame,
        // before drawing, through the original physical constant getter.
        if(!(stats_.verified_banks&(1u<<index))){
            std::array<float,16> readback{};++stats_.readbacks;hr=get_(device_,Slot(index),readback.data(),4);
            if(SUCCEEDED(hr)&&std::memcmp(readback.data(),physical.data(),sizeof(physical)))hr=E_FAIL;
            stats_.readback_result=hr;
            if(FAILED(hr)){const auto restored=Restore();return FAILED(restored)?restored:hr;}
            stats_.verified_frame=frame_;stats_.verified_banks|=1u<<index;
        }return S_OK;
    }
    HRESULT SetConstants(UINT first,const float* values,UINT count,bool recording){
        if(Foreign()){ForeignMutation();return set_(device_,first,values,count);}
        if(!enabled_&&applied_<0)return set_(device_,first,values,count);
        if(!count||first>=256||count>256-first||!values)return set_(device_,first,values,count);
        const bool overlap=applied_>=0&&Intersects(first,count,Slot(applied_));
        std::array<float,1024> physical;
        if(overlap&&!recording){
            if(!WaterOn12Identity::Range(values,static_cast<size_t>(count)*16))return D3DERR_INVALIDCALL;
            std::memcpy(physical.data(),values,static_cast<size_t>(count)*16);
            const UINT slot=Slot(applied_);
            for(UINT n=0;n<count;++n)if(first+n>=slot&&first+n<slot+4){physical[n*4]+=2*jitter_[0]/width_*values[n*4+3];physical[n*4+1]-=2*jitter_[1]/height_*values[n*4+3];}
        }
        const auto hr=set_(device_,first,overlap&&!recording?physical.data():values,count);if(FAILED(hr))return hr;
        for(int index=0;index<2;++index)if(Intersects(first,count,Slot(index))){
            if(index==applied_&&!recording){const UINT slot=Slot(index);
                for(UINT n=0;n<count;++n)if(first+n>=slot&&first+n<slot+4)std::memcpy(banks_[index].logical.data()+(first+n-slot)*4,values+n*4,16);
            }else banks_[index].known=false;
        }
        if(overlap&&!recording)++stats_.translated_sets;return hr;
    }
    HRESULT GetConstants(UINT first,float* values,UINT count){
        const auto hr=get_(device_,first,values,count);if(FAILED(hr)||Foreign()||applied_<0||!count||first>=256||count>256-first)return hr;
        const UINT slot=Slot(applied_);if(Intersects(first,count,slot)){
            for(UINT n=0;n<count;++n)if(first+n>=slot&&first+n<slot+4)std::memcpy(values+n*4,banks_[applied_].logical.data()+(first+n-slot)*4,16);
            ++stats_.logical_gets;
        }return hr;
    }
    HRESULT Suspend(){if(Foreign())return RPC_E_WRONG_THREAD;auto hr=Restore();if(SUCCEEDED(hr))suspended_=true;return hr;}
    HRESULT Resume(){if(Foreign())return RPC_E_WRONG_THREAD;suspended_=false;for(auto& bank:banks_)bank.known=false;return S_OK;}
    HRESULT Ui(){if(Foreign())return RPC_E_WRONG_THREAD;auto hr=Restore();if(SUCCEEDED(hr))ui_=true;return hr;}
    HRESULT Stop(){if(Foreign())return RPC_E_WRONG_THREAD;auto hr=Restore();if(SUCCEEDED(hr)){enabled_=false;armed_.store(false,std::memory_order_release);}return hr;}
    HRESULT Restore(){
        if(Foreign())return RPC_E_WRONG_THREAD;if(applied_<0)return S_OK;
        if(foreign_.load(std::memory_order_acquire))return E_UNEXPECTED;
        const auto hr=set_(device_,Slot(applied_),banks_[applied_].logical.data(),4);if(SUCCEEDED(hr)){applied_=-1;++stats_.restored;}return hr;
    }
    bool Enabled()const{return enabled_;}bool Ready()const{return device_!=nullptr;}
    std::array<float,2> Jitter()const{return jitter_;}std::uint64_t Frame()const{return frame_;}
    ProjectionJitterStats Stats()const{return stats_;}
    void ForeignMutation(){if(armed_.load(std::memory_order_acquire))foreign_.store(true,std::memory_order_release);}
private:
    struct Bank{std::array<float,16> logical{};bool known=false;};
    static UINT Slot(int index){return index==0?12u:64u;}
    static bool Intersects(UINT first,UINT count,UINT slot){return first<slot+4&&first+count>slot;}
    bool Foreign()const{return owner_&&owner_!=GetCurrentThreadId();}
    IDirect3DDevice9* device_=nullptr;Set set_=nullptr;Get get_=nullptr;DWORD owner_=0;
    std::uint64_t frame_=0;UINT width_=0,height_=0;std::array<float,2> jitter_{};std::array<Bank,2> banks_{};
    int applied_=-1;bool enabled_=false,ui_=true,suspended_=false;std::atomic_bool foreign_{false},armed_{false};ProjectionJitterStats stats_;
};
}
