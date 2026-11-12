#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "crossplay_active_player_writer.h"
#include <stdio.h>

extern "C" __declspec(dllexport) uint32_t __cdecl WePlayActivePlayerMethodsOracle(uint32_t value) {
    CrossplayActivePlayerWriter::Pin pin{};
    return CrossplayActivePlayerWriter::Layout(static_cast<Crossplay::Edition>(value),pin)
        && CrossplayActivePlayerWriter::Methods(pin)?1u:0u;
}
extern "C" __declspec(dllexport) void __cdecl WePlayActivePlayerWriteOracle(
    uint32_t value,void* rows,void* visitor,uint32_t slot,uint32_t version) {
    const auto edition=static_cast<Crossplay::Edition>(value);CrossplayActivePlayerWriter::Pin pin{};
    if(CrossplayActivePlayerWriter::Layout(edition,pin))CrossplayActivePlayerWriter::Detail::Write(edition,pin,rows,visitor,slot,version);
}
int main() {
    for(uint32_t value:{0u,4u,UINT32_MAX})if(WePlayActivePlayerMethodsOracle(value))return 1;
    puts("PASS unsupported active-player ABI profiles; original-image tests run in the offline emulator.");return 0;
}
