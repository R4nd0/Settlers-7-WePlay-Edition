#include "on12_native_scope.h"
#include <cstdio>
#include <thread>

namespace {
unsigned checks=0,packets=0,dirty=0;bool fail=false;
bool Check(bool okay,unsigned line){++checks;if(!okay)std::fprintf(stderr,"native scope check failed at %u\n",line);return okay;}
#define REQUIRE(x) do{if(!Check(!!(x),__LINE__))return 20;}while(false)
DWORD WINAPI Control(void* p){++packets;return !fail&&On12NativeTemporal::Configure(*static_cast<On12NativeTemporal::Control*>(p))?1u:0u;}
DWORD WINAPI Query(void* p){return On12NativeTemporal::Snapshot(*static_cast<On12NativeTemporal::Query*>(p))?1u:0u;}
void Dirty(void*){++dirty;}
}
int main(){
    On12NativeTemporal::State driver;auto* key=reinterpret_cast<void*>(static_cast<std::uintptr_t>(0x123400));REQUIRE(driver.Register(key,&dirty,Dirty));driver.ObserveOwner();
    On12Graphics::NativeJitterScope scope;REQUIRE(SUCCEEDED(scope.Initialize(reinterpret_cast<std::uintptr_t>(key),Control,Query))&&scope.Ready()&&!packets);
    REQUIRE(SUCCEEDED(scope.Begin(1,1280,720,true))&&!packets);
    REQUIRE(SUCCEEDED(scope.Geometry(true))&&packets==1);
    for(unsigned i=0;i<1000;++i)REQUIRE(SUCCEEDED(scope.Geometry(true)));
    REQUIRE(packets==1);On12NativeTemporal::Query q;REQUIRE(SUCCEEDED(scope.Snapshot(q))&&q.enabled&&q.configurations==1);
    REQUIRE(SUCCEEDED(scope.Suspend())&&packets==2);REQUIRE(SUCCEEDED(scope.Resume())&&packets==3);
    REQUIRE(SUCCEEDED(scope.Geometry(false))&&packets==4);REQUIRE(SUCCEEDED(scope.Ui())&&packets==4);
    REQUIRE(SUCCEEDED(scope.Geometry(true))&&packets==4); // UI never inherits a later geometry bind.
    REQUIRE(SUCCEEDED(scope.Begin(2,1280,720,true))&&packets==5);
    REQUIRE(scope.Begin(2,1280,720,true)==E_INVALIDARG&&packets==5);
    const auto before=packets;HRESULT foreign=S_OK;std::thread thread([&]{foreign=scope.Geometry(false);});thread.join();REQUIRE(foreign==RPC_E_WRONG_THREAD&&packets==before);
    REQUIRE(SUCCEEDED(scope.Snapshot(q))&&q.enabled&&q.width==1280&&q.height==720);
    REQUIRE(SUCCEEDED(scope.Stop())&&packets==6);REQUIRE(SUCCEEDED(scope.Snapshot(q))&&!q.enabled);
    REQUIRE(SUCCEEDED(scope.Begin(3,1280,720,false))&&packets==6);
    REQUIRE(scope.Begin(0,1280,720,true)==E_INVALIDARG);
    fail=true;REQUIRE(FAILED(scope.Begin(4,1280,720,true)));fail=false;
    REQUIRE(SUCCEEDED(scope.Stop())); // Failed updates are not treated as known hardware state.
    REQUIRE(SUCCEEDED(scope.Snapshot(q))&&!q.enabled);
    std::printf("{\"passed\":true,\"checks\":%u,\"cpu_only\":true,\"gpu_device_created\":false,\"repeated_role_binds\":1000,\"redundant_packets\":0}\n",checks);return 0;
}
