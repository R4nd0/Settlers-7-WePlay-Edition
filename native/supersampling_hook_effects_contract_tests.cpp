#include "supersampling_hook_effects_contract.h"
#include <cstdio>
#include <cstdlib>
#include <limits>

namespace {
unsigned checks=0;
void Require(bool condition,const char* message) {
    ++checks;if(!condition){std::fprintf(stderr,"effects contract: %s\n",message);std::exit(1);}
}
NativeGraphicsConfig::Version2 Native() {
    NativeGraphicsConfig::Version2 value{};
    value.base={sizeof(value),2,1920,1080,1280,720,{}};
    value.flags=NativeGraphicsConfig::Water|NativeGraphicsConfig::Terrain|NativeGraphicsConfig::Vegetation;
    std::wcscpy(value.base.report_path,L"Z:\\work\\next-case\\hook.private.json");
    std::wcscpy(value.observer_report_path,L"Z:\\work\\next-case\\observer.private.json");
    return value;
}
MaterialShaderEffects::Config Effects() {
    MaterialShaderEffects::Config value{};
    value.flags=MaterialShaderEffects::AllowedFlags;value.render_width=1920;value.render_height=1080;
    std::wcscpy(value.report_path,L"Z:\\work\\next-case\\effects.private.json");
    std::wcscpy(value.leaf_vertex_path,L"Z:\\work\\next-case\\LocalData\\MaterialShaders\\leaf.vs.bin");
    std::wcscpy(value.bump_pixel_path,L"Z:\\work\\next-case\\LocalData\\MaterialShaders\\bump.ps.bin");
    std::wcscpy(value.parallax_pixel_path,L"Z:\\work\\next-case\\LocalData\\MaterialShaders\\parallax.ps.bin");
    return value;
}
}

int main() {
    using namespace NativeEffectsContract;
    const auto native=Native();const auto effects=Effects();
    Require(sizeof(effects)==4132,"fixed transport size changed");
    Require(Validate(effects,native,false)==Error::None,"valid explicit roles rejected");
    Require(Requested(NativeGraphicsConfig::Observe|NativeGraphicsConfig::Water)==MaterialShaderEffects::River,"flag spaces were conflated");
    auto bad=effects;bad.flags=MaterialShaderEffects::River;
    Require(Validate(bad,native,false)==Error::None,"explicit subset toggle rejected");
    bad=effects;bad.flags=0;
    Require(Validate(bad,native,false)==Error::None,"explicit disabled toggle rejected");
    bad=effects;bad.flags|=8;
    Require(Validate(bad,native,false)==Error::Flags,"unknown role accepted");
    auto unrequested=native;unrequested.flags=NativeGraphicsConfig::Observe;
    Require(Validate(effects,unrequested,false)==Error::Flags,"unconfigured material intent accepted");
    bad=effects;bad.render_width=1280;
    Require(Validate(bad,native,false)==Error::Dimensions,"context dimensions did not bind to renderer");
    bad=effects;bad.river_density_scale=std::numeric_limits<float>::quiet_NaN();
    Require(Validate(bad,native,false)==Error::Parameters,"NaN policy accepted");
    bad=effects;bad.terrain_detail_scale=2;
    Require(Validate(bad,native,false)==Error::Parameters,"unbounded policy accepted");
    bad=effects;std::wcscpy(bad.leaf_vertex_path,L"Z:\\work\\next-case-other\\leaf.vs.bin");
    Require(Validate(bad,native,false)==Error::OutsideRoot,"prefix lookalike escaped case root");
    bad=effects;std::wcscpy(bad.report_path,L"Z:\\work\\other-case\\effects.private.json");
    Require(Validate(bad,native,false)==Error::OutsideRoot,"report escaped case root");
    bad=effects;std::wcscpy(bad.report_path,native.base.report_path);
    Require(Validate(bad,native,false)==Error::ReportCollision,"base report would be overwritten");
    bad=effects;std::wcscpy(bad.report_path,native.observer_report_path);
    Require(Validate(bad,native,false)==Error::ReportCollision,"observer report would be overwritten");
    bad=effects;std::wcscpy(bad.bump_pixel_path,bad.parallax_pixel_path);
    Require(Validate(bad,native,false)==Error::ReportCollision,"two shader roles share one artifact");
    bad=effects;bad.leaf_vertex_path[0]=0;
    Require(Validate(bad,native,false)==Error::Path,"enabled leaf has no recipient artifact");
    bad=effects;std::wcscpy(bad.report_path,L"Z:\\work\\next-case\\effects.json");
    Require(Validate(bad,native,false)==Error::Path,"report is not marked private");
    bad=effects;std::wcscpy(bad.leaf_vertex_path,L"Z:\\work\\next-case\\..\\leaf.vs.bin");
    Require(Validate(bad,native,false)==Error::Path,"parent traversal accepted");
    bad=effects;std::wcscpy(bad.leaf_vertex_path,L"Z:\\work\\next-case\\leaf.\\leaf.vs.bin");
    Require(Validate(bad,native,false)==Error::Path,"Win32 trailing-dot alias accepted");
    SetLastError(0x7357);Require(Validate(effects,native,false)==Error::None && GetLastError()==0x7357,"validation leaked LastError");
    std::printf("effects host contract: %u checks passed\n",checks);
    return 0;
}
