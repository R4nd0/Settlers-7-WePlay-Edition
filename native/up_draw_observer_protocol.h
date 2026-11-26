// SPDX-License-Identifier: MIT
// Read-only protocol only; Root owns actual vtable installation/TLS/source guards.
#pragma once
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <d3d9.h>
#include <float.h>
#include <intrin.h>
#include <utility>

namespace UpDrawObserverProtocol {
constexpr size_t PrimitiveSlot=83,IndexedSlot=84;
constexpr unsigned PrimitiveKind=3,IndexedKind=4;
using PrimitiveMethod=HRESULT(STDMETHODCALLTYPE*)(IDirect3DDevice9*,D3DPRIMITIVETYPE,UINT,const void*,UINT);
using IndexedMethod=HRESULT(STDMETHODCALLTYPE*)(IDirect3DDevice9*,D3DPRIMITIVETYPE,UINT,UINT,UINT,const void*,D3DFORMAT,const void*,UINT);
struct Result {
    HRESULT original_result=D3DERR_INVALIDCALL;
    bool original_called=false,original_exception=false,observed=false,diagnostic_exception=false;
};
namespace Detail {
struct PostOriginalState {
    alignas(16) unsigned char floating[512];DWORD error=GetLastError();DWORD padding[3]{};
    PostOriginalState(){_fxsave(floating);unsigned ignored=0;_controlfp_s(&ignored,_MCW_EM|_RC_NEAR,_MCW_EM|_MCW_RC);}
    ~PostOriginalState(){_fxrstor(floating);SetLastError(error);}
};
template<class Draw,class Observe> Result Execute(unsigned kind,bool eligible,Draw&& draw,Observe&& observe) noexcept {
    Result result;result.original_called=true;
    try{result.original_result=draw();}
    catch(...){result.original_result=D3DERR_DRIVERINTERNALERROR;result.original_exception=true;}
    // Preserve the ORIGINAL POSTCALL error/FP state, not entry state. The
    // diagnostic may query state but never interprets or copies user buffers.
    PostOriginalState original;
    if(eligible&&SUCCEEDED(result.original_result)&&!result.original_exception){
        try{observe(kind,result.original_result);result.observed=true;}
        catch(...){result.diagnostic_exception=true;}
    }
    return result;
}
}
// Original is invoked exactly once even for zero/invalid-looking parameters.
// Only metadata eligibility controls observation. Never read userVertexData.
// Original UP stream/index-reset side effects are deliberately left intact.
template<class Observe> Result Primitive(IDirect3DDevice9* device,PrimitiveMethod original,D3DPRIMITIVETYPE type,
    UINT primitive_count,const void* user_vertex_data,UINT vertex_stride,Observe&& observe) noexcept {
    if(!original)return {};
    return Detail::Execute(PrimitiveKind,primitive_count&&user_vertex_data&&vertex_stride,
        [&]{return original(device,type,primitive_count,user_vertex_data,vertex_stride);},std::forward<Observe>(observe));
}
template<class Observe> Result Indexed(IDirect3DDevice9* device,IndexedMethod original,D3DPRIMITIVETYPE type,
    UINT min_vertex,UINT vertices,UINT primitive_count,const void* user_index_data,D3DFORMAT index_format,
    const void* user_vertex_data,UINT vertex_stride,Observe&& observe) noexcept {
    if(!original)return {};
    const bool eligible=vertices&&primitive_count&&user_index_data&&user_vertex_data&&vertex_stride
        &&(index_format==D3DFMT_INDEX16||index_format==D3DFMT_INDEX32);
    return Detail::Execute(IndexedKind,eligible,[&]{return original(device,type,min_vertex,vertices,primitive_count,
        user_index_data,index_format,user_vertex_data,vertex_stride);},std::forward<Observe>(observe));
}
}
