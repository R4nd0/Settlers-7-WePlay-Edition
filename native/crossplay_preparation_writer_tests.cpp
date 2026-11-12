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
extern "C" __declspec(dllexport) uint32_t __cdecl WePlayBinaryWriterSizeOracle() {
    return sizeof(CrossplayNativeCodec::Detail::Writer);
}
// Execute the original scope primitives against actual C++ stack storage, with
// guards on both sides. The emulator supplies only the stream boundary.
extern "C" __declspec(dllexport) uint32_t __cdecl WePlayBinaryWriterScopeOracle(uintptr_t table,void* stream) {
    using CrossplayNativeCodec::Detail::Writer;
    struct Guarded {uint32_t before;Writer writer;uint32_t after;};
    static_assert(offsetof(Guarded,writer)==4 && offsetof(Guarded,after)==0x214,"Writer canary placement");
    Guarded guarded{0x13579bdf,{table,0,static_cast<CrossplayNativeCodec::Detail::Stream*>(stream),0},0x2468ace0};
    const auto* methods=reinterpret_cast<const uintptr_t*>(table);
    for(uint32_t i=0;i<128;++i) {
        reinterpret_cast<void (__thiscall*)(void*,uint32_t)>(methods[0x90/4])(&guarded.writer,0x636f6c42);
        if(guarded.before!=0x13579bdf || guarded.after!=0x2468ace0 || guarded.writer.flags!=i+1)return 0;
    }
    for(uint32_t i=0;i<128;++i)reinterpret_cast<void (__thiscall*)(void*)>(methods[0x98/4])(&guarded.writer);
    return guarded.before==0x13579bdf && guarded.after==0x2468ace0 && !guarded.writer.flags
        && guarded.writer.vt==table && guarded.writer.stream==stream && !guarded.writer.reading?1u:0u;
}
int main() {
    for(uint32_t value:{0u,4u,UINT32_MAX})if(WePlayPreparationMethodsOracle(value))return 1;
    puts("PASS unsupported whole-preparation ABI profiles; original-image tests run in the offline emulator.");return 0;
}
