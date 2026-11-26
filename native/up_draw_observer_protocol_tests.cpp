// Authored CPU COM-shaped forwarding fixture; never creates a device/GPU.
#define CINTERFACE
#include "up_draw_observer_protocol.h"
#include <array>
#include <cstddef>
#include <cstdio>
#include <stdexcept>
#include <type_traits>

static_assert(offsetof(IDirect3DDevice9Vtbl,DrawPrimitiveUP)==UpDrawObserverProtocol::PrimitiveSlot*sizeof(void*),"SDK slot83");
static_assert(offsetof(IDirect3DDevice9Vtbl,DrawIndexedPrimitiveUP)==UpDrawObserverProtocol::IndexedSlot*sizeof(void*),"SDK slot84");
static_assert(std::is_same<decltype(IDirect3DDevice9Vtbl::DrawPrimitiveUP),UpDrawObserverProtocol::PrimitiveMethod>::value,"SDK primitive stdcall ABI");
static_assert(std::is_same<decltype(IDirect3DDevice9Vtbl::DrawIndexedPrimitiveUP),UpDrawObserverProtocol::IndexedMethod>::value,"SDK indexed stdcall ABI");
static_assert(offsetof(IDirect3DDevice9Vtbl,GetVertexShader)==93*sizeof(void*),"SDK fake vertex query slot93");
static_assert(offsetof(IDirect3DDevice9Vtbl,GetPixelShader)==108*sizeof(void*),"SDK fake pixel query slot108");
namespace {
unsigned checks=0,failures=0;
void Check(bool value){++checks;if(!value)++failures;}
constexpr DWORD PostError=0x37578384;
struct Device {
    void** table=nullptr;unsigned calls=0;HRESULT returned=S_OK;bool throw_original=false;
    D3DPRIMITIVETYPE type=D3DPT_POINTLIST;UINT min_vertex=0,vertices=0,primitives=0,stride=0;
    const void* vertex_data=nullptr;const void* index_data=nullptr;D3DFORMAT index_format=D3DFMT_UNKNOWN;
    unsigned post_mxcsr=0;
};
void Post(Device* d){SetLastError(PostError);_mm_setcsr(d->post_mxcsr);if(d->throw_original)throw std::runtime_error("authored original exception");}
HRESULT STDMETHODCALLTYPE Primitive(IDirect3DDevice9* opaque,D3DPRIMITIVETYPE type,UINT count,const void* vertices,UINT stride){
    auto* d=reinterpret_cast<Device*>(opaque);++d->calls;d->type=type;d->primitives=count;d->vertex_data=vertices;d->stride=stride;Post(d);return d->returned;
}
HRESULT STDMETHODCALLTYPE Indexed(IDirect3DDevice9* opaque,D3DPRIMITIVETYPE type,UINT min,UINT vertices,UINT count,
    const void* indices,D3DFORMAT format,const void* data,UINT stride){
    auto* d=reinterpret_cast<Device*>(opaque);++d->calls;d->type=type;d->min_vertex=min;d->vertices=vertices;
    d->primitives=count;d->index_data=indices;d->index_format=format;d->vertex_data=data;d->stride=stride;Post(d);return d->returned;
}
}
int main(){using namespace UpDrawObserverProtocol;
    std::array<void*,85> table{};table[PrimitiveSlot]=reinterpret_cast<void*>(&::Primitive);table[IndexedSlot]=reinterpret_cast<void*>(&::Indexed);
    Device d{};d.table=table.data();auto* device=reinterpret_cast<IDirect3DDevice9*>(&d);
    const auto primitive=reinterpret_cast<PrimitiveMethod>(table[PrimitiveSlot]);const auto indexed=reinterpret_cast<IndexedMethod>(table[IndexedSlot]);
    const void* vertex=reinterpret_cast<void*>(0x12340001u);const void* index=reinterpret_cast<void*>(0x43210003u);
    const unsigned entry_mxcsr=_mm_getcsr();d.post_mxcsr=entry_mxcsr^0x2000u;
    unsigned observed=0,kind=0;HRESULT observed_result=E_FAIL;
    const auto observe=[&](unsigned value,HRESULT result){++observed;kind=value;observed_result=result;SetLastError(0xdeadbeefu);_mm_setcsr(entry_mxcsr);};
    SetLastError(0x37570000);auto result=UpDrawObserverProtocol::Primitive(device,primitive,D3DPT_TRIANGLESTRIP,2,vertex,48,observe);
    Check(result.original_called&&result.original_result==S_OK&&result.observed&&d.calls==1);
    Check(d.type==D3DPT_TRIANGLESTRIP&&d.primitives==2&&d.vertex_data==vertex&&d.stride==48);
    Check(observed==1&&kind==3&&observed_result==S_OK);Check(GetLastError()==PostError);Check(_mm_getcsr()==d.post_mxcsr);
    result=UpDrawObserverProtocol::Indexed(device,indexed,D3DPT_TRIANGLELIST,5,17,3,index,D3DFMT_INDEX32,vertex,44,observe);
    Check(result.original_called&&result.observed&&d.calls==2&&kind==4);
    Check(d.min_vertex==5&&d.vertices==17&&d.primitives==3&&d.index_data==index&&d.index_format==D3DFMT_INDEX32&&d.vertex_data==vertex&&d.stride==44);
    Check(GetLastError()==PostError&&_mm_getcsr()==d.post_mxcsr);
    const auto before=observed;const auto calls=d.calls;
    result=UpDrawObserverProtocol::Primitive(device,primitive,D3DPT_TRIANGLELIST,0,vertex,48,observe);Check(d.calls==calls+1&&!result.observed);
    result=UpDrawObserverProtocol::Primitive(device,primitive,D3DPT_TRIANGLELIST,1,nullptr,48,observe);Check(!result.observed);
    result=UpDrawObserverProtocol::Primitive(device,primitive,D3DPT_TRIANGLELIST,1,vertex,0,observe);Check(!result.observed);
    result=UpDrawObserverProtocol::Indexed(device,indexed,D3DPT_TRIANGLELIST,0,0,1,index,D3DFMT_INDEX16,vertex,48,observe);Check(!result.observed);
    result=UpDrawObserverProtocol::Indexed(device,indexed,D3DPT_TRIANGLELIST,0,3,1,nullptr,D3DFMT_INDEX16,vertex,48,observe);Check(!result.observed);
    result=UpDrawObserverProtocol::Indexed(device,indexed,D3DPT_TRIANGLELIST,0,3,1,index,D3DFMT_UNKNOWN,vertex,48,observe);Check(!result.observed);
    Check(observed==before);
    d.returned=D3DERR_INVALIDCALL;result=UpDrawObserverProtocol::Primitive(device,primitive,D3DPT_TRIANGLELIST,1,vertex,48,observe);
    Check(result.original_result==D3DERR_INVALIDCALL&&!result.observed&&GetLastError()==PostError);
    d.returned=S_OK;result=UpDrawObserverProtocol::Primitive(device,primitive,D3DPT_TRIANGLELIST,1,vertex,48,
        [&](unsigned,HRESULT){SetLastError(1);_mm_setcsr(entry_mxcsr);throw std::runtime_error("authored diagnostic exception");});
    Check(result.original_result==S_OK&&result.diagnostic_exception&&!result.observed);
    Check(GetLastError()==PostError&&_mm_getcsr()==d.post_mxcsr);
    d.throw_original=true;result=UpDrawObserverProtocol::Primitive(device,primitive,D3DPT_TRIANGLELIST,1,vertex,48,observe);
    Check(result.original_called&&result.original_exception&&!result.observed&&result.original_result==D3DERR_DRIVERINTERNALERROR);
    d.throw_original=false;const auto null_calls=d.calls;SetLastError(0x37570009);
    result=UpDrawObserverProtocol::Primitive(device,nullptr,D3DPT_TRIANGLELIST,1,vertex,48,observe);
    Check(!result.original_called&&!result.observed&&d.calls==null_calls&&GetLastError()==0x37570009);
    _mm_setcsr(entry_mxcsr);
    std::printf("{\"format\":\"s7-up-observer-protocol-cpu-v1\",\"checks\":%u,\"failures\":%u,\"sdk_slots\":[83,84],\"game_started\":false,\"gpu_started\":false}\n",checks,failures);
    return failures?1:0;
}
