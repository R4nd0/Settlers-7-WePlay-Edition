#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "crossplay_setup_root_reader.h"
#include <stdio.h>

extern "C" __declspec(dllexport) uint32_t __cdecl WePlaySetupRootLifetimeMethodsOracle(uint32_t value) {
    return CrossplaySetupRootLifetime::Detail::Methods(static_cast<Crossplay::Edition>(value))?1u:0u;
}
// The compiled register/stack bridge is exercised with each pinned original
// serializer in the bounded offline CPU, for both reader and writer visitors.
extern "C" __declspec(dllexport) uint32_t __cdecl WePlaySetupRootSerializeOracle(
    uint32_t value,void* root,void* visitor,uint32_t version,DWORD* exception) {
    const auto edition=static_cast<Crossplay::Edition>(value);CrossplaySetupRootWriter::Pin pin{};
    return exception && (version==1 || version==2) && CrossplaySetupRootWriter::Layout(edition,pin)
        && CrossplaySetupRootReader::Detail::Serialize(edition,pin,root,visitor,version,*exception)?1u:0u;
}
int main() {
    for(uint32_t value:{0u,4u,UINT32_MAX})if(WePlaySetupRootLifetimeMethodsOracle(value))return 1;
    CrossplaySetupRootReader::Verification result{};
    CrossplayNative::ParticipantEvent binding{};
    for(uint32_t version:{0u,1u,2u,3u}) {
        result.fresh_input_equal=result.active_unchanged=true;
        if(CrossplaySetupRootReader::VerifyReceived(Crossplay::Edition::Gold,binding,{},true,version,{},result)
            || result.fresh_input_equal || result.active_unchanged || result.native.constructed)return 2;
    }
    puts("PASS unsupported lifetime profiles and empty native-reader input rejection; original ABI execution uses the offline CPU.");
    return 0;
}
