#include "supersampling_hook_config.h"
#include <array>
#include <cstdio>
#include <cstdlib>

namespace {
unsigned checks=0;
void Require(bool condition,const char* message) {
    ++checks;if(!condition){std::fprintf(stderr,"config fixture: %s\n",message);std::exit(1);}
}
NativeGraphicsConfig::Version2 Valid() {
    NativeGraphicsConfig::Version2 value{};
    value.base={sizeof(value),2,1920,1080,1280,720,{}};
    std::wcscpy(value.base.report_path,L"Z:\\work\\graphics.json");
    return value;
}
}

int main() {
    using namespace NativeGraphicsConfig;
    auto value=Valid();Version2 output{};
    Require(Decode(&value,sizeof(value),Observe,output)==Error::None,"v2 SSAA-only rejected");
    auto legacy=value.base;legacy.version=1;legacy.size=sizeof(legacy);
    Require(Decode(&legacy,sizeof(legacy),0,output)==Error::None,"v1 compatibility rejected");
    Require(output.flags==0 && Zero(output.catalog_path,sizeof(output.catalog_path)),"v1 trailing state was retained");
    value.base.render_width=value.base.output_width;value.base.render_height=value.base.output_height;
    value.flags=Observe;std::wcscpy(value.catalog_path,L"Z:\\work\\catalog.bin");
    std::wcscpy(value.observer_report_path,L"Z:\\work\\observer.json");value.catalog_sha256[0]=1;
    Require(Decode(&value,sizeof(value),Observe,output)==Error::None,"native-size observation rejected");
    for(DWORD flag:std::array<DWORD,6>{{Water,Terrain,Vegetation,StableShadows,32u,0x80000000u}}) {
        auto unsupported=value;unsupported.flags|=flag;
        Require(Decode(&unsupported,sizeof(unsupported),Observe,output)==Error::Capabilities,"unimplemented capability silently accepted");
    }
    auto bad=value;bad.catalog_sha256[0]=0;
    Require(Decode(&bad,sizeof(bad),Observe,output)==Error::ObserverIdentity,"zero catalog identity accepted");
    for(const wchar_t* path:{L"relative.json",L"Z:\\work\\..\\outside.json",L"Z:\\work\\.\\same.json",L"Z:\\work\\wrong.bin",L"Z:\\work\\a:stream.json"}) {
        bad=value;std::wcscpy(bad.base.report_path,path);
        Require(Decode(&bad,sizeof(bad),Observe,output)==Error::Path,"unsafe or ambiguous report path accepted");
    }
    bad=value;std::wcscpy(bad.observer_report_path,bad.base.report_path);
    Require(Decode(&bad,sizeof(bad),Observe,output)==Error::Path,"shared report file accepted");
    bad=value;bad.flags=0;
    Require(Decode(&bad,sizeof(bad),Observe,output)==Error::UnusedFields,"ignored observer fields accepted");
    bad=value;bad.base.render_width=5120;bad.base.render_height=2880;
    Require(Decode(&bad,sizeof(bad),Observe,output)==Error::Dimensions,"32-bit render budget exceeded");
    bad=value;bad.base.render_width=1919;
    Require(Decode(&bad,sizeof(bad),Observe,output)==Error::Dimensions,"aspect mismatch accepted");
    Require(Decode(&value,sizeof(value)-1,Observe,output)==Error::Size,"truncated v2 accepted");
    std::array<unsigned char,sizeof(value)+1> extended{};
    std::memcpy(extended.data(),&value,sizeof(value));
    Require(Decode(extended.data(),extended.size(),Observe,output)==Error::Size,"oversized v2 accepted");
    bad=value;bad.base.version=3;
    Require(Decode(&bad,sizeof(bad),Observe,output)==Error::Version,"unknown version accepted");
    std::printf("native graphics config: %u checks passed\n",checks);
    return 0;
}
