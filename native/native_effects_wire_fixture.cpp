#include "native_effects_wire.h"
#include <cstdio>
#include <cstring>

namespace {
unsigned checks=0;
void Check(bool value,const char* name) {
    ++checks;if(!value){std::fprintf(stderr,"FAIL: %s\n",name);ExitProcess(1);}
}
struct Guarded {
    unsigned char* allocation=nullptr;size_t page=0;
    Guarded() {
        SYSTEM_INFO info{};GetSystemInfo(&info);page=info.dwPageSize;
        allocation=static_cast<unsigned char*>(VirtualAlloc(nullptr,page*3,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));
        Check(allocation!=nullptr,"guard allocation");DWORD old=0;
        Check(VirtualProtect(allocation+page*2,page,PAGE_NOACCESS,&old)!=FALSE,"guard protection");
    }
    ~Guarded(){if(allocation)VirtualFree(allocation,0,MEM_RELEASE);}
    unsigned char* Tail(size_t count){Check(count<=page*2,"guarded count");return allocation+page*2-count;}
};
}

int main() {
    NativeEffectsWire::Pending output{};
    output.version=91;output.v1.flags=73;output.v2.flags=69;
    const auto refused=[&](const void* input,const char* name) {
        const auto before=output;Check(!NativeEffectsWire::Decode(input,&output),name);
        Check(std::memcmp(&before,&output,sizeof(output))==0,"refusal preserves pending configuration");
    };
    refused(nullptr,"null input");
    MaterialShaderEffects::Config v1{};v1.flags=7;v1.render_width=3840;v1.render_height=2160;
    Check(!NativeEffectsWire::Decode(&v1,nullptr),"null output");
    Guarded region;
    auto* old=region.Tail(sizeof(v1));std::memcpy(old,&v1,sizeof(v1));
    Check(NativeEffectsWire::Decode(old,&output),"legacy allocation ending at guard page");
    Check(output.version==1&&output.Flags()==7&&std::memcmp(&output.v1,&v1,sizeof(v1))==0,"legacy exact transport");
    MaterialShaderConfigV2::Config v2{};v2.flags=6;v2.render_width=3840;v2.render_height=2160;
    auto* modern=region.Tail(sizeof(v2));std::memcpy(modern,&v2,sizeof(v2));
    Check(NativeEffectsWire::Decode(modern,&output),"V2 allocation ending at guard page");
    Check(output.version==2&&output.Flags()==6&&std::memcmp(&output.v2,&v2,sizeof(v2))==0,"V2 exact transport");
    MaterialShaderConfigV3::Config v3{};v3.flags=4;v3.render_width=3840;v3.render_height=2160;
    auto* wrapped=region.Tail(sizeof(v3));std::memcpy(wrapped,&v3,sizeof(v3));
    Check(NativeEffectsWire::Decode(wrapped,&output),"V3 allocation ending at guard page");
    Check(output.version==3&&output.Flags()==4&&std::memcmp(&output.v3,&v3,sizeof(v3))==0,"V3 exact typed transport");
    Check(MaterialShaderConfigV3::FitsWorker(392)&&MaterialShaderConfigV3::FitsWorker(988)&&!MaterialShaderConfigV3::FitsWorker(989),"V3 complete payload prefix bound988");
    DWORD header[2]={sizeof(v2),2};
    old=region.Tail(sizeof(v1));std::memcpy(old,&v1,sizeof(v1));std::memcpy(old,header,sizeof(header));
    refused(old,"legacy-sized allocation cannot claim full V2 body");
    header[0]=sizeof(v3);header[1]=3;modern=region.Tail(sizeof(v2));std::memcpy(modern,&v2,sizeof(v2));std::memcpy(modern,header,sizeof(header));
    refused(modern,"V2-sized allocation cannot claim V3 body");
    old=region.Tail(sizeof(v1));std::memcpy(old,&v1,sizeof(v1));std::memcpy(old,header,sizeof(header));refused(old,"legacy allocation cannot claim V3 body");
    for(const auto size:{0u,8u,4131u,4133u,6179u,6181u,7203u,7205u,0xffffffffu}) {
        DWORD bad[2]={size,2};refused(bad,"unknown size rejected from header");
    }
    for(const auto version:{0u,1u,3u,0xffffffffu}) {
        DWORD bad[2]={sizeof(v2),version};refused(bad,"unknown size/version pairing");
    }
    for(const auto version:{0u,1u,2u,4u,0xffffffffu}){DWORD bad[2]={sizeof(v3),version};refused(bad,"V3 size requires version3 only");}
    auto* partial=region.Tail(7);std::memset(partial,0,7);refused(partial,"truncated header");
    refused(region.Tail(0),"inaccessible header");
    // Zero means an explicit safe-boundary disable request, not an implicit ABI
    // conversion or permission to initialize an empty resource configuration.
    v2.flags=0;Check(NativeEffectsWire::Decode(&v2,&output)&&output.version==2&&output.Flags()==0,"explicit V2 disable transport");
    v1.flags=0;Check(NativeEffectsWire::Decode(&v1,&output)&&output.version==1&&output.Flags()==0,"explicit V1 disable transport");
    v3.flags=0;Check(NativeEffectsWire::Decode(&v3,&output)&&output.version==3&&output.Flags()==0,"explicit V3 disable transport");
    std::printf("PASS: %u CPU wire/guard-page checks; no GPU or game started\n",checks);
}
