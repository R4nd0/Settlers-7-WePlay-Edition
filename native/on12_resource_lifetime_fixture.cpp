#include "on12_resource_lifetime.h"
#include "on12_doodad_lifetime.h"
#include <cstdio>
#include <thread>
#include <array>
#include <vector>
#include <algorithm>
#include <atomic>

namespace {
unsigned checks=0;
bool Check(bool value,unsigned line){++checks;if(!value)std::fprintf(stderr,"resource lifetime check failed at %u\n",line);return value;}
#define REQUIRE(x) do{if(!Check(!!(x),__LINE__))return 20;}while(false)
int __cdecl TargetA(){return 1234;}int __cdecl TargetB(){return 5678;}
std::uint64_t CallBlock(unsigned char* address,const void* target){
    std::array<unsigned char,8> value{{0x90,0x90,0x90,0xe8,0,0,0,0}};
    const auto relative=static_cast<DWORD>(reinterpret_cast<std::uintptr_t>(target)-reinterpret_cast<std::uintptr_t>(address)-8);
    std::memcpy(value.data()+4,&relative,4);std::uint64_t result=0;std::memcpy(&result,value.data(),8);return result;
}
}
int main(){
    using namespace On12Graphics;ResourceLifetimes registry(8);
    REQUIRE(registry.Find(0x10000)==0&&registry.Constructions()==0);
    REQUIRE(registry.Constructed(0)==0&&registry.Healthy());
    const auto first=registry.Constructed(0x10000);REQUIRE(first&&registry.Find(0x10000)==first);
    const auto second=registry.Constructed(0x10000);REQUIRE(second>first&&registry.Find(0x10000)==second);
    REQUIRE(!registry.Retired(0x10000,first)&&registry.Find(0x10000)==second);
    REQUIRE(registry.Retired(0x10000,second)&&registry.Find(0x10000)==0);
    REQUIRE(!registry.Retired(0x10000,second));
    const auto third=registry.Constructed(0x10000);REQUIRE(third>second);
    for(std::uintptr_t p=0x20000;p<=0x80000;p+=0x10000)REQUIRE(registry.Constructed(p)>third);
    REQUIRE(registry.Constructed(0x90000)==0&&!registry.Healthy());
    REQUIRE(registry.Find(0x10000)==0&&registry.Constructed(0x10000)==0);
    ResourceLifetimes concurrent;std::array<std::vector<std::uint64_t>,4> results;std::array<std::thread,4> workers;
    for(size_t n=0;n<workers.size();++n)workers[n]=std::thread([&,n]{for(unsigned k=0;k<1000;++k)results[n].push_back(concurrent.Constructed(0x10000+n*0x1000));});
    for(auto& thread:workers)thread.join();
    std::vector<std::uint64_t> all;for(const auto& result:results)all.insert(all.end(),result.begin(),result.end());std::sort(all.begin(),all.end());
    REQUIRE(concurrent.Healthy()&&concurrent.Constructions()==4000&&all.size()==4000);
    for(size_t n=0;n<all.size();++n)REQUIRE(all[n]==n+1);
    for(size_t n=0;n<results.size();++n)REQUIRE(concurrent.Find(0x10000+n*0x1000)==results[n].back());
    concurrent.Invalidate();REQUIRE(!concurrent.Healthy()&&concurrent.Find(0x10000)==0);
    DoodadLifetimes exact_game;REQUIRE(!exact_game.Install()&&!exact_game.Healthy()&&exact_game.Constructions()==0);
    std::array<float,16> world{};REQUIRE(exact_game.Resolve(0x10000,world.data())==0);
    auto* code=static_cast<unsigned char*>(VirtualAlloc(nullptr,4096,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE));REQUIRE(code);
    const auto before=CallBlock(code,reinterpret_cast<void*>(&TargetA)),after=CallBlock(code,reinterpret_cast<void*>(&TargetB));
    std::memcpy(code,&before,8);code[8]=0xc3;DWORD old=0;REQUIRE(VirtualProtect(code,4096,PAGE_EXECUTE_READ,&old)&&FlushInstructionCache(GetCurrentProcess(),code,9));
    const auto call=reinterpret_cast<int(__cdecl*)()>(code);REQUIRE(call()==1234);
    REQUIRE(!ExchangePoolCall(code+1,before,after)&&!ExchangePoolCall(code,after,before)&&call()==1234);
    std::atomic_bool stop{false},bad{false};std::atomic_uint calls{0};
    std::thread execute([&]{while(!stop.load()){const auto value=call();if(value!=1234&&value!=5678)bad.store(true);++calls;}});
    const auto started=GetTickCount64();while(!calls.load()&&GetTickCount64()-started<3000)SwitchToThread();
    for(unsigned n=0;n<1000;++n){REQUIRE(ExchangePoolCall(code,before,after));REQUIRE(ExchangePoolCall(code,after,before));}
    stop.store(true);execute.join();REQUIRE(!bad.load()&&calls.load()>0&&call()==1234);
    REQUIRE(VirtualFree(code,0,MEM_RELEASE));
    std::printf("{\"passed\":true,\"checks\":%u,\"cpu_only\":true,\"address_reuse\":true,\"delayed_retirement\":true,\"concurrent_constructions\":4000,\"game_identity_verified\":false}\n",checks);
}
