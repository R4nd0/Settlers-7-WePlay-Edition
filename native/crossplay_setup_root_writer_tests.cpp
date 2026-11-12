#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "crossplay_setup_root_writer.h"
#include <stdio.h>
extern "C" __declspec(dllexport) uint32_t __cdecl WePlaySetupRootMethodsOracle(uint32_t value) {
    const auto edition=static_cast<Crossplay::Edition>(value);CrossplaySetupRootWriter::Pin pin{};
    return CrossplaySetupRootWriter::Layout(edition,pin) && CrossplaySetupRootWriter::Methods(edition,pin)?1u:0u;
}
extern "C" __declspec(dllexport) void __cdecl WePlaySetupRootWriteOracle(
    uint32_t value,void* root,void* visitor,uint32_t version) {
    const auto edition=static_cast<Crossplay::Edition>(value);CrossplaySetupRootWriter::Pin pin{};
    if(CrossplaySetupRootWriter::Layout(edition,pin))CrossplaySetupRootWriter::Detail::Write(edition,pin,root,visitor,version);
}
int main() {
    for(uint32_t value:{0u,4u,UINT32_MAX})if(WePlaySetupRootMethodsOracle(value))return 1;
    puts("PASS unsupported enclosing-setup ABI profiles; owned-image execution uses the offline CPU.");return 0;
}
