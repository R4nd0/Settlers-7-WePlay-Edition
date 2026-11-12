#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "crossplay_preparation_writer.h"
#include <stdio.h>

extern "C" __declspec(dllexport) uint32_t __cdecl WePlayPreparationMethodsOracle(uint32_t value) {
    const auto edition=static_cast<Crossplay::Edition>(value);CrossplayPreparationWriter::Pin pin{};
    return CrossplayPreparationWriter::Layout(edition,pin) && CrossplayPreparationWriter::Methods(edition,pin)?1u:0u;
}
extern "C" __declspec(dllexport) void __cdecl WePlayPreparationWriteOracle(
    uint32_t value,void* rows,void* visitor,uint32_t version) {
    const auto edition=static_cast<Crossplay::Edition>(value);CrossplayPreparationWriter::Pin pin{};
    if(CrossplayPreparationWriter::Layout(edition,pin))CrossplayPreparationWriter::Detail::Write(edition,pin,rows,visitor,version);
}
int main() {
    for(uint32_t value:{0u,4u,UINT32_MAX})if(WePlayPreparationMethodsOracle(value))return 1;
    puts("PASS unsupported whole-preparation ABI profiles; original-image tests run in the offline emulator.");return 0;
}
